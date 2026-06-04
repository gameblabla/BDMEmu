#include "bdm_sound.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define BDM_SOUND_DEFAULT_SAMPLE_RATE 44100u
#define BDM_SOUND_DEFAULT_STEP_RATE   2000000u
#define BDM_SOUND_CPU_HZ              8000000u

typedef struct bdm_timer8_audio {
    uint8_t tcr;
    uint8_t tcsr;
    uint8_t tcora;
    uint8_t tcorb;
    uint8_t tcnt;
    uint32_t phase;
    uint32_t phase_inc;
    int volume;
    bool gate;
    unsigned release_samples;
} bdm_timer8_audio_t;

struct bdm_sound {
    unsigned sample_rate;
    unsigned step_rate;
    uint64_t step_accum;
    int recording_enabled;

    int16_t *recorded;
    size_t frames;
    size_t capacity;

    bdm_timer8_audio_t timer8[2];
    uint64_t event_count;
    uint64_t last_event_step;
    uint16_t last_io_address;
    uint8_t last_io_value;

    uint32_t noise_lfsr;
    unsigned click_envelope;
};

static unsigned timer8_divisor(unsigned channel, uint8_t tcr) {
    /* MAME's H8/3334 timer8 construction lists the clock divisors used by the
       two channels.  The exact output-pin mode is not implemented in MAME for
       this driver, but the divisors are a useful, hardware-grounded basis for
       a tentative speaker/PWM model. */
    static const unsigned div0[8] = { 8u, 2u, 64u, 32u, 1024u, 256u, 1024u, 1024u };
    static const unsigned div1[8] = { 8u, 2u, 64u, 128u, 1024u, 2048u, 2048u, 2048u };
    unsigned idx = (unsigned)(tcr & 0x07u);
    return channel ? div1[idx] : div0[idx];
}

static void timer8_recalc(bdm_sound_t *s, unsigned channel) {
    if (!s || channel >= 2u || s->sample_rate == 0u) return;
    bdm_timer8_audio_t *t = &s->timer8[channel];
    unsigned period = t->tcorb ? t->tcorb : t->tcora;
    if (period == 0u) period = 1u;
    unsigned div = timer8_divisor(channel, t->tcr);
    unsigned freq = BDM_SOUND_CPU_HZ / (div * 2u * (period + 1u));
    if (freq < 40u) freq = 40u;
    if (freq > s->sample_rate / 2u) freq = s->sample_rate / 2u;
    t->phase_inc = (uint32_t)(((uint64_t)freq << 32) / s->sample_rate);
}

static int ensure_capacity(bdm_sound_t *s, size_t add) {
    if (!s || !s->recording_enabled) return 0;
    if (add > (size_t)-1 - s->frames) return -1;
    size_t need = s->frames + add;
    if (need <= s->capacity) return 0;
    size_t cap = s->capacity ? s->capacity : 8192u;
    while (cap < need) {
        if (cap > ((size_t)-1 / 2u)) return -1;
        cap *= 2u;
    }
    int16_t *p = (int16_t *)realloc(s->recorded, cap * sizeof(*p));
    if (!p) return -1;
    s->recorded = p;
    s->capacity = cap;
    return 0;
}

static int16_t clamp_s16(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static int synth_one(bdm_sound_t *s) {
    int sample = 0;
    for (unsigned ch = 0; ch < 2u; ++ch) {
        bdm_timer8_audio_t *t = &s->timer8[ch];
        bool active = t->gate || t->release_samples != 0u;
        if (active && t->phase_inc) {
            t->phase += t->phase_inc;
            int vol = t->volume;
            if (!t->gate && t->release_samples) {
                vol = (int)((unsigned)vol * t->release_samples / (s->sample_rate / 60u + 1u));
            }
            sample += (t->phase & 0x80000000u) ? vol : -vol;
            if (!t->gate && t->release_samples) --t->release_samples;
        }
    }

    if (s->click_envelope) {
        /* Short, low-level transient for timer/status writes.  This gives the
           observed UI beeps/clicks audible onset without pretending that the
           unidentified gate array is fully understood. */
        uint32_t bit = ((s->noise_lfsr >> 0) ^ (s->noise_lfsr >> 2) ^ (s->noise_lfsr >> 3) ^ (s->noise_lfsr >> 5)) & 1u;
        s->noise_lfsr = (s->noise_lfsr >> 1) | (bit << 31);
        int amp = (int)(s->click_envelope * 10u);
        sample += (s->noise_lfsr & 1u) ? amp : -amp;
        --s->click_envelope;
    }

    return sample;
}

bdm_sound_t *bdm_sound_create(void) {
    bdm_sound_t *s = (bdm_sound_t *)calloc(1, sizeof(*s));
    if (s) {
        s->sample_rate = BDM_SOUND_DEFAULT_SAMPLE_RATE;
        s->step_rate = BDM_SOUND_DEFAULT_STEP_RATE;
        bdm_sound_reset(s);
    }
    return s;
}

void bdm_sound_destroy(bdm_sound_t *sound) {
    if (!sound) return;
    free(sound->recorded);
    free(sound);
}

void bdm_sound_reset(bdm_sound_t *s) {
    if (!s) return;
    s->step_accum = 0;
    s->frames = 0;
    s->event_count = 0;
    s->last_event_step = 0;
    s->last_io_address = 0;
    s->last_io_value = 0;
    s->noise_lfsr = 0x13579bdfu;
    s->click_envelope = 0;
    memset(s->timer8, 0, sizeof(s->timer8));
    s->timer8[0].volume = 5600;
    s->timer8[1].volume = 2200;
    timer8_recalc(s, 0);
    timer8_recalc(s, 1);
}

void bdm_sound_set_sample_rate(bdm_sound_t *s, unsigned sample_rate) {
    if (!s || sample_rate < 8000u || sample_rate > 192000u) return;
    s->sample_rate = sample_rate;
    timer8_recalc(s, 0);
    timer8_recalc(s, 1);
}

unsigned bdm_sound_sample_rate(const bdm_sound_t *s) {
    return s ? s->sample_rate : 0u;
}

void bdm_sound_set_step_rate(bdm_sound_t *s, unsigned steps_per_second) {
    if (!s || steps_per_second < 1000u) return;
    s->step_rate = steps_per_second;
}

unsigned bdm_sound_step_rate(const bdm_sound_t *s) {
    return s ? s->step_rate : 0u;
}

void bdm_sound_enable_recording(bdm_sound_t *s, int enabled) {
    if (!s) return;
    s->recording_enabled = enabled != 0;
}

int bdm_sound_recording_enabled(const bdm_sound_t *s) {
    return s ? s->recording_enabled : 0;
}

void bdm_sound_advance_steps(bdm_sound_t *s, uint64_t steps) {
    if (!s || !s->recording_enabled || s->sample_rate == 0u || s->step_rate == 0u) return;
    s->step_accum += steps * (uint64_t)s->sample_rate;
    while (s->step_accum >= (uint64_t)s->step_rate) {
        s->step_accum -= (uint64_t)s->step_rate;
        if (ensure_capacity(s, 1u) != 0) return;
        s->recorded[s->frames++] = clamp_s16(synth_one(s));
    }
}

void bdm_sound_io_write(bdm_sound_t *s, uint16_t address, uint8_t value, uint64_t step) {
    if (!s) return;
    s->last_io_address = address;
    s->last_io_value = value;

    bdm_timer8_audio_t *t = NULL;
    unsigned ch = 0;
    switch (address) {
    case 0xffc8u: t = &s->timer8[0]; ch = 0; t->tcr = value; timer8_recalc(s, ch); break;
    case 0xffc9u: {
        t = &s->timer8[0];
        uint8_t old = t->tcsr;
        t->tcsr = value;
        /* In the DBZ path the program toggles timer8-0 TCSR between 0x05 and
           0x06 at UI transition points.  Treat bit 1 as the tentative sound
           gate and add a short release to avoid hard clipping. */
        bool new_gate = (value & 0x02u) != 0u;
        if (old != value) {
            ++s->event_count;
            s->last_event_step = step;
            s->click_envelope = s->sample_rate / 100u;
        }
        if (t->gate && !new_gate) t->release_samples = s->sample_rate / 60u;
        t->gate = new_gate;
        break;
    }
    case 0xffcau: t = &s->timer8[0]; ch = 0; t->tcora = value; timer8_recalc(s, ch); break;
    case 0xffcbu: t = &s->timer8[0]; ch = 0; t->tcorb = value; timer8_recalc(s, ch); break;
    case 0xffccu: t = &s->timer8[0]; t->tcnt = value; break;

    case 0xffd0u: t = &s->timer8[1]; ch = 1; t->tcr = value; timer8_recalc(s, ch); break;
    case 0xffd1u:
        t = &s->timer8[1];
        t->tcsr = value;
        /* Channel 1 is configured by the startup code, but in the currently
           validated path it is not later gated like channel 0.  Keep it muted
           unless a future game sets bit 1, which avoids a permanent carrier. */
        t->gate = (value & 0x02u) != 0u;
        break;
    case 0xffd2u: t = &s->timer8[1]; ch = 1; t->tcora = value; timer8_recalc(s, ch); break;
    case 0xffd3u: t = &s->timer8[1]; ch = 1; t->tcorb = value; timer8_recalc(s, ch); break;
    case 0xffd4u: t = &s->timer8[1]; t->tcnt = value; break;
    default:
        break;
    }
}

size_t bdm_sound_mix_s16(bdm_sound_t *sound, int16_t *out, size_t frames, unsigned sample_rate) {
    if (!out) return 0;
    if (!sound) {
        memset(out, 0, frames * sizeof(*out));
        return frames;
    }
    unsigned old_rate = sound->sample_rate;
    if (sample_rate && sample_rate != sound->sample_rate) bdm_sound_set_sample_rate(sound, sample_rate);
    for (size_t i = 0; i < frames; ++i) out[i] = clamp_s16(synth_one(sound));
    if (sample_rate && old_rate != sound->sample_rate) bdm_sound_set_sample_rate(sound, old_rate);
    return frames;
}

const int16_t *bdm_sound_recorded_samples(const bdm_sound_t *sound, size_t *frames) {
    if (frames) *frames = sound ? sound->frames : 0u;
    return sound ? sound->recorded : NULL;
}

size_t bdm_sound_recorded_frames(const bdm_sound_t *sound) {
    return sound ? sound->frames : 0u;
}

uint64_t bdm_sound_event_count(const bdm_sound_t *sound) {
    return sound ? sound->event_count : 0u;
}

uint8_t bdm_sound_last_timer0_tcsr(const bdm_sound_t *sound) {
    return sound ? sound->timer8[0].tcsr : 0u;
}

#define BDM_SOUND_STATE_SIZE (4u + 4u + 4u + 4u + 8u + 1u + (2u * (5u + 4u + 4u + 4u + 1u + 4u)) + 8u + 8u + 2u + 1u + 4u + 4u)

static void snd_w16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t snd_r16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static void snd_w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t snd_r32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void snd_w64(uint8_t *p, uint64_t v) { for (unsigned i = 0; i < 8u; ++i) p[i] = (uint8_t)(v >> (i * 8u)); }
static uint64_t snd_r64(const uint8_t *p) { uint64_t v = 0; for (unsigned i = 0; i < 8u; ++i) v |= (uint64_t)p[i] << (i * 8u); return v; }

size_t bdm_sound_state_size(const bdm_sound_t *sound) {
    (void)sound;
    return BDM_SOUND_STATE_SIZE;
}

size_t bdm_sound_save_state(const bdm_sound_t *s, void *out_data, size_t out_capacity) {
    if (!s) return 0u;
    size_t need = BDM_SOUND_STATE_SIZE;
    if (!out_data || out_capacity < need) return need;
    uint8_t *p = (uint8_t *)out_data;
    memcpy(p, "BDMS", 4); p += 4;
    snd_w32(p, 1u); p += 4;
    snd_w32(p, s->sample_rate); p += 4;
    snd_w32(p, s->step_rate); p += 4;
    snd_w64(p, s->step_accum); p += 8;
    *p++ = s->recording_enabled ? 1u : 0u;
    for (unsigned ch = 0; ch < 2u; ++ch) {
        const bdm_timer8_audio_t *t = &s->timer8[ch];
        *p++ = t->tcr; *p++ = t->tcsr; *p++ = t->tcora; *p++ = t->tcorb; *p++ = t->tcnt;
        snd_w32(p, t->phase); p += 4;
        snd_w32(p, t->phase_inc); p += 4;
        snd_w32(p, (uint32_t)(int32_t)t->volume); p += 4;
        *p++ = t->gate ? 1u : 0u;
        snd_w32(p, t->release_samples); p += 4;
    }
    snd_w64(p, s->event_count); p += 8;
    snd_w64(p, s->last_event_step); p += 8;
    snd_w16(p, s->last_io_address); p += 2;
    *p++ = s->last_io_value;
    snd_w32(p, s->noise_lfsr); p += 4;
    snd_w32(p, s->click_envelope); p += 4;
    return need;
}

int bdm_sound_load_state(bdm_sound_t *s, const void *data, size_t size) {
    if (!s || !data || size < BDM_SOUND_STATE_SIZE) return -1;
    const uint8_t *p = (const uint8_t *)data;
    if (memcmp(p, "BDMS", 4) != 0) return -1;
    p += 4;
    if (snd_r32(p) != 1u) return -1;
    p += 4;
    s->sample_rate = snd_r32(p); p += 4;
    s->step_rate = snd_r32(p); p += 4;
    s->step_accum = snd_r64(p); p += 8;
    s->recording_enabled = *p++ != 0u;
    for (unsigned ch = 0; ch < 2u; ++ch) {
        bdm_timer8_audio_t *t = &s->timer8[ch];
        t->tcr = *p++; t->tcsr = *p++; t->tcora = *p++; t->tcorb = *p++; t->tcnt = *p++;
        t->phase = snd_r32(p); p += 4;
        t->phase_inc = snd_r32(p); p += 4;
        t->volume = (int)(int32_t)snd_r32(p); p += 4;
        t->gate = *p++ != 0u;
        t->release_samples = snd_r32(p); p += 4;
    }
    s->event_count = snd_r64(p); p += 8;
    s->last_event_step = snd_r64(p); p += 8;
    s->last_io_address = snd_r16(p); p += 2;
    s->last_io_value = *p++;
    s->noise_lfsr = snd_r32(p); p += 4;
    s->click_envelope = snd_r32(p); p += 4;
    if (s->sample_rate < 8000u || s->sample_rate > 192000u) s->sample_rate = BDM_SOUND_DEFAULT_SAMPLE_RATE;
    if (s->step_rate < 1000u) s->step_rate = BDM_SOUND_DEFAULT_STEP_RATE;
    return 0;
}
