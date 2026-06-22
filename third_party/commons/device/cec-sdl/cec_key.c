#include "cec_key.h"

#include "cec_sdl.h"

#include <SDL_events.h>
#include <SDL_timer.h>

static SDL_KeyCode key_from_cec(cec_user_control_code code);

void cec_sdl_cb_key(void *cbparam, const cec_keypress *keypress) {
    cec_sdl_ctx_t *support = cbparam;
    SDL_Window *focused = SDL_GetKeyboardFocus();
    Uint32 window_id;
    if (focused != NULL) {
        window_id = SDL_GetWindowID(focused);
    } else if (support->enable_unfocused) {
        window_id = 0;
    } else {
        return;
    }
    SDL_KeyCode key_code = key_from_cec(keypress->keycode);
    if (key_code == 0) {
        return;
    }
    SDL_KeyboardEvent key_event = {
            .type = keypress->duration == 0 ? SDL_KEYDOWN : SDL_KEYUP,
            .timestamp = SDL_GetTicks(),
            .windowID = window_id,
            .state = keypress->duration == 0 ? SDL_PRESSED : SDL_RELEASED,
            .repeat = 0,
            .keysym = {
                    .scancode = SDL_GetScancodeFromKey(key_code),
                    .sym = key_code,
            }
    };
    SDL_Event event = {.key = key_event};
    SDL_PushEvent(&event);
}

static SDL_KeyCode key_from_cec(cec_user_control_code code) {
    switch (code) {
        case CEC_USER_CONTROL_CODE_SELECT:
            return SDLK_RETURN;
        case CEC_USER_CONTROL_CODE_UP:
            return SDLK_UP;
        case CEC_USER_CONTROL_CODE_DOWN:
            return SDLK_DOWN;
        case CEC_USER_CONTROL_CODE_LEFT:
            return SDLK_LEFT;
        case CEC_USER_CONTROL_CODE_RIGHT:
            return SDLK_RIGHT;
        case CEC_USER_CONTROL_CODE_EXIT:
            return SDLK_ESCAPE;
        case CEC_USER_CONTROL_CODE_F1_BLUE:
            return SDLK_F1;
        case CEC_USER_CONTROL_CODE_F2_RED:
            return SDLK_F2;
        case CEC_USER_CONTROL_CODE_F3_GREEN:
            return SDLK_F3;
        case CEC_USER_CONTROL_CODE_F4_YELLOW:
            return SDLK_F4;
        default:
            return SDLK_UNKNOWN;
    }
}