#include "bdm_video.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define LCD_PIXEL_OFF 0xffc6d1bdu
#define LCD_PIXEL_ON  0xff1c2a20u
struct bdm_video {
    uint32_t pixels[BDM_LCD_WIDTH * BDM_LCD_HEIGHT];
    uint8_t lcd_vram[BDM_LCD_VRAM_SIZE];
    uint8_t lcd_regs[256];
    uint8_t lcd_index;
    size_t active_width;
    size_t active_height;
    size_t stride_bytes;
    uint64_t frames;
    uint64_t lcd_dirty;
};

static void lcd_clear_pixels(bdm_video_t *v) {
    for (size_t y = 0; y < BDM_LCD_HEIGHT; ++y) {
        for (size_t x = 0; x < BDM_LCD_WIDTH; ++x) {
            v->pixels[y * BDM_LCD_WIDTH + x] = LCD_PIXEL_OFF;
        }
    }
}

static void lcd_redecode_geometry(bdm_video_t *v) {
    size_t stride = v->lcd_regs[0x12] ? v->lcd_regs[0x12] : v->lcd_regs[0x01];
    if (!stride) stride = BDM_LCD_STRIDE_BYTES;
    if (stride > BDM_LCD_STRIDE_BYTES) stride = BDM_LCD_STRIDE_BYTES;

    size_t height = v->lcd_regs[0x14] ? ((size_t)v->lcd_regs[0x14] + 1u) : BDM_LCD_ACTIVE_HEIGHT;
    if (height > BDM_LCD_ACTIVE_HEIGHT) height = BDM_LCD_ACTIVE_HEIGHT;
    if (height == 0) height = BDM_LCD_ACTIVE_HEIGHT;

    v->stride_bytes = stride;
    v->active_width = stride * 8u;
    if (v->active_width > BDM_LCD_ACTIVE_WIDTH) v->active_width = BDM_LCD_ACTIVE_WIDTH;
    v->active_height = height;
}

static void lcd_draw_byte(bdm_video_t *v, size_t offset) {
    if (!v || offset >= sizeof(v->lcd_vram) || !v->stride_bytes) return;
    size_t y = offset / v->stride_bytes;
    size_t byte_x = offset % v->stride_bytes;
    if (y >= v->active_height || byte_x >= v->stride_bytes) return;

    uint8_t data = v->lcd_vram[offset];
    unsigned x0 = (unsigned)(byte_x * 8u);
    for (unsigned bit = 0; bit < 8; ++bit) {
        unsigned x = x0 + bit;
        if (x >= BDM_LCD_WIDTH || y >= BDM_LCD_HEIGHT) continue;
        bool on = (data & (uint8_t)(0x80u >> bit)) != 0;
        v->pixels[y * BDM_LCD_WIDTH + x] = on ? LCD_PIXEL_ON : LCD_PIXEL_OFF;
    }
}

static void lcd_redraw_all(bdm_video_t *v) {
    if (!v) return;
    lcd_redecode_geometry(v);
    lcd_clear_pixels(v);
    for (size_t off = 0; off < sizeof(v->lcd_vram); ++off) lcd_draw_byte(v, off);
}

bdm_video_t *bdm_video_create(void) {
    bdm_video_t *v = (bdm_video_t *)calloc(1, sizeof(*v));
    if (v) bdm_video_reset(v);
    return v;
}

void bdm_video_destroy(bdm_video_t *video) {
    free(video);
}

void bdm_video_reset(bdm_video_t *video) {
    if (!video) return;
    memset(video->lcd_vram, 0, sizeof(video->lcd_vram));
    memset(video->lcd_regs, 0, sizeof(video->lcd_regs));
    video->lcd_index = 0;
    video->active_width = BDM_LCD_ACTIVE_WIDTH;
    video->active_height = BDM_LCD_ACTIVE_HEIGHT;
    video->stride_bytes = BDM_LCD_STRIDE_BYTES;
    video->frames = 0;
    video->lcd_dirty = 0;
    lcd_clear_pixels(video);
}

void bdm_video_set_pixel(bdm_video_t *video, unsigned x, unsigned y, uint32_t argb) {
    if (!video || x >= BDM_LCD_WIDTH || y >= BDM_LCD_HEIGHT) return;
    video->pixels[y * BDM_LCD_WIDTH + x] = argb;
}

const uint32_t *bdm_video_framebuffer(const bdm_video_t *video, size_t *width, size_t *height) {
    if (width) *width = BDM_LCD_WIDTH;
    if (height) *height = BDM_LCD_HEIGHT;
    return video ? video->pixels : NULL;
}

uint64_t bdm_video_frame_count(const bdm_video_t *video) {
    return video ? video->frames : 0;
}

void bdm_video_present_headless(bdm_video_t *video) {
    if (video) ++video->frames;
}

void bdm_video_lcd_index_write(bdm_video_t *video, uint8_t index) {
    if (!video) return;
    video->lcd_index = index;
}

void bdm_video_lcd_data_write(bdm_video_t *video, uint8_t data) {
    if (!video) return;
    video->lcd_regs[video->lcd_index] = data;
    if (video->lcd_index == 0x01u || video->lcd_index == 0x12u || video->lcd_index == 0x14u) {
        lcd_redraw_all(video);
    }
}

uint8_t bdm_video_lcd_index(const bdm_video_t *video) {
    return video ? video->lcd_index : 0xffu;
}

uint8_t bdm_video_lcd_reg(const bdm_video_t *video, uint8_t index) {
    return video ? video->lcd_regs[index] : 0xffu;
}

void bdm_video_lcd_vram_write(bdm_video_t *video, size_t offset, uint8_t data) {
    if (!video || offset >= sizeof(video->lcd_vram)) return;
    if (video->lcd_vram[offset] == data) return;
    video->lcd_vram[offset] = data;
    lcd_draw_byte(video, offset);
    ++video->lcd_dirty;
}

uint8_t bdm_video_lcd_vram_read(const bdm_video_t *video, size_t offset) {
    if (!video || offset >= sizeof(video->lcd_vram)) return 0xffu;
    return video->lcd_vram[offset];
}

size_t bdm_video_lcd_vram_size(const bdm_video_t *video) {
    (void)video;
    return BDM_LCD_VRAM_SIZE;
}

size_t bdm_video_lcd_active_width(const bdm_video_t *video) {
    return video ? video->active_width : 0u;
}

size_t bdm_video_lcd_active_height(const bdm_video_t *video) {
    return video ? video->active_height : 0u;
}

size_t bdm_video_lcd_stride_bytes(const bdm_video_t *video) {
    return video ? video->stride_bytes : 0u;
}

uint64_t bdm_video_lcd_dirty_count(const bdm_video_t *video) {
    return video ? video->lcd_dirty : 0u;
}

void bdm_video_lcd_test_pattern(bdm_video_t *video) {
    if (!video) return;
    video->lcd_regs[0x12] = BDM_LCD_STRIDE_BYTES;
    video->lcd_regs[0x14] = BDM_LCD_ACTIVE_HEIGHT - 1u;
    lcd_redecode_geometry(video);
    for (size_t y = 0; y < BDM_LCD_ACTIVE_HEIGHT; ++y) {
        for (size_t bx = 0; bx < BDM_LCD_STRIDE_BYTES; ++bx) {
            uint8_t v;
            if (y < 16 || y >= BDM_LCD_ACTIVE_HEIGHT - 16 || bx == 0 || bx == BDM_LCD_STRIDE_BYTES - 1) v = 0xffu;
            else if (((y / 8u) ^ bx) & 1u) v = 0xaau;
            else v = 0x55u;
            bdm_video_lcd_vram_write(video, y * BDM_LCD_STRIDE_BYTES + bx, v);
        }
    }
}

/* Save-state format for video only.  Little-endian, no host structs. */
#define BDM_VIDEO_STATE_SIZE (4u + 4u + 1u + 256u + 8u + 8u + 8u + 8u + 8u + BDM_LCD_VRAM_SIZE)

static void v_w32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t v_r32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void v_w64(uint8_t *p, uint64_t v) {
    for (unsigned i = 0; i < 8u; ++i) p[i] = (uint8_t)(v >> (i * 8u));
}
static uint64_t v_r64(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8u; ++i) v |= (uint64_t)p[i] << (i * 8u);
    return v;
}

size_t bdm_video_state_size(const bdm_video_t *video) {
    (void)video;
    return BDM_VIDEO_STATE_SIZE;
}

size_t bdm_video_save_state(const bdm_video_t *video, void *out_data, size_t out_capacity) {
    size_t need = BDM_VIDEO_STATE_SIZE;
    if (!video) return 0u;
    if (!out_data || out_capacity < need) return need;
    uint8_t *p = (uint8_t *)out_data;
    memcpy(p, "BDMV", 4); p += 4;
    v_w32(p, 1u); p += 4;
    *p++ = video->lcd_index;
    memcpy(p, video->lcd_regs, sizeof(video->lcd_regs)); p += sizeof(video->lcd_regs);
    v_w64(p, (uint64_t)video->active_width); p += 8;
    v_w64(p, (uint64_t)video->active_height); p += 8;
    v_w64(p, (uint64_t)video->stride_bytes); p += 8;
    v_w64(p, video->frames); p += 8;
    v_w64(p, video->lcd_dirty); p += 8;
    memcpy(p, video->lcd_vram, sizeof(video->lcd_vram));
    return need;
}

int bdm_video_load_state(bdm_video_t *video, const void *data, size_t size) {
    if (!video || !data || size < BDM_VIDEO_STATE_SIZE) return -1;
    const uint8_t *p = (const uint8_t *)data;
    if (memcmp(p, "BDMV", 4) != 0) return -1;
    p += 4;
    if (v_r32(p) != 1u) return -1;
    p += 4;
    video->lcd_index = *p++;
    memcpy(video->lcd_regs, p, sizeof(video->lcd_regs)); p += sizeof(video->lcd_regs);
    video->active_width = (size_t)v_r64(p); p += 8;
    video->active_height = (size_t)v_r64(p); p += 8;
    video->stride_bytes = (size_t)v_r64(p); p += 8;
    video->frames = v_r64(p); p += 8;
    video->lcd_dirty = v_r64(p); p += 8;
    memcpy(video->lcd_vram, p, sizeof(video->lcd_vram));
    lcd_redraw_all(video);
    return 0;
}
