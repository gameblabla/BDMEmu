#ifndef BDM_VIDEO_H
#define BDM_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BDM_LCD_WIDTH 160u
#define BDM_LCD_HEIGHT 120u
#define BDM_LCD_ACTIVE_WIDTH BDM_LCD_WIDTH
#define BDM_LCD_ACTIVE_HEIGHT BDM_LCD_HEIGHT
#define BDM_LCD_STRIDE_BYTES (BDM_LCD_ACTIVE_WIDTH / 8u)
#define BDM_LCD_VRAM_SIZE (BDM_LCD_STRIDE_BYTES * BDM_LCD_ACTIVE_HEIGHT)

typedef struct bdm_video bdm_video_t;

bdm_video_t *bdm_video_create(void);
void bdm_video_destroy(bdm_video_t *video);
void bdm_video_reset(bdm_video_t *video);

void bdm_video_set_pixel(bdm_video_t *video, unsigned x, unsigned y, uint32_t argb);
const uint32_t *bdm_video_framebuffer(const bdm_video_t *video, size_t *width, size_t *height);
uint64_t bdm_video_frame_count(const bdm_video_t *video);
void bdm_video_present_headless(bdm_video_t *video);

/* LCD gate-array model.  Games write an index to ff80 and data to ff81. */
void bdm_video_lcd_index_write(bdm_video_t *video, uint8_t index);
void bdm_video_lcd_data_write(bdm_video_t *video, uint8_t data);
uint8_t bdm_video_lcd_index(const bdm_video_t *video);
uint8_t bdm_video_lcd_reg(const bdm_video_t *video, uint8_t index);

/* External 0x8000-0x895f RAM is treated as the observed 20-byte x 120-line 1bpp LCD VRAM. */
void bdm_video_lcd_vram_write(bdm_video_t *video, size_t offset, uint8_t data);
uint8_t bdm_video_lcd_vram_read(const bdm_video_t *video, size_t offset);
size_t bdm_video_lcd_vram_size(const bdm_video_t *video);
size_t bdm_video_lcd_active_width(const bdm_video_t *video);
size_t bdm_video_lcd_active_height(const bdm_video_t *video);
size_t bdm_video_lcd_stride_bytes(const bdm_video_t *video);
uint64_t bdm_video_lcd_dirty_count(const bdm_video_t *video);
void bdm_video_lcd_test_pattern(bdm_video_t *video);

/* Opaque subsystem snapshot.  Used by bdm_core save states. */
size_t bdm_video_state_size(const bdm_video_t *video);
size_t bdm_video_save_state(const bdm_video_t *video, void *out_data, size_t out_capacity);
int bdm_video_load_state(bdm_video_t *video, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
