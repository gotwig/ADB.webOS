#pragma once

#include <SDL_thread.h>
#include <SDL_events.h>
#include <SDL_mouse.h>

#ifndef EVMOUSE_MAX_FDS
#define EVMOUSE_MAX_FDS 8
#endif

typedef struct evmouse_t evmouse_t;

typedef union evmouse_event_t {
    Uint32 type;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
} evmouse_event_t;

typedef void (*evmouse_listener_t)(const evmouse_event_t *event, void *userdata);

evmouse_t *evmouse_open_default();

void evmouse_close(evmouse_t *mouse);

void evmouse_listen(evmouse_t *mouse, evmouse_listener_t listener, void *userdata);

void evmouse_set_grab(evmouse_t *mouse, SDL_bool grab);

void evmouse_interrupt(evmouse_t *mouse);

SDL_bool evmouse_is_interrupted(evmouse_t *mouse);