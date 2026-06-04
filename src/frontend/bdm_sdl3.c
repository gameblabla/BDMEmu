#include "bdm_core.h"
#include "bdm_input.h"
#include "bdm_sound.h"
#include "bdm_video.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BDM_SDL_DEFAULT_SCALE 3u
#define BDM_TOUCH_PIXEL_BIAS_X (0)
#define BDM_TOUCH_PIXEL_BIAS_Y (0)
#define BDM_SDL_DEFAULT_FPS 60u
#define BDM_SDL_DEFAULT_STEPS_PER_SECOND 2000000u

typedef struct sdl_audio_state {
    bdm_sound_t *sound;
    SDL_AudioStream *stream;
    unsigned sample_rate;
    uint64_t sample_remainder;
    int capture_enabled;
    int16_t *capture;
    size_t capture_frames;
    size_t capture_capacity;
} sdl_audio_state_t;

typedef struct scripted_tap_event {
    int x;
    int y;
    uint64_t down_steps;
    uint64_t up_steps;
} scripted_tap_event_t;

typedef struct sdl_touch_state {
    int x;
    int y;
    int32_t x_fp;
    int32_t y_fp;
    int physical_down;
    int emulated_down;
    int release_pending;
    uint64_t down_start_steps;
    uint64_t earliest_release_steps;
    uint64_t min_hold_steps;
    int debug;
} sdl_touch_state_t;

typedef struct sdl_options {
    const char *cart_path;
    const char *media_path;
    const char *bios_path;
    const char *load_sram_path;
    const char *save_sram_path;
    const char *load_state_path;
    const char *save_state_path;
    const char *state_slot_path;
    const char *dump_wav_path;
    unsigned scale;
    unsigned fps;
    unsigned steps_per_second;
    unsigned sample_rate;
    unsigned touch_hold_ms;
    unsigned calibration_touch_hold_ms;
    int touch_offset_x;
    int touch_offset_y;
    int fullscreen;
    int integer_scaling;
    int enable_audio;
    int touch_debug;
    int auto_calibrate;
    int auto_title;
    int auto_menu;
    int auto_mode1;
    scripted_tap_event_t post_auto_taps[64];
    size_t post_auto_tap_count;
} sdl_options_t;

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s game_g.bin [game_m.bin] [--bios h8.bin]\n"
        "          [--scale N] [--fullscreen] [--fps N] [--steps-per-second N]\n"
        "          [--sample-rate HZ] [--no-audio] [--dump-wav file.wav]\n"
        "          [--touch-hold-ms N] [--calibration-touch-hold-ms N]\n"
        "          [--touch-offset-x N] [--touch-offset-y N] [--touch-debug]\n"
        "          [--auto-calibrate] [--no-auto-calibrate]\n"
        "          [--load-sram file.bin] [--save-sram file.bin]\n"
        "          [--load-state file.bdmst] [--save-state file.bdmst] [--state file.bdmst]\n"
        "          [--integer-scale] [--aspect-scale]\n"
        "          [--auto-title] [--auto-menu] [--auto-mode1]\n"
        "          [--tap-after-auto X,Y[,DOWN,UP]]\n"
        "\n"
        "SDL3 input:\n"
        "  Mouse/touch: left mouse button on the LCD area. A click is held for\n"
        "               --touch-hold-ms for ADC sampling; default is 20 ms.\n"
        "  A/B:         Z / X\n"
        "  Start:       Return\n"
        "  Select:      Right Shift or Backspace\n"
        "  Reset:       R\n"
        "  Save/load:   F5 / F8, using --state or bdm_state.bdmst\n"
        "  Calibration: auto-assisted by default; pass --no-auto-calibrate to do it manually\n"
        "  Scaling:     F9 toggles integer scale vs aspect-fit letterbox\n"
        "  Fullscreen:  F11 toggles fullscreen\n"
        "  Quit:        Escape or window close\n",
        argv0);
}

static uint64_t parse_u64(const char *s, uint64_t def) {
    if (!s || !*s) return def;
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno || !end || *end) return def;
    return (uint64_t)v;
}

static int parse_script_tap_arg(const char *s, scripted_tap_event_t *tap) {
    if (!s || !tap) return 0;
    char *end = NULL;
    long px = strtol(s, &end, 0);
    if (!end || *end != ',') return 0;
    long py = strtol(end + 1, &end, 0);
    uint64_t down = 1000000ull;
    uint64_t up = 50000000ull;
    if (end && *end == ',') {
        char *tail = NULL;
        down = strtoull(end + 1, &tail, 0);
        if (!tail || *tail != ',') return 0;
        up = strtoull(tail + 1, &tail, 0);
        if (tail && *tail) return 0;
    } else if (end && *end) return 0;
    tap->x = (int)px;
    tap->y = (int)py;
    tap->down_steps = down;
    tap->up_steps = up;
    return 1;
}

static int parse_args(int argc, char **argv, sdl_options_t *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->scale = BDM_SDL_DEFAULT_SCALE;
    opt->fps = BDM_SDL_DEFAULT_FPS;
    opt->steps_per_second = BDM_SDL_DEFAULT_STEPS_PER_SECOND;
    opt->sample_rate = 44100u;
    opt->touch_hold_ms = 20u;
    opt->calibration_touch_hold_ms = 500u;
    opt->touch_offset_x = BDM_TOUCH_PIXEL_BIAS_X;
    opt->touch_offset_y = BDM_TOUCH_PIXEL_BIAS_Y;
    opt->enable_audio = 1;
    opt->auto_calibrate = 1;
    opt->integer_scaling = 1;
    opt->state_slot_path = "bdm_state.bdmst";

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--cart") && i + 1 < argc) opt->cart_path = argv[++i];
        else if (!strcmp(argv[i], "--media") && i + 1 < argc) opt->media_path = argv[++i];
        else if (!strcmp(argv[i], "--bios") && i + 1 < argc) opt->bios_path = argv[++i];
        else if (!strcmp(argv[i], "--load-sram") && i + 1 < argc) opt->load_sram_path = argv[++i];
        else if (!strcmp(argv[i], "--save-sram") && i + 1 < argc) opt->save_sram_path = argv[++i];
        else if ((!strcmp(argv[i], "--load-state") || !strcmp(argv[i], "--load_state")) && i + 1 < argc) opt->load_state_path = argv[++i];
        else if ((!strcmp(argv[i], "--save-state") || !strcmp(argv[i], "--save_state")) && i + 1 < argc) opt->save_state_path = argv[++i];
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) opt->state_slot_path = argv[++i];
        else if (!strcmp(argv[i], "--dump-wav") && i + 1 < argc) opt->dump_wav_path = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) opt->scale = (unsigned)parse_u64(argv[++i], opt->scale);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) opt->fps = (unsigned)parse_u64(argv[++i], opt->fps);
        else if (!strcmp(argv[i], "--steps-per-second") && i + 1 < argc) opt->steps_per_second = (unsigned)parse_u64(argv[++i], opt->steps_per_second);
        else if (!strcmp(argv[i], "--sample-rate") && i + 1 < argc) opt->sample_rate = (unsigned)parse_u64(argv[++i], opt->sample_rate);
        else if (!strcmp(argv[i], "--touch-hold-ms") && i + 1 < argc) opt->touch_hold_ms = (unsigned)parse_u64(argv[++i], opt->touch_hold_ms);
        else if (!strcmp(argv[i], "--calibration-touch-hold-ms") && i + 1 < argc) opt->calibration_touch_hold_ms = (unsigned)parse_u64(argv[++i], opt->calibration_touch_hold_ms);
        else if (!strcmp(argv[i], "--touch-offset-x") && i + 1 < argc) opt->touch_offset_x = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--touch-offset-y") && i + 1 < argc) opt->touch_offset_y = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--touch-debug")) opt->touch_debug = 1;
        else if (!strcmp(argv[i], "--fullscreen")) opt->fullscreen = 1;
        else if (!strcmp(argv[i], "--integer-scale")) opt->integer_scaling = 1;
        else if (!strcmp(argv[i], "--aspect-scale")) opt->integer_scaling = 0;
        else if (!strcmp(argv[i], "--no-audio")) opt->enable_audio = 0;
        else if (!strcmp(argv[i], "--auto-calibrate")) opt->auto_calibrate = 1;
        else if (!strcmp(argv[i], "--no-auto-calibrate")) opt->auto_calibrate = 0;
        else if (!strcmp(argv[i], "--auto-title")) opt->auto_title = 1;
        else if (!strcmp(argv[i], "--auto-menu")) opt->auto_menu = 1;
        else if (!strcmp(argv[i], "--auto-mode1")) { opt->auto_mode1 = 1; opt->auto_menu = 1; }
        else if (!strcmp(argv[i], "--tap-after-auto") && i + 1 < argc) {
            if (opt->post_auto_tap_count >= sizeof(opt->post_auto_taps) / sizeof(opt->post_auto_taps[0]) ||
                !parse_script_tap_arg(argv[++i], &opt->post_auto_taps[opt->post_auto_tap_count])) return 0;
            ++opt->post_auto_tap_count;
        }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(argv[0]); exit(0); }
        else if (argv[i][0] != '-') {
            if (!opt->cart_path) opt->cart_path = argv[i];
            else if (!opt->media_path) opt->media_path = argv[i];
            else return 0;
        }
        else return 0;
    }

    if (!opt->cart_path && !opt->bios_path) return 0;
    if (opt->scale < 1u) opt->scale = 1u;
    if (opt->scale > 12u) opt->scale = 12u;
    if (opt->fps < 10u) opt->fps = 10u;
    if (opt->fps > 240u) opt->fps = 240u;
    if (opt->touch_hold_ms > 5000u) opt->touch_hold_ms = 5000u;
    if (opt->calibration_touch_hold_ms < opt->touch_hold_ms) opt->calibration_touch_hold_ms = opt->touch_hold_ms;
    if (opt->calibration_touch_hold_ms > 5000u) opt->calibration_touch_hold_ms = 5000u;
    if (opt->touch_offset_x < -8) opt->touch_offset_x = -8;
    if (opt->touch_offset_x > 8) opt->touch_offset_x = 8;
    if (opt->touch_offset_y < -8) opt->touch_offset_y = -8;
    if (opt->touch_offset_y > 8) opt->touch_offset_y = 8;
    if (opt->steps_per_second < 1000u) opt->steps_per_second = BDM_SDL_DEFAULT_STEPS_PER_SECOND;
    return 1;
}

static void *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "open failed: %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    void *data = malloc((size_t)sz ? (size_t)sz : 1u);
    if (!data) { fclose(f); return NULL; }
    if (sz && fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "read failed: %s\n", path);
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (out_size) *out_size = (size_t)sz;
    return data;
}

static int write_file_exact(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "open failed: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (size && fwrite(data, 1, size, f) != size) {
        fprintf(stderr, "write failed: %s\n", path);
        fclose(f);
        return -1;
    }
    int err = ferror(f);
    fclose(f);
    return err ? -1 : 0;
}

static int dump_wav_samples(const char *path, const int16_t *samples, size_t frames, unsigned sample_rate) {
    if (!path) return -1;
    if (!samples && frames) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    uint16_t channels = 1;
    uint16_t bits = 16;
    uint32_t data_bytes = (uint32_t)(frames * channels * (bits / 8u));
    uint32_t riff_size = 36u + data_bytes;
    uint32_t byte_rate = sample_rate * channels * (bits / 8u);
    uint16_t block_align = (uint16_t)(channels * (bits / 8u));

#define W8(v) do { unsigned char _b = (unsigned char)(v); fwrite(&_b, 1, 1, f); } while (0)
#define W16(v) do { uint16_t _x = (uint16_t)(v); W8(_x & 0xffu); W8((_x >> 8) & 0xffu); } while (0)
#define W32(v) do { uint32_t _x = (uint32_t)(v); W8(_x & 0xffu); W8((_x >> 8) & 0xffu); W8((_x >> 16) & 0xffu); W8((_x >> 24) & 0xffu); } while (0)

    fwrite("RIFF", 1, 4, f); W32(riff_size); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); W32(16u); W16(1u); W16(channels); W32(sample_rate); W32(byte_rate); W16(block_align); W16(bits);
    fwrite("data", 1, 4, f); W32(data_bytes);
    for (size_t i = 0; i < frames; ++i) W16((uint16_t)samples[i]);

#undef W8
#undef W16
#undef W32

    int err = ferror(f);
    fclose(f);
    return err ? -1 : 0;
}

static int dump_wav(const char *path, const bdm_sound_t *sound) {
    if (!path || !sound) return -1;
    size_t frames = 0;
    const int16_t *samples = bdm_sound_recorded_samples(sound, &frames);
    return dump_wav_samples(path, samples, frames, bdm_sound_sample_rate(sound));
}

static int sdl_audio_capture_append(sdl_audio_state_t *state, const int16_t *samples, size_t frames) {
    if (!state || !state->capture_enabled || !samples || !frames) return 0;
    if (frames > (size_t)-1 - state->capture_frames) return -1;
    size_t need = state->capture_frames + frames;
    if (need > state->capture_capacity) {
        size_t cap = state->capture_capacity ? state->capture_capacity : 65536u;
        while (cap < need) {
            if (cap > ((size_t)-1 / 2u)) return -1;
            cap *= 2u;
        }
        int16_t *p = (int16_t *)realloc(state->capture, cap * sizeof(*p));
        if (!p) return -1;
        state->capture = p;
        state->capture_capacity = cap;
    }
    memcpy(state->capture + state->capture_frames, samples, frames * sizeof(*samples));
    state->capture_frames += frames;
    return 0;
}

static bdm_status_t run_checked(bdm_core_t *core, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) {
        bdm_status_t rc = bdm_core_step(core);
        if (rc != BDM_OK) return rc;
    }
    return BDM_OK;
}

static uint64_t core_steps_now(bdm_core_t *core) {
    bdm_core_state_t st;
    memset(&st, 0, sizeof(st));
    if (core) bdm_core_get_state(core, &st);
    return st.steps;
}

static bdm_status_t scripted_tap(bdm_core_t *core, bdm_input_t *input, int x, int y, uint64_t down_steps, uint64_t up_steps) {
    bdm_input_set_pen(input, x, y, true);
    bdm_status_t rc = run_checked(core, down_steps);
    bdm_input_set_pen(input, x, y, false);
    if (rc != BDM_OK) return rc;
    return run_checked(core, up_steps);
}

static bdm_status_t run_auto_sequence(bdm_core_t *core, bdm_input_t *input,
                                      int want_calibrate, int want_title, int want_menu, int want_mode1) {
    if (!want_calibrate && !want_title && !want_menu && !want_mode1) return BDM_OK;
    bdm_status_t rc = run_checked(core, 5000000ull);
    if (rc != BDM_OK) return rc;
    rc = scripted_tap(core, input, 10, 10, 1000000ull, 20000000ull);
    if (rc != BDM_OK) return rc;
    rc = scripted_tap(core, input, 150, 110, 1000000ull, want_calibrate && !want_title && !want_menu ? 20000000ull : 50000000ull);
    if (rc != BDM_OK || (want_calibrate && !want_title && !want_menu)) return rc;
    rc = scripted_tap(core, input, 48, 106, 1000000ull, 80000000ull);
    if (rc != BDM_OK) return rc;
    rc = scripted_tap(core, input, 48, 106, 1000000ull, 80000000ull);
    if (rc != BDM_OK || (!want_menu && !want_mode1)) return rc;
    rc = scripted_tap(core, input, 48, 106, 1000000ull, 80000000ull);
    if (rc != BDM_OK || !want_mode1) return rc;
    return scripted_tap(core, input, 80, 50, 1000000ull, 100000000ull);
}


static int queue_audio(sdl_audio_state_t *state, unsigned fps) {
    if (!state || !state->stream || !state->sound || state->sample_rate == 0u || fps == 0u) return 0;

    int queued_bytes = SDL_GetAudioStreamQueued(state->stream);
    if (queued_bytes < 0) queued_bytes = 0;
    int target_bytes = (int)((state->sample_rate / 10u) * sizeof(int16_t)); /* about 100 ms */
    if (queued_bytes > target_bytes) return 0;

    state->sample_remainder += state->sample_rate;
    size_t frames = (size_t)(state->sample_remainder / fps);
    state->sample_remainder %= fps;

    size_t extra = (size_t)((target_bytes - queued_bytes) / (int)sizeof(int16_t));
    if (extra > frames) frames = extra;
    if (frames == 0u) frames = 1u;

    int16_t buf[4096];
    while (frames) {
        size_t chunk = frames;
        if (chunk > sizeof(buf) / sizeof(buf[0])) chunk = sizeof(buf) / sizeof(buf[0]);
        bdm_sound_mix_s16(state->sound, buf, chunk, state->sample_rate);
        (void)sdl_audio_capture_append(state, buf, chunk);
        if (!SDL_PutAudioStreamData(state->stream, buf, (int)(chunk * sizeof(buf[0])))) return -1;
        frames -= chunk;
    }
    return 0;
}

static int render_frame(SDL_Renderer *renderer, SDL_Texture *texture, const bdm_video_t *video) {
    size_t w = 0, h = 0;
    const uint32_t *fb = bdm_video_framebuffer(video, &w, &h);
    if (!renderer || !texture || !fb || !w || !h) return -1;
    if (!SDL_UpdateTexture(texture, NULL, fb, (int)(w * sizeof(uint32_t)))) return -1;
    (void)SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    (void)SDL_RenderClear(renderer);
    if (!SDL_RenderTexture(renderer, texture, NULL, NULL)) return -1;
    (void)SDL_RenderPresent(renderer);
    return 0;
}

static int framebuffer_pixel_on(uint32_t p) {
    uint8_t r = (uint8_t)(p >> 16);
    uint8_t g = (uint8_t)(p >> 8);
    uint8_t b = (uint8_t)p;
    return (unsigned)r + (unsigned)g + (unsigned)b < 220u;
}

static int video_has_calibration_target(const bdm_video_t *video) {
    size_t w = 0, h = 0;
    const uint32_t *fb = bdm_video_framebuffer(video, &w, &h);
    if (!fb || w < 160u || h < 120u) return 0;
    unsigned tl = 0, br = 0;
    for (unsigned y = 3; y < 20; ++y)
        for (unsigned x = 3; x < 20; ++x)
            if (framebuffer_pixel_on(fb[y * w + x])) ++tl;
    for (unsigned y = 100; y < 120; ++y)
        for (unsigned x = 140; x < 160; ++x)
            if (framebuffer_pixel_on(fb[y * w + x])) ++br;
    return tl >= 10u || br >= 10u;
}

static uint64_t touch_hold_steps_from_ms(const sdl_options_t *opt, unsigned ms) {
    unsigned rate = opt && opt->steps_per_second ? opt->steps_per_second : 2000000u;
    return ((uint64_t)rate * (uint64_t)ms + 999u) / 1000u;
}

static void touch_prepare_down_for_video(const bdm_video_t *video, sdl_touch_state_t *touch, const sdl_options_t *opt) {
    if (!touch || !opt) return;
    uint64_t normal = touch_hold_steps_from_ms(opt, opt->touch_hold_ms);
    uint64_t calib = touch_hold_steps_from_ms(opt, opt->calibration_touch_hold_ms);
    if (calib < normal) calib = normal;
    touch->min_hold_steps = video_has_calibration_target(video) ? calib : normal;
}

static int32_t float_to_fp(float v) {
    if (v <= -32768.0f) return (int32_t)-2147483647 - 1;
    if (v >= 32767.0f) return 2147418112;
    return (int32_t)(v * 65536.0f);
}

static void logical_to_pen_fp(const bdm_video_t *video, float lx, float ly, int touch_offset_x, int touch_offset_y, int32_t *out_x_fp, int32_t *out_y_fp) {
    int32_t x_fp = float_to_fp(lx);
    int32_t y_fp = float_to_fp(ly);
    if (!video_has_calibration_target(video)) {
        x_fp += (int32_t)touch_offset_x << 16;
        y_fp += (int32_t)touch_offset_y << 16;
    }
    const int32_t max_x = ((int32_t)BDM_LCD_ACTIVE_WIDTH << 16) - 1;
    const int32_t max_y = ((int32_t)BDM_LCD_ACTIVE_HEIGHT << 16) - 1;
    if (x_fp < 0) x_fp = 0;
    if (y_fp < 0) y_fp = 0;
    if (x_fp > max_x) x_fp = max_x;
    if (y_fp > max_y) y_fp = max_y;
    if (out_x_fp) *out_x_fp = x_fp;
    if (out_y_fp) *out_y_fp = y_fp;
}

static void event_position_to_pen_fp(const bdm_video_t *video, SDL_Renderer *renderer, float wx, float wy, unsigned scale, int touch_offset_x, int touch_offset_y, int32_t *out_x_fp, int32_t *out_y_fp) {
    float lx = wx;
    float ly = wy;
    if (!renderer || !SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &lx, &ly)) {
        if (scale == 0u) scale = 1u;
        lx = wx / (float)scale;
        ly = wy / (float)scale;
    }
    logical_to_pen_fp(video, lx, ly, touch_offset_x, touch_offset_y, out_x_fp, out_y_fp);
}

static void touch_apply_down_fp(bdm_input_t *input, sdl_touch_state_t *touch, bdm_core_t *core, int32_t x_fp, int32_t y_fp) {
    if (!input || !touch) return;
    touch->x_fp = x_fp;
    touch->y_fp = y_fp;
    touch->x = (int)(x_fp >> 16);
    touch->y = (int)(y_fp >> 16);
    touch->physical_down = 1;
    touch->release_pending = 0;
    if (!touch->emulated_down) {
        touch->down_start_steps = core_steps_now(core);
        touch->earliest_release_steps = touch->down_start_steps + touch->min_hold_steps;
        touch->emulated_down = 1;
        if (touch->debug) fprintf(stderr, "touch down %.3f,%.3f at step=%" PRIu64 " hold_until=%" PRIu64 "\n",
                                  (double)x_fp / 65536.0, (double)y_fp / 65536.0,
                                  touch->down_start_steps, touch->earliest_release_steps);
    }
    bdm_input_set_pen_fp(input, x_fp, y_fp, true);
}

static void touch_request_up_fp(bdm_input_t *input, sdl_touch_state_t *touch, bdm_core_t *core, int32_t x_fp, int32_t y_fp) {
    (void)x_fp;
    (void)y_fp;
    if (!input || !touch) return;
    touch->physical_down = 0;
    uint64_t now = core_steps_now(core);
    if (!touch->emulated_down) {
        bdm_input_set_pen_fp(input, touch->x_fp, touch->y_fp, false);
        return;
    }
    if (now >= touch->earliest_release_steps) {
        touch->emulated_down = 0;
        touch->release_pending = 0;
        bdm_input_set_pen_fp(input, touch->x_fp, touch->y_fp, false);
        if (touch->debug) fprintf(stderr, "touch up %.3f,%.3f at step=%" PRIu64 "\n",
                                  (double)touch->x_fp / 65536.0, (double)touch->y_fp / 65536.0, now);
    } else {
        touch->release_pending = 1;
        bdm_input_set_pen_fp(input, touch->x_fp, touch->y_fp, true);
        if (touch->debug) fprintf(stderr, "touch up deferred %.3f,%.3f at step=%" PRIu64 " until=%" PRIu64 "\n",
                                  (double)touch->x_fp / 65536.0, (double)touch->y_fp / 65536.0,
                                  now, touch->earliest_release_steps);
    }
}

static void touch_update_motion_fp(bdm_input_t *input, sdl_touch_state_t *touch, int32_t x_fp, int32_t y_fp) {
    if (!input || !touch) return;
    touch->x_fp = x_fp;
    touch->y_fp = y_fp;
    touch->x = (int)(x_fp >> 16);
    touch->y = (int)(y_fp >> 16);
    if (touch->emulated_down) bdm_input_set_pen_fp(input, x_fp, y_fp, true);
}

static void touch_tick_release(bdm_input_t *input, sdl_touch_state_t *touch, bdm_core_t *core) {
    if (!input || !touch || !touch->release_pending || touch->physical_down) return;
    uint64_t now = core_steps_now(core);
    if (now >= touch->earliest_release_steps) {
        touch->emulated_down = 0;
        touch->release_pending = 0;
        bdm_input_set_pen_fp(input, touch->x_fp, touch->y_fp, false);
        if (touch->debug) fprintf(stderr, "touch deferred release %.3f,%.3f at step=%" PRIu64 "\n", (double)touch->x_fp / 65536.0, (double)touch->y_fp / 65536.0, now);
    }
}

static void touch_force_clear(bdm_input_t *input, sdl_touch_state_t *touch) {
    if (!input || !touch) return;
    touch->physical_down = 0;
    touch->emulated_down = 0;
    touch->release_pending = 0;
    bdm_input_set_pen_fp(input, touch->x_fp, touch->y_fp, false);
}


static bdm_status_t soft_reset_with_options(bdm_core_t *core, bdm_input_t *input, sdl_touch_state_t *touch, const sdl_options_t *opt) {
    if (!core || !input) return BDM_ERR_INVALID_ARGUMENT;
    bdm_core_reset(core);
    bdm_input_reset(input);
    if (touch) {
        int debug = touch->debug;
        memset(touch, 0, sizeof(*touch));
        touch->min_hold_steps = touch_hold_steps_from_ms(opt, opt ? opt->touch_hold_ms : 20u);
        touch->debug = debug;
    }
    if (!opt || !opt->auto_calibrate) return BDM_OK;
    return run_auto_sequence(core, input, 1, opt->auto_title, opt->auto_menu, opt->auto_mode1);
}

static void set_key_button(bdm_input_t *input, SDL_Keycode sym, int pressed, int *quit_requested, bdm_core_t *core, sdl_touch_state_t *touch, const sdl_options_t *opt) {
    switch (sym) {
    case SDLK_ESCAPE:
        if (pressed && quit_requested) *quit_requested = 1;
        break;
    case SDLK_Z:
        bdm_input_set_button(input, BDM_BUTTON_A, pressed != 0);
        break;
    case SDLK_X:
        bdm_input_set_button(input, BDM_BUTTON_B, pressed != 0);
        break;
    case SDLK_RETURN:
        bdm_input_set_button(input, BDM_BUTTON_START, pressed != 0);
        break;
    case SDLK_RSHIFT:
    case SDLK_BACKSPACE:
        bdm_input_set_button(input, BDM_BUTTON_SELECT, pressed != 0);
        break;
    case SDLK_R:
        if (pressed && core) {
            bdm_status_t rc = soft_reset_with_options(core, input, touch, opt);
            if (rc == BDM_OK) fprintf(stderr, opt && opt->auto_calibrate ? "reset; auto calibration processed\n" : "reset\n");
            else fprintf(stderr, "reset auto sequence failed: rc=%d\n", rc);
        }
        break;
    default:
        break;
    }
}


static int save_state_file(const char *path, bdm_core_t *core) {
    if (!path || !core) return 0;
    size_t need = bdm_core_state_size(core);
    void *buf = malloc(need ? need : 1u);
    if (!buf) return -1;
    size_t got = bdm_core_save_state(core, buf, need);
    int rc = (got == need) ? write_file_exact(path, buf, need) : -1;
    free(buf);
    return rc;
}

static int load_state_file(const char *path, bdm_core_t *core) {
    if (!path || !core) return 0;
    size_t sz = 0;
    void *buf = read_file(path, &sz);
    if (!buf) return -1;
    bdm_status_t rc = bdm_core_load_state(core, buf, sz);
    free(buf);
    return rc == BDM_OK ? 0 : -1;
}

static void apply_sdl3_scale_mode(SDL_Renderer *renderer, int integer_scaling) {
    if (!renderer) return;
    SDL_RendererLogicalPresentation mode = integer_scaling ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE : SDL_LOGICAL_PRESENTATION_LETTERBOX;
    (void)SDL_SetRenderLogicalPresentation(renderer, (int)BDM_LCD_WIDTH, (int)BDM_LCD_HEIGHT, mode);
}

static int save_sram_if_requested(const char *path, bdm_core_t *core) {
    if (!path || !core) return 0;
    size_t sram_size = bdm_core_external_sram_size(core);
    void *sram = malloc(sram_size ? sram_size : 1u);
    if (!sram) return -1;
    bdm_core_save_sram(core, sram, sram_size);
    int rc = write_file_exact(path, sram, sram_size);
    free(sram);
    return rc;
}

int main(int argc, char **argv) {
    SDL_SetMainReady();

    sdl_options_t opt;
    if (!parse_args(argc, argv, &opt)) {
        usage(argv[0]);
        return 2;
    }

    bdm_video_t *video = bdm_video_create();
    bdm_input_t *input = bdm_input_create();
    bdm_sound_t *sound = bdm_sound_create();
    if (!video || !input || !sound) {
        fprintf(stderr, "allocation failed\n");
        return 1;
    }
    bdm_sound_set_sample_rate(sound, opt.sample_rate);
    bdm_sound_set_step_rate(sound, opt.steps_per_second);
    bdm_sound_enable_recording(sound, opt.dump_wav_path != NULL && !opt.enable_audio);

    bdm_core_config_t cfg;
    cfg.video = video;
    cfg.input = input;
    cfg.sound = sound;
    bdm_core_t *core = bdm_core_create(&cfg);
    if (!core) {
        fprintf(stderr, "core allocation failed\n");
        return 1;
    }

    if (opt.bios_path) {
        size_t sz = 0;
        void *data = read_file(opt.bios_path, &sz);
        if (!data || bdm_core_load_bios(core, data, sz) != BDM_OK) { free(data); fprintf(stderr, "bad BIOS image\n"); return 1; }
        free(data);
    }
    if (opt.cart_path) {
        size_t sz = 0;
        void *data = read_file(opt.cart_path, &sz);
        if (!data || bdm_core_load_cart(core, data, sz) != BDM_OK) { free(data); fprintf(stderr, "bad cart image\n"); return 1; }
        free(data);
    }
    if (opt.media_path) {
        size_t sz = 0;
        void *data = read_file(opt.media_path, &sz);
        if (!data || bdm_core_load_media_cart(core, data, sz) != BDM_OK) { free(data); fprintf(stderr, "bad media cart image\n"); return 1; }
        free(data);
    }

    bdm_core_reset(core);
    if (opt.load_sram_path) {
        size_t sz = 0;
        void *data = read_file(opt.load_sram_path, &sz);
        if (!data || bdm_core_load_sram(core, data, sz) != BDM_OK) { free(data); fprintf(stderr, "bad SRAM image\n"); return 1; }
        free(data);
    }
    if (opt.load_state_path) {
        if (load_state_file(opt.load_state_path, core) != 0) { fprintf(stderr, "state load failed: %s\n", opt.load_state_path); return 1; }
        else printf("loaded %s\n", opt.load_state_path);
    }

    bdm_core_state_t initial_state;
    bdm_core_get_state(core, &initial_state);
    printf("reset pc=%04x vector=%04x\n", initial_state.pc, bdm_core_bus_read16(core, 0));

    int startup_auto_calibrate = opt.auto_calibrate && !opt.load_state_path;
    bdm_status_t auto_rc = run_auto_sequence(core, input, startup_auto_calibrate,
                                             startup_auto_calibrate ? opt.auto_title : 0,
                                             startup_auto_calibrate ? opt.auto_menu : 0,
                                             startup_auto_calibrate ? opt.auto_mode1 : 0);
    if (auto_rc != BDM_OK) {
        bdm_core_state_t st;
        bdm_core_get_state(core, &st);
        fprintf(stderr, "auto sequence failed: rc=%d pc=%04x op=%04x steps=%" PRIu64 "\n", auto_rc, st.pc, st.last_opcode, st.steps);
    }
    for (size_t i = 0; i < opt.post_auto_tap_count; ++i) {
        scripted_tap_event_t *tap = &opt.post_auto_taps[i];
        bdm_status_t tap_rc = scripted_tap(core, input, tap->x, tap->y, tap->down_steps, tap->up_steps);
        if (tap_rc != BDM_OK) {
            fprintf(stderr, "post-auto tap failed: rc=%d\n", tap_rc);
            break;
        }
    }

    SDL_InitFlags init_flags = SDL_INIT_VIDEO;
    if (opt.enable_audio) init_flags |= SDL_INIT_AUDIO;
    if (!SDL_Init(init_flags)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | (opt.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
    if (!SDL_CreateWindowAndRenderer("Bandai Design Master Emulator", (int)(BDM_LCD_WIDTH * opt.scale),
                                     (int)(BDM_LCD_HEIGHT * opt.scale), flags, &window, &renderer)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    int runtime_fullscreen = opt.fullscreen;
    int runtime_integer_scaling = opt.integer_scaling;
    apply_sdl3_scale_mode(renderer, runtime_integer_scaling);

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             (int)BDM_LCD_WIDTH, (int)BDM_LCD_HEIGHT);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    (void)SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    sdl_audio_state_t audio_state;
    memset(&audio_state, 0, sizeof(audio_state));
    audio_state.sound = sound;
    audio_state.sample_rate = opt.sample_rate;
    audio_state.capture_enabled = opt.dump_wav_path != NULL && opt.enable_audio;
    if (opt.enable_audio) {
        SDL_AudioSpec want;
        memset(&want, 0, sizeof(want));
        want.format = SDL_AUDIO_S16;
        want.channels = 1;
        want.freq = (int)opt.sample_rate;
        audio_state.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, NULL, NULL);
        if (!audio_state.stream) {
            fprintf(stderr, "SDL_OpenAudioDeviceStream failed: %s; continuing silent\n", SDL_GetError());
            opt.enable_audio = 0;
            audio_state.capture_enabled = 0;
            if (opt.dump_wav_path) bdm_sound_enable_recording(sound, 1);
        } else {
            bdm_sound_set_sample_rate(sound, opt.sample_rate);
            (void)SDL_ResumeAudioStreamDevice(audio_state.stream);
        }
    }

    const uint64_t pacer_start_ns = SDL_GetTicksNS();
    uint64_t pacer_frame = 0;
    uint64_t step_remainder = 0;
    sdl_touch_state_t touch;
    memset(&touch, 0, sizeof(touch));
    touch.min_hold_steps = touch_hold_steps_from_ms(&opt, opt.touch_hold_ms);
    touch.debug = opt.touch_debug;
    int quit_requested = 0;

    while (!quit_requested) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                quit_requested = 1;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!ev.key.repeat) {
                    if (ev.key.key == SDLK_F11) {
                        runtime_fullscreen = !runtime_fullscreen;
                        if (!SDL_SetWindowFullscreen(window, runtime_fullscreen != 0))
                            fprintf(stderr, "fullscreen toggle failed: %s\n", SDL_GetError());
                    } else if (ev.key.key == SDLK_F9) {
                        runtime_integer_scaling = !runtime_integer_scaling;
                        apply_sdl3_scale_mode(renderer, runtime_integer_scaling);
                        fprintf(stderr, "scale mode: %s\n", runtime_integer_scaling ? "integer" : "aspect");
                    } else if (ev.key.key == SDLK_F5) {
                        if (save_state_file(opt.state_slot_path, core) == 0) fprintf(stderr, "saved state: %s\n", opt.state_slot_path);
                        else fprintf(stderr, "state save failed: %s\n", opt.state_slot_path);
                    } else if (ev.key.key == SDLK_F8) {
                        if (load_state_file(opt.state_slot_path, core) == 0) { touch_force_clear(input, &touch); fprintf(stderr, "loaded state: %s\n", opt.state_slot_path); }
                        else fprintf(stderr, "state load failed: %s\n", opt.state_slot_path);
                    } else if (ev.key.key == SDLK_F10) {
                        opt.auto_calibrate = !opt.auto_calibrate;
                        fprintf(stderr, "auto calibration on reset/load: %s\n", opt.auto_calibrate ? "on" : "off");
                    } else {
                        set_key_button(input, ev.key.key, 1, &quit_requested, core, &touch, &opt);
                    }
                }
                break;
            case SDL_EVENT_KEY_UP:
                set_key_button(input, ev.key.key, 0, &quit_requested, core, &touch, &opt);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    int32_t px = 0, py = 0;
                    event_position_to_pen_fp(video, renderer, ev.button.x, ev.button.y, opt.scale, opt.touch_offset_x, opt.touch_offset_y, &px, &py);
                    touch_prepare_down_for_video(video, &touch, &opt);
                    touch_apply_down_fp(input, &touch, core, px, py);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    int32_t px = 0, py = 0;
                    event_position_to_pen_fp(video, renderer, ev.button.x, ev.button.y, opt.scale, opt.touch_offset_x, opt.touch_offset_y, &px, &py);
                    touch_request_up_fp(input, &touch, core, px, py);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (touch.physical_down || touch.emulated_down) {
                    int32_t px = 0, py = 0;
                    event_position_to_pen_fp(video, renderer, ev.motion.x, ev.motion.y, opt.scale, opt.touch_offset_x, opt.touch_offset_y, &px, &py);
                    touch_update_motion_fp(input, &touch, px, py);
                }
                break;
            default:
                break;
            }
        }

        step_remainder += opt.steps_per_second;
        uint64_t frame_steps = step_remainder / opt.fps;
        step_remainder %= opt.fps;

        bdm_status_t rc = run_checked(core, frame_steps);
        touch_tick_release(input, &touch, core);
        if (rc != BDM_OK) {
            bdm_core_state_t st;
            bdm_core_get_state(core, &st);
            fprintf(stderr, "emulation stopped: rc=%d pc=%04x last_op=%04x steps=%" PRIu64 "\n", rc, st.pc, st.last_opcode, st.steps);
            quit_requested = 1;
        }

        if (opt.enable_audio && queue_audio(&audio_state, opt.fps) != 0) {
            fprintf(stderr, "SDL3 audio queue failed: %s\n", SDL_GetError());
            opt.enable_audio = 0;
        }

        bdm_video_present_headless(video);
        if (render_frame(renderer, texture, video) != 0) {
            fprintf(stderr, "SDL3 render failed: %s\n", SDL_GetError());
            quit_requested = 1;
        }

        ++pacer_frame;
        uint64_t target_ns = pacer_start_ns + (pacer_frame * 1000000000ull) / opt.fps;
        uint64_t now_ns = SDL_GetTicksNS();
        if (target_ns > now_ns) {
            uint64_t wait_ns = target_ns - now_ns;
#if SDL_VERSION_ATLEAST(3,0,0)
            SDL_DelayNS(wait_ns);
#else
            SDL_Delay((Uint32)(wait_ns / 1000000ull));
#endif
        }
    }

    if (audio_state.stream) SDL_DestroyAudioStream(audio_state.stream);
    if (opt.save_state_path) {
        if (save_state_file(opt.save_state_path, core) == 0) printf("wrote %s\n", opt.save_state_path);
        else fprintf(stderr, "state save failed: %s\n", opt.save_state_path);
    }
    if (opt.dump_wav_path) {
        int wav_rc;
        if (audio_state.capture_frames) wav_rc = dump_wav_samples(opt.dump_wav_path, audio_state.capture, audio_state.capture_frames, audio_state.sample_rate);
        else wav_rc = dump_wav(opt.dump_wav_path, sound);
        if (wav_rc != 0) fprintf(stderr, "WAV dump failed: %s\n", opt.dump_wav_path);
        else printf("wrote %s\n", opt.dump_wav_path);
    }
    if (save_sram_if_requested(opt.save_sram_path, core) == 0 && opt.save_sram_path) {
        printf("wrote %s\n", opt.save_sram_path);
    }

    free(audio_state.capture);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    bdm_core_destroy(core);
    bdm_video_destroy(video);
    bdm_input_destroy(input);
    bdm_sound_destroy(sound);
    return 0;
}
