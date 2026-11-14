#include "evmouse.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <linux/input.h>

#include "logging.h"

typedef struct mouse_info_t {
    struct input_id id;
} mouse_info_t;

typedef SDL_bool (*mouse_filter_fn)(const mouse_info_t *info, SDL_bool *grab);

typedef struct dev_fd_t {
    int fd;
    SDL_bool grab;
} dev_fd_t;

struct evmouse_t {
    SDL_mutex *lock;
    SDL_bool listening;
    dev_fd_t fds[EVMOUSE_MAX_FDS];
    int nfds;
};

static int mouse_fds_find(dev_fd_t *fds, mouse_filter_fn filter);

static SDL_bool mouse_filter_any(const mouse_info_t *info, SDL_bool *grab);

static SDL_bool is_mouse(int fd, mouse_info_t *info);

static inline SDL_bool has_bit(const uint8_t *bits, uint32_t bit);

static void dispatch_motion(const struct input_event *raw, evmouse_listener_t listener, void *userdata);

static void dispatch_wheel(const struct input_event *raw, evmouse_listener_t listener, void *userdata);

static void dispatch_button(const struct input_event *raw, evmouse_listener_t listener, void *userdata);


evmouse_t *evmouse_open_default() {
    evmouse_t *mouse = malloc(sizeof(evmouse_t));
    memset(mouse, 0, sizeof(evmouse_t));
    mouse->lock = SDL_CreateMutex();
    mouse->nfds = mouse_fds_find(mouse->fds, mouse_filter_any);
    if (mouse->nfds <= 0) {
        free(mouse);
        return NULL;
    }
    return mouse;
}

void evmouse_close(evmouse_t *mouse) {
    assert(mouse != NULL);
    int ret = SDL_LockMutex(mouse->lock);
    assert(ret == 0);
    for (int i = 0; i < mouse->nfds; i++) {
        if (mouse->fds[i].grab) {
            if (ioctl(mouse->fds[i].fd, EVIOCGRAB, 0) < 0) {
                commons_log_warn("EvMouse", "Failed to ungrab fd %d: %d (%s)", mouse->fds[i].fd, errno,
                                 strerror(errno));
            }
        }
        close(mouse->fds[i].fd);
    }
    SDL_UnlockMutex(mouse->lock);
    SDL_DestroyMutex(mouse->lock);
    free(mouse);
}

void evmouse_listen(evmouse_t *mouse, evmouse_listener_t listener, void *userdata) {
    if (SDL_LockMutex(mouse->lock) != 0) {
        return;
    }
    if (mouse->listening) {
        SDL_UnlockMutex(mouse->lock);
        return;
    }
    mouse->listening = SDL_TRUE;
    SDL_UnlockMutex(mouse->lock);
    while (evmouse_is_interrupted(mouse)) {
        fd_set fds;
        FD_ZERO(&fds);
        for (int i = 0; i < mouse->nfds; i++) {
            FD_SET(mouse->fds[i].fd, &fds);
        }
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 1000};
        if (select(FD_SETSIZE, &fds, NULL, NULL, &timeout) <= 0) {
            continue;
        }

        for (int i = 0; i < mouse->nfds; i++) {
            int fd = mouse->fds[i].fd;
            if (!FD_ISSET(fd, &fds)) {
                continue;
            }
            struct input_event raw_ev;
            if (read(fd, &raw_ev, sizeof(raw_ev)) != sizeof(raw_ev)) {
                continue;
            }
            switch (raw_ev.type) {
                case EV_REL: {
                    switch (raw_ev.code) {
                        case REL_X:
                        case REL_Y:
                            dispatch_motion(&raw_ev, listener, userdata);
                            break;
                        case REL_HWHEEL:
                        case REL_WHEEL:
                            dispatch_wheel(&raw_ev, listener, userdata);
                            break;
                    }
                    break;
                }
                case EV_KEY: {
                    if (raw_ev.code >= BTN_LEFT && raw_ev.code <= BTN_TASK) {
                        dispatch_button(&raw_ev, listener, userdata);
                    }
                    break;
                }
            }
        }
    }
}

void evmouse_set_grab(evmouse_t *mouse, SDL_bool grab) {
    if (SDL_LockMutex(mouse->lock) != 0) {
        return;
    }
    for (int i = 0; i < mouse->nfds; i++) {
        if (mouse->fds[i].grab != grab) {
            if (ioctl(mouse->fds[i].fd, EVIOCGRAB, grab ? 1 : 0) < 0) {
                commons_log_warn("EvMouse", "Failed to set grab=%d on fd %d: %d (%s)", grab, mouse->fds[i].fd,
                                 errno, strerror(errno));
            } else {
                mouse->fds[i].grab = grab;
            }
        }
    }
    SDL_UnlockMutex(mouse->lock);
}

void evmouse_interrupt(evmouse_t *mouse) {
    if (SDL_LockMutex(mouse->lock) != 0) {
        return;
    }
    mouse->listening = SDL_FALSE;
    SDL_UnlockMutex(mouse->lock);
}

SDL_bool evmouse_is_interrupted(evmouse_t *mouse) {
    if (SDL_LockMutex(mouse->lock) != 0) {
        return SDL_FALSE;
    }
    SDL_bool interrupted;
    interrupted = mouse->listening;
    SDL_UnlockMutex(mouse->lock);
    return interrupted;
}

static int mouse_fds_find(dev_fd_t *fds, mouse_filter_fn filter) {
    DIR *dir = opendir("/dev/input");
    if (dir == NULL) {
        return 0;
    }
    struct dirent *ent;
    char dev_path[32] = "/dev/input/";
    int nfds = 0;
    while (nfds < EVMOUSE_MAX_FDS && (ent = readdir(dir))) {
        if (strncmp(ent->d_name, "event", 5) != 0) {
            continue;
        }
        strncpy(&dev_path[11], ent->d_name, 20);
        dev_path[31] = '\0';
        int fd = open(dev_path, O_RDONLY);
        if (fd < 0) {
            // Silently ignore "No such device or address"
            if (errno != ENXIO) {
                commons_log_warn("EvMouse", "Failed to open %s: %d (%s)", dev_path, errno, strerror(errno));
            }
            continue;
        }
        mouse_info_t mouse_info;
        if (is_mouse(fd, &mouse_info) && (!filter || filter(&mouse_info, &fds[nfds].grab))) {
            commons_log_debug("EvMouse", "Opened mouse device: %s", dev_path);
            fds[nfds].fd = fd;
            if (fds[nfds].grab) {
                if (ioctl(fd, EVIOCGRAB, 1) < 0) {
                    commons_log_warn("EvMouse", "Failed to grab %s: %d (%s)", dev_path, errno, strerror(errno));
                    fds[nfds].grab = SDL_FALSE;
                }
            }
            nfds++;
            continue;
        }
        close(fd);
    }
    closedir(dir);
    return nfds;
}

static SDL_bool is_mouse(int fd, mouse_info_t *info) {
    if (ioctl(fd, EVIOCGID, &info->id) < 0) {
        return SDL_FALSE;
    }

    uint8_t keycaps[(KEY_MAX / 8) + 1];
    uint8_t relcaps[(REL_MAX / 8) + 1];

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keycaps)), keycaps) < 0) {
        return SDL_FALSE;
    }

    if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relcaps)), relcaps) < 0) {
        return SDL_FALSE;
    }
    if (has_bit(keycaps, BTN_MOUSE) && has_bit(relcaps, REL_X) && has_bit(relcaps, REL_Y)) {
        return SDL_TRUE;
    }

    return SDL_FALSE;
}

static SDL_bool mouse_filter_any(const mouse_info_t *info, SDL_bool *grab) {
    (void) info;
    *grab = SDL_FALSE;
    return SDL_TRUE;
}

static inline SDL_bool has_bit(const uint8_t *bits, uint32_t bit) {
    return (bits[bit / 8] & 1 << (bit % 8)) != 0;
}

static void dispatch_motion(const struct input_event *raw, evmouse_listener_t listener, void *userdata) {
    evmouse_event_t event = {.motion = {.type=SDL_MOUSEMOTION}};
    switch (raw->code) {
        case REL_X:
            event.motion.xrel = raw->value;
            break;
        case REL_Y:
            event.motion.yrel = raw->value;
            break;
    }
    listener(&event, userdata);
}

static void dispatch_wheel(const struct input_event *raw, evmouse_listener_t listener, void *userdata) {
    evmouse_event_t event = {.wheel = {.type=SDL_MOUSEWHEEL}};
    switch (raw->code) {
        case REL_WHEEL:
            event.wheel.y = raw->value;
            break;
        case REL_HWHEEL:
            event.wheel.x = raw->value;
            break;
    }
    listener(&event, userdata);
}

static void dispatch_button(const struct input_event *raw, evmouse_listener_t listener, void *userdata) {
    evmouse_event_t event = {
        .button = {
            .type=raw->value ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP,
            .state = raw->value ? SDL_PRESSED : SDL_RELEASED
        }
    };
    switch (raw->code) {
        case BTN_LEFT:
            event.button.button = SDL_BUTTON_LEFT;
            break;
        case BTN_RIGHT:
            event.button.button = SDL_BUTTON_RIGHT;
            break;
        case BTN_MIDDLE:
            event.button.button = SDL_BUTTON_MIDDLE;
            break;
        case BTN_SIDE:
            event.button.button = SDL_BUTTON_X1;
            break;
        case BTN_EXTRA:
            event.button.button = SDL_BUTTON_X2;
            break;
        case BTN_FORWARD:
            event.button.button = SDL_BUTTON_X2 + 1;
            break;
        case BTN_BACK:
            event.button.button = SDL_BUTTON_X2 + 2;
            break;
        case BTN_TASK:
            event.button.button = SDL_BUTTON_X2 + 3;
            break;
        default:
            commons_log_warn("EvMouse", "Unhandled button event: %d", raw->code);
            return;
    }
    listener(&event, userdata);
}