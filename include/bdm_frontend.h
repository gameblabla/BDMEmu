#ifndef BDM_FRONTEND_H
#define BDM_FRONTEND_H

#include "bdm_core.h"
#include "bdm_input.h"
#include "bdm_sound.h"
#include "bdm_video.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BDM_FE_DEFAULT_SCALE 3u
#define BDM_FE_DEFAULT_FPS 60u
#define BDM_FE_DEFAULT_STEPS_PER_SECOND 2000000u
#define BDM_FE_DEFAULT_SAMPLE_RATE 44100u
#define BDM_FE_DEFAULT_TOUCH_HOLD_MS 20u
#define BDM_FE_DEFAULT_CALIBRATION_TOUCH_HOLD_MS 500u

typedef struct bdm_fe_scripted_tap {
    int x;
    int y;
    uint64_t down_steps;
    uint64_t up_steps;
} bdm_fe_scripted_tap_t;

typedef struct bdm_fe_options {
    const char *cart_path;
    const char *media_path;
    const char *bios_path;
    const char *load_sram_path;
    const char *save_sram_path;
    const char *load_state_path;
    const char *save_state_path;
    const char *state_slot_path;
    const char *dump_wav_path;
    const char *video_backend;
    const char *audio_backend;
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
    bdm_fe_scripted_tap_t post_auto_taps[64];
    size_t post_auto_tap_count;
} bdm_fe_options_t;

typedef struct bdm_fe_machine {
    bdm_video_t *video;
    bdm_input_t *input;
    bdm_sound_t *sound;
    bdm_core_t *core;
} bdm_fe_machine_t;

typedef struct bdm_fe_touch_state {
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
} bdm_fe_touch_state_t;

void bdm_fe_options_init(bdm_fe_options_t *opt);
int bdm_fe_parse_args(int argc, char **argv, bdm_fe_options_t *opt, int allow_backend_options);
void bdm_fe_print_usage(FILE *f, const char *argv0, const char *frontend_name, int show_backend_options);

int bdm_fe_machine_init(bdm_fe_machine_t *machine, const bdm_fe_options_t *opt);
void bdm_fe_machine_destroy(bdm_fe_machine_t *machine);

void *bdm_fe_read_file(const char *path, size_t *out_size);
int bdm_fe_write_file_exact(const char *path, const void *data, size_t size);
int bdm_fe_save_state_file(const char *path, bdm_core_t *core);
int bdm_fe_load_state_file(const char *path, bdm_core_t *core);
int bdm_fe_save_sram_if_requested(const char *path, bdm_core_t *core);
int bdm_fe_dump_wav_samples(const char *path, const int16_t *samples, size_t frames, unsigned sample_rate);
int bdm_fe_dump_wav(const char *path, const bdm_sound_t *sound);

bdm_status_t bdm_fe_run_checked(bdm_core_t *core, uint64_t count);
uint64_t bdm_fe_ms_to_steps(unsigned steps_per_second, unsigned ms);
bdm_status_t bdm_fe_scripted_tap(bdm_core_t *core, bdm_input_t *input, int x, int y, uint64_t down_steps, uint64_t up_steps);
bdm_status_t bdm_fe_run_auto_sequence(bdm_core_t *core, bdm_input_t *input, int want_calibrate, int want_title, int want_menu, int want_mode1);
bdm_status_t bdm_fe_soft_reset(bdm_core_t *core, bdm_input_t *input, bdm_fe_touch_state_t *touch, const bdm_fe_options_t *opt, int allow_extended_auto);
uint64_t bdm_fe_core_steps_now(bdm_core_t *core);

int bdm_fe_framebuffer_pixel_on(uint32_t p);
int bdm_fe_video_has_calibration_target(const bdm_video_t *video);
void bdm_fe_touch_prepare_down_for_video(bdm_fe_touch_state_t *touch, const bdm_video_t *video, uint64_t normal_hold_steps, uint64_t calibration_hold_steps);
void bdm_fe_logical_to_pen(const bdm_video_t *video, float lx, float ly, int touch_offset_x, int touch_offset_y, int *out_x, int *out_y);
void bdm_fe_logical_to_pen_fp(const bdm_video_t *video, float lx, float ly, int touch_offset_x, int touch_offset_y, int32_t *out_x_fp, int32_t *out_y_fp);
void bdm_fe_touch_apply_down(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core, int x, int y);
void bdm_fe_touch_apply_down_fp(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core, int32_t x_fp, int32_t y_fp);
void bdm_fe_touch_request_up(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core, int x, int y);
void bdm_fe_touch_request_up_fp(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core, int32_t x_fp, int32_t y_fp);
void bdm_fe_touch_update_motion(bdm_input_t *input, bdm_fe_touch_state_t *touch, int x, int y);
void bdm_fe_touch_update_motion_fp(bdm_input_t *input, bdm_fe_touch_state_t *touch, int32_t x_fp, int32_t y_fp);
void bdm_fe_touch_tick_release(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core);
void bdm_fe_touch_force_clear(bdm_input_t *input, bdm_fe_touch_state_t *touch);
int bdm_fe_panel_button_to_pen_fp(bdm_button_t button, int32_t *out_x_fp, int32_t *out_y_fp);
void bdm_fe_set_panel_button(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core, bdm_button_t button, int pressed);
void bdm_fe_set_button_key_ascii(bdm_input_t *input, unsigned key, int pressed, int *quit_requested, bdm_core_t *core, bdm_fe_touch_state_t *touch);

#ifdef __cplusplus
}
#endif

#endif
