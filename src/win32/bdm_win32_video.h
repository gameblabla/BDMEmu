#ifndef BDM_WIN32_VIDEO_H
#define BDM_WIN32_VIDEO_H

#include "bdm_video.h"
#include <stdint.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bdm_win32_video bdm_win32_video_t;

bdm_win32_video_t *bdm_win32_video_create(HWND hwnd, unsigned scale, int integer_scaling, const char *backend);
void bdm_win32_video_destroy(bdm_win32_video_t *v);
void bdm_win32_video_resize(bdm_win32_video_t *v, unsigned width, unsigned height);
int bdm_win32_video_present(bdm_win32_video_t *v, const bdm_video_t *video);
void bdm_win32_video_window_to_pen(bdm_win32_video_t *v, const bdm_video_t *video, int wx, int wy, int touch_offset_x, int touch_offset_y, int *out_x, int *out_y);
void bdm_win32_video_window_to_pen_fp(bdm_win32_video_t *v, const bdm_video_t *video, int wx, int wy, int touch_offset_x, int touch_offset_y, int32_t *out_x_fp, int32_t *out_y_fp);
const char *bdm_win32_video_active_backend(const bdm_win32_video_t *v);
void bdm_win32_video_set_integer_scaling(bdm_win32_video_t *v, int enabled);
int bdm_win32_video_integer_scaling(const bdm_win32_video_t *v);

#ifdef __cplusplus
}
#endif

#endif
