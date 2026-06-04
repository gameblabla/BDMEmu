#ifndef BDM_IMAGE_IMPORT_H
#define BDM_IMAGE_IMPORT_H

#include "bdm_core.h"
#include "bdm_frontend.h"
#include "bdm_input.h"
#include "bdm_video.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bdm_fe_rect {
    int x;
    int y;
    int w;
    int h;
    int confidence;
} bdm_fe_rect_t;

typedef struct bdm_fe_image_import_options {
    float dither_strength;          /* 0.0 = threshold only, 1.0 = full Floyd-Steinberg. Default frontend UI uses 0.85. */
    unsigned down_steps;            /* Stylus settle steps at the start of each run. 0 selects default. */
    unsigned pixel_steps;           /* Steps while advancing one LCD pixel. 0 selects default. */
    unsigned up_steps;              /* Stylus release steps after each run. 0 selects default. */
    int touch_offset_x;
    int touch_offset_y;
} bdm_fe_image_import_options_t;

void bdm_fe_image_import_options_init(bdm_fe_image_import_options_t *opt);
int bdm_fe_detect_drawing_area(const bdm_video_t *video, bdm_fe_rect_t *out_rect, char *status, size_t status_cap);
int bdm_fe_import_image_file_to_drawing_area(const char *path,
                                             bdm_video_t *video,
                                             bdm_input_t *input,
                                             bdm_core_t *core,
                                             const bdm_fe_image_import_options_t *opt,
                                             char *status,
                                             size_t status_cap);
int bdm_fe_import_image_pixels_to_drawing_area(const uint8_t *rgb,
                                               int width,
                                               int height,
                                               int stride_bytes,
                                               bdm_video_t *video,
                                               bdm_input_t *input,
                                               bdm_core_t *core,
                                               const bdm_fe_image_import_options_t *opt,
                                               char *status,
                                               size_t status_cap);

#ifdef __cplusplus
}
#endif

#endif
