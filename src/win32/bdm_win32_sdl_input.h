#ifndef BDM_WIN32_SDL_INPUT_H
#define BDM_WIN32_SDL_INPUT_H

#include "bdm_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bdm_win32_sdl_input bdm_win32_sdl_input_t;

bdm_win32_sdl_input_t *bdm_win32_sdl_input_create(void);
void bdm_win32_sdl_input_destroy(bdm_win32_sdl_input_t *sdl_input);
void bdm_win32_sdl_input_poll(bdm_win32_sdl_input_t *sdl_input, bdm_input_t *input, int *quit_requested);
const char *bdm_win32_sdl_input_status(const bdm_win32_sdl_input_t *sdl_input);

#ifdef __cplusplus
}
#endif

#endif
