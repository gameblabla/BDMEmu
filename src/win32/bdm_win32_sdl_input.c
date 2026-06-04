#include "bdm_win32_sdl_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(BDM_WIN64_FRONTEND)
#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

struct bdm_win32_sdl_input {
    int initialized;
    char status[96];
#if defined(BDM_WIN64_FRONTEND)
    SDL_Gamepad *pads[8];
#endif
};

bdm_win32_sdl_input_t *bdm_win32_sdl_input_create(void) {
    bdm_win32_sdl_input_t *s = (bdm_win32_sdl_input_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    strcpy(s->status, "disabled");
#if defined(BDM_WIN64_FRONTEND)
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        s->initialized = 1;
        strcpy(s->status, "SDL3 gamepad input");
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (ids) {
            int max = count < 8 ? count : 8;
            for (int i = 0; i < max; ++i) s->pads[i] = SDL_OpenGamepad(ids[i]);
            SDL_free(ids);
        }
    } else {
        const char *err = SDL_GetError();
        snprintf(s->status, sizeof(s->status), "SDL3 init failed: %s", err ? err : "unknown");
    }
#endif
    return s;
}

void bdm_win32_sdl_input_destroy(bdm_win32_sdl_input_t *s) {
    if (!s) return;
#if defined(BDM_WIN64_FRONTEND)
    for (int i = 0; i < 8; ++i) if (s->pads[i]) SDL_CloseGamepad(s->pads[i]);
    if (s->initialized) SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
#endif
    free(s);
}

void bdm_win32_sdl_input_poll(bdm_win32_sdl_input_t *s, bdm_input_t *input, int *quit_requested) {
    (void)s; (void)input; (void)quit_requested;
#if defined(BDM_WIN64_FRONTEND)
    if (!s || !s->initialized) return;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            if (quit_requested) *quit_requested = 1;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            for (int i = 0; i < 8; ++i) if (!s->pads[i]) { s->pads[i] = SDL_OpenGamepad(ev.gdevice.which); break; }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            for (int i = 0; i < 8; ++i) if (s->pads[i] && SDL_GetGamepadID(s->pads[i]) == ev.gdevice.which) { SDL_CloseGamepad(s->pads[i]); s->pads[i] = NULL; break; }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            int down = ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            switch (ev.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_SOUTH: bdm_input_set_button(input, BDM_BUTTON_A, down != 0); break;
            case SDL_GAMEPAD_BUTTON_EAST:  bdm_input_set_button(input, BDM_BUTTON_B, down != 0); break;
            case SDL_GAMEPAD_BUTTON_START: bdm_input_set_button(input, BDM_BUTTON_START, down != 0); break;
            case SDL_GAMEPAD_BUTTON_BACK:  bdm_input_set_button(input, BDM_BUTTON_SELECT, down != 0); break;
            default: break;
            }
            break;
        }
        default:
            break;
        }
    }
#endif
}

const char *bdm_win32_sdl_input_status(const bdm_win32_sdl_input_t *s) { return s ? s->status : "disabled"; }
