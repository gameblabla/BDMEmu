#include "bdm_win32_audio.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef COBJMACROS
#define COBJMACROS 1
#endif
#include <windows.h>
#include <mmsystem.h>
#if defined(BDM_WIN64_FRONTEND)
/*
 * MinGW-w64 does not always provide these Core Audio GUID objects in an
 * import library that is linked by default.  Emit the WASAPI/MMDevice GUIDs
 * in this translation unit so the Win64 build does not depend on fragile
 * library ordering or a particular MinGW package split.
 */
#ifndef INITGUID
#define INITGUID
#endif
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif

#define BDM_AUDIO_BUFFERS 4u
#define BDM_AUDIO_BUFFER_FRAMES 1024u

typedef enum audio_kind { AUDIO_NONE = 0, AUDIO_WAVEOUT, AUDIO_WASAPI } audio_kind_t;

struct bdm_win32_audio {
    bdm_sound_t *sound;
    audio_kind_t kind;
    unsigned sample_rate;
    uint64_t sample_remainder;
    int capture_enabled;
    int16_t *capture;
    size_t capture_frames;
    size_t capture_capacity;
    char backend_name[32];
    HWAVEOUT wave;
    WAVEHDR hdr[BDM_AUDIO_BUFFERS];
    int16_t *wave_buf[BDM_AUDIO_BUFFERS];
#if defined(BDM_WIN64_FRONTEND)
    int com_initialized;
    IMMDevice *device;
    IAudioClient *client;
    IAudioRenderClient *render;
    WAVEFORMATEX *mixfmt;
    UINT32 wasapi_buffer_frames;
    int wasapi_started;
#endif
};

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
    }
    return *a == *b;
}

static int capture_append(bdm_win32_audio_t *a, const int16_t *samples, size_t frames) {
    if (!a || !a->capture_enabled || !samples || !frames) return 0;
    if (frames > (size_t)-1 - a->capture_frames) return -1;
    size_t need = a->capture_frames + frames;
    if (need > a->capture_capacity) {
        size_t cap = a->capture_capacity ? a->capture_capacity : 65536u;
        while (cap < need) {
            if (cap > ((size_t)-1 / 2u)) return -1;
            cap *= 2u;
        }
        int16_t *p = (int16_t *)realloc(a->capture, cap * sizeof(*p));
        if (!p) return -1;
        a->capture = p;
        a->capture_capacity = cap;
    }
    memcpy(a->capture + a->capture_frames, samples, frames * sizeof(*samples));
    a->capture_frames += frames;
    return 0;
}

static void mix_s16(bdm_win32_audio_t *a, int16_t *out, size_t frames) {
    if (!a || !out || !frames) return;
    bdm_sound_mix_s16(a->sound, out, frames, a->sample_rate);
    (void)capture_append(a, out, frames);
}

static int waveout_open(bdm_win32_audio_t *a) {
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = a->sample_rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8u);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    MMRESULT mm = waveOutOpen(&a->wave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
    if (mm != MMSYSERR_NOERROR) return 0;
    for (unsigned i = 0; i < BDM_AUDIO_BUFFERS; ++i) {
        a->wave_buf[i] = (int16_t *)calloc(BDM_AUDIO_BUFFER_FRAMES, sizeof(int16_t));
        if (!a->wave_buf[i]) return 0;
        memset(&a->hdr[i], 0, sizeof(a->hdr[i]));
        a->hdr[i].lpData = (LPSTR)a->wave_buf[i];
        a->hdr[i].dwBufferLength = (DWORD)(BDM_AUDIO_BUFFER_FRAMES * sizeof(int16_t));
    }
    a->kind = AUDIO_WAVEOUT;
    strcpy(a->backend_name, "waveout");
    return 1;
}

static int waveout_pump(bdm_win32_audio_t *a) {
    int wrote = 0;
    for (unsigned i = 0; i < BDM_AUDIO_BUFFERS; ++i) {
        WAVEHDR *h = &a->hdr[i];
        if ((h->dwFlags & WHDR_PREPARED) && !(h->dwFlags & WHDR_DONE)) continue;
        if (h->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(a->wave, h, sizeof(*h));
        memset(h, 0, sizeof(*h));
        h->lpData = (LPSTR)a->wave_buf[i];
        h->dwBufferLength = (DWORD)(BDM_AUDIO_BUFFER_FRAMES * sizeof(int16_t));
        mix_s16(a, a->wave_buf[i], BDM_AUDIO_BUFFER_FRAMES);
        if (waveOutPrepareHeader(a->wave, h, sizeof(*h)) != MMSYSERR_NOERROR) return -1;
        if (waveOutWrite(a->wave, h, sizeof(*h)) != MMSYSERR_NOERROR) return -1;
        ++wrote;
        if (wrote >= 2) break;
    }
    return 0;
}

#if defined(BDM_WIN64_FRONTEND)
static int wasapi_open(bdm_win32_audio_t *a) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) a->com_initialized = 1;
    else if (hr != RPC_E_CHANGED_MODE) return 0;

    IMMDeviceEnumerator *enumerator = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (FAILED(hr) || !enumerator) return 0;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &a->device);
    IMMDeviceEnumerator_Release(enumerator);
    if (FAILED(hr) || !a->device) return 0;
    hr = IMMDevice_Activate(a->device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&a->client);
    if (FAILED(hr) || !a->client) return 0;
    hr = IAudioClient_GetMixFormat(a->client, &a->mixfmt);
    if (FAILED(hr) || !a->mixfmt) return 0;
    REFERENCE_TIME dur = 10000000; /* 1 second device buffer; pump keeps it partially filled. */
    hr = IAudioClient_Initialize(a->client, AUDCLNT_SHAREMODE_SHARED, 0, dur, 0, a->mixfmt, NULL);
    if (FAILED(hr)) return 0;
    hr = IAudioClient_GetBufferSize(a->client, &a->wasapi_buffer_frames);
    if (FAILED(hr)) return 0;
    hr = IAudioClient_GetService(a->client, &IID_IAudioRenderClient, (void **)&a->render);
    if (FAILED(hr) || !a->render) return 0;
    hr = IAudioClient_Start(a->client);
    if (FAILED(hr)) return 0;
    a->wasapi_started = 1;
    a->kind = AUDIO_WASAPI;
    strcpy(a->backend_name, "wasapi");
    return 1;
}

static int is_extensible_subtype(const WAVEFORMATEX *fmt, const GUID *sub) {
    if (!fmt || fmt->wFormatTag != WAVE_FORMAT_EXTENSIBLE || fmt->cbSize < 22) return 0;
    const WAVEFORMATEXTENSIBLE *ex = (const WAVEFORMATEXTENSIBLE *)fmt;
    return IsEqualGUID(&ex->SubFormat, sub) != 0;
}

static void fill_wasapi_buffer(bdm_win32_audio_t *a, BYTE *dst, UINT32 frames) {
    if (!dst || !a || !a->mixfmt || !frames) return;
    WAVEFORMATEX *fmt = a->mixfmt;
    int channels = fmt->nChannels ? (int)fmt->nChannels : 2;
    int is_float = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || is_extensible_subtype(fmt, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    int is_pcm = fmt->wFormatTag == WAVE_FORMAT_PCM || is_extensible_subtype(fmt, &KSDATAFORMAT_SUBTYPE_PCM);
    int bits = fmt->wBitsPerSample;
    int16_t tmp[2048];
    UINT32 done = 0;
    while (done < frames) {
        UINT32 chunk = frames - done;
        if (chunk > (UINT32)(sizeof(tmp) / sizeof(tmp[0]))) chunk = (UINT32)(sizeof(tmp) / sizeof(tmp[0]));
        mix_s16(a, tmp, chunk);
        BYTE *base = dst + (size_t)done * fmt->nBlockAlign;
        if (is_float && bits == 32) {
            float *out = (float *)base;
            for (UINT32 i = 0; i < chunk; ++i) {
                float s = (float)tmp[i] / 32768.0f;
                for (int c = 0; c < channels; ++c) *out++ = s;
            }
        } else if (is_pcm && bits == 16) {
            int16_t *out = (int16_t *)base;
            for (UINT32 i = 0; i < chunk; ++i)
                for (int c = 0; c < channels; ++c) *out++ = tmp[i];
        } else {
            memset(base, 0, (size_t)chunk * fmt->nBlockAlign);
        }
        done += chunk;
    }
}

static int wasapi_pump(bdm_win32_audio_t *a) {
    if (!a || !a->client || !a->render) return -1;
    UINT32 padding = 0;
    HRESULT hr = IAudioClient_GetCurrentPadding(a->client, &padding);
    if (FAILED(hr)) return -1;
    UINT32 avail = a->wasapi_buffer_frames > padding ? (a->wasapi_buffer_frames - padding) : 0;
    if (avail < 256u) return 0;
    if (avail > 4096u) avail = 4096u;
    BYTE *dst = NULL;
    hr = IAudioRenderClient_GetBuffer(a->render, avail, &dst);
    if (FAILED(hr) || !dst) return -1;
    fill_wasapi_buffer(a, dst, avail);
    hr = IAudioRenderClient_ReleaseBuffer(a->render, avail, 0);
    return FAILED(hr) ? -1 : 0;
}
#else
static int wasapi_open(bdm_win32_audio_t *a) { (void)a; return 0; }
static int wasapi_pump(bdm_win32_audio_t *a) { (void)a; return -1; }
#endif

bdm_win32_audio_t *bdm_win32_audio_create(bdm_sound_t *sound, unsigned sample_rate, const char *backend, int capture_enabled) {
    bdm_win32_audio_t *a = (bdm_win32_audio_t *)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->sound = sound;
    a->sample_rate = sample_rate ? sample_rate : 44100u;
    a->capture_enabled = capture_enabled;
    strcpy(a->backend_name, "none");
    if (backend && str_eq(backend, "none")) return a;
#if defined(BDM_WIN64_FRONTEND)
    if (!backend || str_eq(backend, "wasapi")) {
        if (wasapi_open(a)) return a;
        fprintf(stderr, "WASAPI init failed; falling back to waveOut\n");
    }
#endif
    if (waveout_open(a)) return a;
    fprintf(stderr, "waveOut init failed; continuing silent\n");
    a->kind = AUDIO_NONE;
    strcpy(a->backend_name, "none");
    return a;
}

void bdm_win32_audio_destroy(bdm_win32_audio_t *a) {
    if (!a) return;
#if defined(BDM_WIN64_FRONTEND)
    if (a->client && a->wasapi_started) IAudioClient_Stop(a->client);
    if (a->render) IAudioRenderClient_Release(a->render);
    if (a->client) IAudioClient_Release(a->client);
    if (a->device) IMMDevice_Release(a->device);
    if (a->mixfmt) CoTaskMemFree(a->mixfmt);
    if (a->com_initialized) CoUninitialize();
#endif
    if (a->wave) {
        waveOutReset(a->wave);
        for (unsigned i = 0; i < BDM_AUDIO_BUFFERS; ++i) {
            if (a->hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(a->wave, &a->hdr[i], sizeof(a->hdr[i]));
        }
        waveOutClose(a->wave);
    }
    for (unsigned i = 0; i < BDM_AUDIO_BUFFERS; ++i) free(a->wave_buf[i]);
    free(a->capture);
    free(a);
}

int bdm_win32_audio_pump(bdm_win32_audio_t *a, unsigned fps) {
    (void)fps;
    if (!a || a->kind == AUDIO_NONE) return 0;
    if (a->kind == AUDIO_WAVEOUT) return waveout_pump(a);
    if (a->kind == AUDIO_WASAPI) return wasapi_pump(a);
    return 0;
}

const int16_t *bdm_win32_audio_capture(const bdm_win32_audio_t *a, size_t *frames) {
    if (frames) *frames = a ? a->capture_frames : 0;
    return a ? a->capture : NULL;
}

const char *bdm_win32_audio_active_backend(const bdm_win32_audio_t *a) { return a ? a->backend_name : "none"; }
unsigned bdm_win32_audio_sample_rate(const bdm_win32_audio_t *a) { return a ? a->sample_rate : 0u; }
