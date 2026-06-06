#include "bdm_core.h"
#include "bdm_input.h"
#include "bdm_sound.h"
#include "bdm_video.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define BDM_WASM_STATUS_EMPTY   0u
#define BDM_WASM_STATUS_READY   1u
#define BDM_WASM_STATUS_RUNNING 3u
#define BDM_WASM_STATUS_ERROR   5u

#define BDM_WASM_ERR_NONE       0u
#define BDM_WASM_ERR_ALLOC      1u
#define BDM_WASM_ERR_BAD_CART   2u
#define BDM_WASM_ERR_BAD_MEDIA  3u
#define BDM_WASM_ERR_BAD_STATE  4u
#define BDM_WASM_ERR_CPU        5u

#define BDM_WASM_AUDIO_MAX_FRAMES 8192u

extern void __pcfx_wasm_heap_reset(void);
extern uint32_t __pcfx_wasm_heap_used(void);

static bdm_video_t *g_video;
static bdm_input_t *g_input;
static bdm_sound_t *g_sound;
static bdm_core_t *g_core;
static uint32_t g_status = BDM_WASM_STATUS_EMPTY;
static uint32_t g_error = BDM_WASM_ERR_NONE;
static uint32_t g_frame_count;
static unsigned g_sample_rate = 44100u;
static unsigned g_steps_per_second = 2000000u;
static uint64_t g_audio_remainder;
static int16_t g_audio[BDM_WASM_AUDIO_MAX_FRAMES];
static uint32_t g_audio_frames;
static uint8_t *g_save;
static uint32_t g_save_size;
static uint32_t g_save_capacity;

static void destroy_machine(void)
{
    if (g_core) { bdm_core_destroy(g_core); g_core = NULL; }
    if (g_video) { bdm_video_destroy(g_video); g_video = NULL; }
    if (g_input) { bdm_input_destroy(g_input); g_input = NULL; }
    if (g_sound) { bdm_sound_destroy(g_sound); g_sound = NULL; }
}

static int ensure_machine(void)
{
    if (g_core) return 1;
    g_video = bdm_video_create();
    g_input = bdm_input_create();
    g_sound = bdm_sound_create();
    if (!g_video || !g_input || !g_sound) {
        destroy_machine();
        g_error = BDM_WASM_ERR_ALLOC;
        g_status = BDM_WASM_STATUS_ERROR;
        return 0;
    }
    bdm_sound_set_sample_rate(g_sound, g_sample_rate);
    bdm_sound_set_step_rate(g_sound, g_steps_per_second);
    bdm_sound_enable_recording(g_sound, 0);
    bdm_core_config_t cfg;
    cfg.video = g_video;
    cfg.input = g_input;
    cfg.sound = g_sound;
    g_core = bdm_core_create(&cfg);
    if (!g_core) {
        destroy_machine();
        g_error = BDM_WASM_ERR_ALLOC;
        g_status = BDM_WASM_STATUS_ERROR;
        return 0;
    }
    g_status = BDM_WASM_STATUS_READY;
    g_error = BDM_WASM_ERR_NONE;
    return 1;
}

static int wasm_panel_mask_to_pen_fp(uint32_t mask, int32_t *out_x_fp, int32_t *out_y_fp)
{
    int x = 0, y = 0;
    if (mask & 0x01u) { x = 16;  y = 4;  }
    else if (mask & 0x02u) { x = 48;  y = 4;  }
    else if (mask & 0x04u) { x = 80;  y = 4;  }
    else if (mask & 0x08u) { x = 112; y = 4;  }
    else if (mask & 0x10u) { x = 144; y = 4;  }
    else if (mask & 0x20u) { x = 3;   y = 60; }
    else if (mask & 0x40u) { x = 156; y = 60; }
    else return 0;
    if (out_x_fp) *out_x_fp = ((int32_t)x << 16) | 0x8000;
    if (out_y_fp) *out_y_fp = ((int32_t)y << 16) | 0x8000;
    return 1;
}

static void apply_buttons_fp(uint32_t mask, int32_t pen_x_fp, int32_t pen_y_fp, uint32_t pen_down)
{
    if (!g_input) return;
    bdm_input_set_button(g_input, BDM_BUTTON_MENU_A,     (mask & 0x01u) != 0u);
    bdm_input_set_button(g_input, BDM_BUTTON_MENU_B,     (mask & 0x02u) != 0u);
    bdm_input_set_button(g_input, BDM_BUTTON_MENU_C,     (mask & 0x04u) != 0u);
    bdm_input_set_button(g_input, BDM_BUTTON_MENU_D,     (mask & 0x08u) != 0u);
    bdm_input_set_button(g_input, BDM_BUTTON_MENU_E,     (mask & 0x10u) != 0u);
    bdm_input_set_button(g_input, BDM_BUTTON_PAGE_LEFT,  (mask & 0x20u) != 0u);
    bdm_input_set_button(g_input, BDM_BUTTON_PAGE_RIGHT, (mask & 0x40u) != 0u);

    /* Match the native frontends: browser pointer positions are resistive-panel
       analog coordinates, so keep their 16.16 fraction all the way into the ADC
       model instead of truncating to an LCD pixel.  If only a hardware-panel
       button is held, also present the matching physical panel position like the
       Win32/Win64 input path does. */
    if (pen_down) {
        bdm_input_set_pen_fp(g_input, pen_x_fp, pen_y_fp, true);
    } else {
        int32_t panel_x_fp = 0, panel_y_fp = 0;
        if (wasm_panel_mask_to_pen_fp(mask, &panel_x_fp, &panel_y_fp))
            bdm_input_set_pen_fp(g_input, panel_x_fp, panel_y_fp, true);
        else
            bdm_input_set_pen_fp(g_input, pen_x_fp, pen_y_fp, false);
    }
}

static void apply_buttons(uint32_t mask, int pen_x, int pen_y, uint32_t pen_down)
{
    apply_buttons_fp(mask, (int32_t)pen_x << 16, (int32_t)pen_y << 16, pen_down);
}

__attribute__((export_name("bdm_wasm_version")))
uint32_t bdm_wasm_version(void) { return 0x00010000u; }

__attribute__((export_name("bdm_wasm_malloc")))
uint32_t bdm_wasm_malloc(uint32_t size) { return (uint32_t)(uintptr_t)malloc(size ? size : 1u); }

__attribute__((export_name("bdm_wasm_heap_used")))
uint32_t bdm_wasm_heap_used(void) { return __pcfx_wasm_heap_used(); }

__attribute__((export_name("bdm_wasm_reset_heap")))
void bdm_wasm_reset_heap(void)
{
    destroy_machine();
    __pcfx_wasm_heap_reset();
    g_save = NULL;
    g_save_size = 0;
    g_save_capacity = 0;
    g_audio_frames = 0;
    g_audio_remainder = 0;
    g_frame_count = 0;
    g_status = BDM_WASM_STATUS_EMPTY;
    g_error = BDM_WASM_ERR_NONE;
}

__attribute__((export_name("bdm_wasm_init")))
uint32_t bdm_wasm_init(uint32_t sample_rate, uint32_t steps_per_second)
{
    destroy_machine();
    g_sample_rate = sample_rate >= 8000u && sample_rate <= 192000u ? sample_rate : 44100u;
    g_steps_per_second = steps_per_second >= 1000u ? steps_per_second : 2000000u;
    g_audio_frames = 0;
    g_audio_remainder = 0;
    g_frame_count = 0;
    return ensure_machine() ? 1u : 0u;
}

__attribute__((export_name("bdm_wasm_load_cart")))
uint32_t bdm_wasm_load_cart(uint32_t data_ptr, uint32_t size)
{
    if (!ensure_machine() || !data_ptr || !size) return 0u;
    bdm_status_t st = bdm_core_load_cart(g_core, (const void *)(uintptr_t)data_ptr, size);
    if (st != BDM_OK) { g_status = BDM_WASM_STATUS_ERROR; g_error = BDM_WASM_ERR_BAD_CART; return 0u; }
    g_status = BDM_WASM_STATUS_READY;
    g_error = BDM_WASM_ERR_NONE;
    return 1u;
}

__attribute__((export_name("bdm_wasm_load_media")))
uint32_t bdm_wasm_load_media(uint32_t data_ptr, uint32_t size)
{
    if (!ensure_machine() || !data_ptr || !size) return 0u;
    bdm_status_t st = bdm_core_load_media_cart(g_core, (const void *)(uintptr_t)data_ptr, size);
    if (st != BDM_OK) { g_status = BDM_WASM_STATUS_ERROR; g_error = BDM_WASM_ERR_BAD_MEDIA; return 0u; }
    g_error = BDM_WASM_ERR_NONE;
    return 1u;
}

__attribute__((export_name("bdm_wasm_start")))
uint32_t bdm_wasm_start(void)
{
    if (!ensure_machine()) return 0u;
    bdm_core_reset(g_core);
    g_frame_count = 0;
    g_audio_frames = 0;
    g_audio_remainder = 0;
    g_status = BDM_WASM_STATUS_RUNNING;
    g_error = BDM_WASM_ERR_NONE;
    return 1u;
}

__attribute__((export_name("bdm_wasm_soft_reset")))
uint32_t bdm_wasm_soft_reset(void) { return bdm_wasm_start(); }

__attribute__((export_name("bdm_wasm_frame_fp")))
uint32_t bdm_wasm_frame_fp(uint32_t steps, uint32_t button_mask, int32_t pen_x_fp, int32_t pen_y_fp, uint32_t pen_down)
{
    if (!g_core || g_status != BDM_WASM_STATUS_RUNNING) return 0u;
    apply_buttons_fp(button_mask, pen_x_fp, pen_y_fp, pen_down);
    if (!steps) steps = g_steps_per_second / 60u;
    uint64_t ran = bdm_core_run_steps(g_core, steps, true);
    (void)ran;
    bdm_video_present_headless(g_video);

    uint64_t scaled = (uint64_t)steps * (uint64_t)g_sample_rate + g_audio_remainder;
    uint32_t frames = (uint32_t)(scaled / g_steps_per_second);
    g_audio_remainder = scaled % g_steps_per_second;
    if (frames > BDM_WASM_AUDIO_MAX_FRAMES) frames = BDM_WASM_AUDIO_MAX_FRAMES;
    if (frames) bdm_sound_mix_s16(g_sound, g_audio, frames, g_sample_rate);
    g_audio_frames = frames;

    bdm_core_state_t st;
    bdm_core_get_state(g_core, &st);
    if (st.unsupported) { g_status = BDM_WASM_STATUS_ERROR; g_error = BDM_WASM_ERR_CPU; }
    ++g_frame_count;
    return g_status == BDM_WASM_STATUS_RUNNING ? 1u : 0u;
}

__attribute__((export_name("bdm_wasm_frame")))
uint32_t bdm_wasm_frame(uint32_t steps, uint32_t button_mask, int32_t pen_x, int32_t pen_y, uint32_t pen_down)
{
    return bdm_wasm_frame_fp(steps, button_mask, (int32_t)pen_x << 16, (int32_t)pen_y << 16, pen_down);
}

__attribute__((export_name("bdm_wasm_set_input_fp")))
void bdm_wasm_set_input_fp(uint32_t button_mask, int32_t pen_x_fp, int32_t pen_y_fp, uint32_t pen_down)
{
    apply_buttons_fp(button_mask, pen_x_fp, pen_y_fp, pen_down);
}

__attribute__((export_name("bdm_wasm_set_input")))
void bdm_wasm_set_input(uint32_t button_mask, int32_t pen_x, int32_t pen_y, uint32_t pen_down)
{
    apply_buttons(button_mask, (int)pen_x, (int)pen_y, pen_down);
}

__attribute__((export_name("bdm_wasm_get_steps_lo")))
uint32_t bdm_wasm_get_steps_lo(void)
{
    bdm_core_state_t st;
    memset(&st, 0, sizeof(st));
    if (g_core) bdm_core_get_state(g_core, &st);
    return (uint32_t)st.steps;
}

__attribute__((export_name("bdm_wasm_get_steps_hi")))
uint32_t bdm_wasm_get_steps_hi(void)
{
    bdm_core_state_t st;
    memset(&st, 0, sizeof(st));
    if (g_core) bdm_core_get_state(g_core, &st);
    return (uint32_t)(st.steps >> 32);
}

__attribute__((export_name("bdm_wasm_get_status")))
uint32_t bdm_wasm_get_status(void) { return g_status; }

__attribute__((export_name("bdm_wasm_get_error")))
uint32_t bdm_wasm_get_error(void) { return g_error; }

__attribute__((export_name("bdm_wasm_get_frame_count")))
uint32_t bdm_wasm_get_frame_count(void) { return g_frame_count; }

__attribute__((export_name("bdm_wasm_get_width")))
uint32_t bdm_wasm_get_width(void) { return BDM_LCD_WIDTH; }

__attribute__((export_name("bdm_wasm_get_height")))
uint32_t bdm_wasm_get_height(void) { return BDM_LCD_HEIGHT; }

__attribute__((export_name("bdm_wasm_get_pitch_pixels")))
uint32_t bdm_wasm_get_pitch_pixels(void) { return BDM_LCD_WIDTH; }

__attribute__((export_name("bdm_wasm_get_pixel_format")))
uint32_t bdm_wasm_get_pixel_format(void) { return 1u; /* ARGB8888 */ }

__attribute__((export_name("bdm_wasm_get_bytes_per_pixel")))
uint32_t bdm_wasm_get_bytes_per_pixel(void) { return 4u; }

__attribute__((export_name("bdm_wasm_get_framebuffer")))
uint32_t bdm_wasm_get_framebuffer(void)
{
    size_t w = 0, h = 0;
    const uint32_t *fb = g_video ? bdm_video_framebuffer(g_video, &w, &h) : NULL;
    return (uint32_t)(uintptr_t)fb;
}

__attribute__((export_name("bdm_wasm_get_audio_rate")))
uint32_t bdm_wasm_get_audio_rate(void) { return g_sample_rate; }

__attribute__((export_name("bdm_wasm_get_audio_ptr")))
uint32_t bdm_wasm_get_audio_ptr(void) { return (uint32_t)(uintptr_t)g_audio; }

__attribute__((export_name("bdm_wasm_get_audio_frames")))
uint32_t bdm_wasm_get_audio_frames(void) { return g_audio_frames; }

__attribute__((export_name("bdm_wasm_audio_consume")))
void bdm_wasm_audio_consume(uint32_t frames) { if (frames >= g_audio_frames) g_audio_frames = 0; }

__attribute__((export_name("bdm_wasm_save_state")))
uint32_t bdm_wasm_save_state(void)
{
    if (!g_core) return 0u;
    size_t need = bdm_core_state_size(g_core);
    if (need > g_save_capacity) {
        uint8_t *p = (uint8_t *)realloc(g_save, need ? need : 1u);
        if (!p) { g_error = BDM_WASM_ERR_ALLOC; return 0u; }
        g_save = p;
        g_save_capacity = (uint32_t)need;
    }
    size_t got = bdm_core_save_state(g_core, g_save, need);
    if (got != need) { g_save_size = 0; g_error = BDM_WASM_ERR_BAD_STATE; return 0u; }
    g_save_size = (uint32_t)got;
    g_error = BDM_WASM_ERR_NONE;
    return 1u;
}

__attribute__((export_name("bdm_wasm_get_save_ptr")))
uint32_t bdm_wasm_get_save_ptr(void) { return (uint32_t)(uintptr_t)g_save; }

__attribute__((export_name("bdm_wasm_get_save_size")))
uint32_t bdm_wasm_get_save_size(void) { return g_save_size; }

__attribute__((export_name("bdm_wasm_load_state")))
uint32_t bdm_wasm_load_state(uint32_t data_ptr, uint32_t size)
{
    if (!g_core || !data_ptr || !size) return 0u;
    bdm_status_t st = bdm_core_load_state(g_core, (const void *)(uintptr_t)data_ptr, size);
    if (st != BDM_OK) { g_error = BDM_WASM_ERR_BAD_STATE; return 0u; }
    g_status = BDM_WASM_STATUS_RUNNING;
    g_error = BDM_WASM_ERR_NONE;
    return 1u;
}
