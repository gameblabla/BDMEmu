#ifndef BDM_WIN32_AUDIO_H
#define BDM_WIN32_AUDIO_H

#include "bdm_sound.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bdm_win32_audio bdm_win32_audio_t;

bdm_win32_audio_t *bdm_win32_audio_create(bdm_sound_t *sound, unsigned sample_rate, const char *backend, int capture_enabled);
void bdm_win32_audio_destroy(bdm_win32_audio_t *audio);
int bdm_win32_audio_pump(bdm_win32_audio_t *audio, unsigned fps);
const int16_t *bdm_win32_audio_capture(const bdm_win32_audio_t *audio, size_t *frames);
const char *bdm_win32_audio_active_backend(const bdm_win32_audio_t *audio);
unsigned bdm_win32_audio_sample_rate(const bdm_win32_audio_t *audio);

#ifdef __cplusplus
}
#endif

#endif
