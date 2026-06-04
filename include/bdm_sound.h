#ifndef BDM_SOUND_H
#define BDM_SOUND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bdm_sound bdm_sound_t;

bdm_sound_t *bdm_sound_create(void);
void bdm_sound_destroy(bdm_sound_t *sound);
void bdm_sound_reset(bdm_sound_t *sound);

void bdm_sound_set_sample_rate(bdm_sound_t *sound, unsigned sample_rate);
unsigned bdm_sound_sample_rate(const bdm_sound_t *sound);
void bdm_sound_set_step_rate(bdm_sound_t *sound, unsigned steps_per_second);
unsigned bdm_sound_step_rate(const bdm_sound_t *sound);
void bdm_sound_enable_recording(bdm_sound_t *sound, int enabled);
int bdm_sound_recording_enabled(const bdm_sound_t *sound);

/* Advance the audio clock by an emulated CPU-instruction step count.  The
   current core is instruction-stepped rather than cycle-stepped, so the sound
   backend uses a configurable instruction-step rate. */
void bdm_sound_advance_steps(bdm_sound_t *sound, uint64_t steps);

/* Observe H8 I/O writes.  The Design Master driver in MAME has no sound device
   yet; the only plausible sound writes reached by the DBZ title/menu path are
   H8 8-bit timer registers.  This function keeps that heuristic outside the
   CPU core. */
void bdm_sound_io_write(bdm_sound_t *sound, uint16_t address, uint8_t value, uint64_t step);

size_t bdm_sound_mix_s16(bdm_sound_t *sound, int16_t *out, size_t frames, unsigned sample_rate);
const int16_t *bdm_sound_recorded_samples(const bdm_sound_t *sound, size_t *frames);
size_t bdm_sound_recorded_frames(const bdm_sound_t *sound);
uint64_t bdm_sound_event_count(const bdm_sound_t *sound);
uint8_t bdm_sound_last_timer0_tcsr(const bdm_sound_t *sound);

/* Opaque subsystem snapshot.  Audio recording buffers are deliberately not
   serialized; the mixer/timer/noise phase state is serialized. */
size_t bdm_sound_state_size(const bdm_sound_t *sound);
size_t bdm_sound_save_state(const bdm_sound_t *sound, void *out_data, size_t out_capacity);
int bdm_sound_load_state(bdm_sound_t *sound, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
