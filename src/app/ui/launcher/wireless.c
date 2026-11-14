#include "wireless.h"
#include "app.h"
#include "adb_devices.h"
#include "ui/streaming/streaming.controller.h"
#include "lvgl/util/lv_app_utils.h"
#include "lvgl/font/material_icons_regular_symbols.h"
#include "lvgl/theme/lv_theme_moonlight.h"
#include "util/i18n.h"
#include "src/extra/libs/qrcode/lv_qrcode.h"
#include <microdns/microdns.h>
#include <SDL.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define QR_PAIR_SERVICE_PREFIX "ADBQR-connectPhoneOverWifi"
#define QR_PAIR_TIMEOUT_MS 120000

typedef enum {
    QR_PAIR_WAITING,
    QR_PAIR_READY,
    QR_PAIR_FAILED
} qr_pair_status_t;

typedef struct {
    app_t *app;
    SDL_Thread *thread;
    SDL_mutex *lock;
    SDL_atomic_t stop;
    lv_timer_t *timer;
    lv_obj_t *dialog;
    lv_obj_t *status_label;
    Uint32 started;
    qr_pair_status_t status;
    char service_name[33];
    char password[7];
    char pair_endpoint[64];
    char error[160];
} qr_pair_t;

struct wireless_discovery_t {
    launcher_fragment_t *launcher;
    SDL_Thread *thread;
    SDL_mutex *lock;
    SDL_atomic_t stop;
    lv_timer_t *timer;
    adb_device_t devices[ADB_DEVICE_LIMIT];
    bool available[ADB_DEVICE_LIMIT];
    size_t count;
    Uint32 last_probe;
    lv_obj_t *rows[ADB_DEVICE_LIMIT];
    lv_obj_t *status;
    lv_timer_t *remove_timer;
    size_t removing_index;
    unsigned remove_seconds;
    size_t suppress_click_index;
    bool suppress_click;
};

void screencast_launch(app_t *app, const char *endpoint) {
    streaming_scene_arg_t args = {.global = app, .app = {.id = -999}};
    args.scrcpy_connection.mode = endpoint ? SCRCPY_ADB_MANUAL : SCRCPY_ADB_USB;
    if (endpoint) snprintf(args.scrcpy_connection.serial, sizeof(args.scrcpy_connection.serial), "%s", endpoint);
    app_ui_t *ui = &app->ui;
    lv_obj_t *const *container = lv_fragment_get_container(lv_fragment_manager_get_top(ui->fm));
    lv_fragment_t *stream = lv_fragment_create(&streaming_controller_class, &args);
    lv_fragment_manager_push(ui->fm, stream, container);
}

static void screencast_launch_qr_pair(app_t *app, const char *pair_endpoint, const char *password) {
    streaming_scene_arg_t args = {.global = app, .app = {.id = -999}};
    args.scrcpy_connection.mode = SCRCPY_ADB_QR;
    snprintf(args.scrcpy_connection.pair_endpoint, sizeof(args.scrcpy_connection.pair_endpoint), "%s",
             pair_endpoint);
    snprintf(args.scrcpy_connection.pair_code, sizeof(args.scrcpy_connection.pair_code), "%s", password);
    app_ui_t *ui = &app->ui;
    lv_obj_t *const *container = lv_fragment_get_container(lv_fragment_manager_get_top(ui->fm));
    lv_fragment_t *stream = lv_fragment_create(&streaming_controller_class, &args);
    lv_fragment_manager_push(ui->fm, stream, container);
}

static bool qr_random_password(char password[7]) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;

    const uint32_t limit = UINT32_MAX - (UINT32_MAX % 1000000U);
    uint32_t value;
    for (;;) {
        size_t read_size = 0;
        while (read_size < sizeof(value)) {
            ssize_t result = read(fd, (char *) &value + read_size, sizeof(value) - read_size);
            if (result < 0 && errno == EINTR) continue;
            if (result <= 0) {
                close(fd);
                return false;
            }
            read_size += (size_t) result;
        }
        if (value < limit) break;
    }
    close(fd);
    snprintf(password, 7, "%06u", (unsigned) (value % 1000000U));
    return true;
}

static bool qr_random_service_name(char service_name[33]) {
    uint32_t value;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    size_t read_size = 0;
    while (read_size < sizeof(value)) {
        ssize_t result = read(fd, (char *) &value + read_size, sizeof(value) - read_size);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            close(fd);
            return false;
        }
        read_size += (size_t) result;
    }
    close(fd);
    snprintf(service_name, 33, QR_PAIR_SERVICE_PREFIX "-%05x", value & 0xfffffU);
    return true;
}

static bool name_has_suffix(const char *name, const char *suffix) {
    size_t name_len = strlen(name), suffix_len = strlen(suffix);
    if (name_len && name[name_len - 1] == '.') --name_len;
    return name_len >= suffix_len &&
           memcmp(name + name_len - suffix_len, suffix, suffix_len) == 0;
}

static bool qr_pair_should_stop(void *data) {
    qr_pair_t *self = data;
    return SDL_AtomicGet(&self->stop) ||
           SDL_GetTicks() - self->started >= QR_PAIR_TIMEOUT_MS;
}

static void qr_pair_received(void *data, int status, const struct rr_entry *entries) {
    qr_pair_t *self = data;
    if (status < 0) return;

    char expected_name[96];
    snprintf(expected_name, sizeof(expected_name), "%s._adb-tls-pairing._tcp.local", self->service_name);
    char target[256] = {0};
    unsigned port = 0;

    for (const struct rr_entry *entry = entries; entry; entry = entry->next) {
        if (entry->type == RR_SRV && entry->ttl && entry->data.SRV.port &&
            name_has_suffix(entry->name, "._adb-tls-pairing._tcp.local") &&
            name_has_suffix(entry->name, expected_name)) {
            snprintf(target, sizeof(target), "%s", entry->data.SRV.target);
            port = entry->data.SRV.port;
            break;
        }
    }
    if (!target[0] || !port) return;

    for (const struct rr_entry *entry = entries; entry; entry = entry->next) {
        if (entry->type != RR_A || strcmp(entry->name, target)) continue;
        char ip[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &entry->data.A.addr, ip, sizeof(ip))) return;
        SDL_LockMutex(self->lock);
        if (self->status == QR_PAIR_WAITING) {
            snprintf(self->pair_endpoint, sizeof(self->pair_endpoint), "%s:%u", ip, port);
            self->status = QR_PAIR_READY;
            SDL_AtomicSet(&self->stop, 1);
        }
        SDL_UnlockMutex(self->lock);
        return;
    }
}

static int qr_pair_worker(void *data) {
    qr_pair_t *self = data;
    struct mdns_ctx *ctx = NULL;
    const char *services[] = {"_adb-tls-pairing._tcp.local"};
    int result = mdns_init(&ctx, NULL, MDNS_PORT);
    if (result >= 0) {
        result = mdns_listen(ctx, services, 1, RR_PTR, 2, qr_pair_should_stop, qr_pair_received, self);
        mdns_destroy(ctx);
    }

    SDL_LockMutex(self->lock);
    if (self->status == QR_PAIR_WAITING && !SDL_AtomicGet(&self->stop)) {
        self->status = QR_PAIR_FAILED;
        snprintf(self->error, sizeof(self->error), "%s",
                 result < 0 ? "Could not discover the phone's pairing service." :
                              "Timed out waiting for the phone. Check that Wireless debugging is enabled and scan again.");
    } else if (self->status == QR_PAIR_WAITING && result < 0) {
        self->status = QR_PAIR_FAILED;
        snprintf(self->error, sizeof(self->error), "%s",
                 "Could not discover the phone's pairing service.");
    }
    SDL_UnlockMutex(self->lock);
    return 0;
}

static void qr_error_close(lv_event_t *event) {
    lv_msgbox_close_async(lv_event_get_current_target(event));
}

static void qr_show_error(const char *message) {
    static const char *button_texts[] = {translatable("OK"), ""};
    lv_obj_t *dialog = lv_msgbox_create_i18n(NULL, locstr("Pairing failed"), locstr(message),
                                             button_texts, false);
    lv_obj_add_event_cb(dialog, qr_error_close, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(dialog);
}

static void qr_pair_delete(lv_event_t *event) {
    qr_pair_t *self = lv_event_get_user_data(event);
    if (self->timer) {
        lv_timer_del(self->timer);
        self->timer = NULL;
    }
    SDL_AtomicSet(&self->stop, 1);
    if (self->thread) {
        SDL_WaitThread(self->thread, NULL);
        self->thread = NULL;
    }
    SDL_DestroyMutex(self->lock);
    free(self);
}

static void qr_pair_dialog_event(lv_event_t *event) {
    qr_pair_t *self = lv_event_get_user_data(event);
    SDL_AtomicSet(&self->stop, 1);
    lv_msgbox_close_async(lv_event_get_current_target(event));
}

static void qr_pair_timer(lv_timer_t *timer) {
    qr_pair_t *self = timer->user_data;
    qr_pair_status_t status;
    char pair_endpoint[sizeof(self->pair_endpoint)];
    char error[sizeof(self->error)];

    SDL_LockMutex(self->lock);
    status = self->status;
    snprintf(pair_endpoint, sizeof(pair_endpoint), "%s", self->pair_endpoint);
    snprintf(error, sizeof(error), "%s", self->error);
    SDL_UnlockMutex(self->lock);

    if (status == QR_PAIR_WAITING) return;
    lv_timer_del(self->timer);
    self->timer = NULL;
    if (status == QR_PAIR_FAILED) {
        lv_label_set_text(self->status_label, error);
        return;
    }

    SDL_AtomicSet(&self->stop, 1);
    if (self->thread) {
        SDL_WaitThread(self->thread, NULL);
        self->thread = NULL;
    }
    lv_label_set_text(self->status_label, locstr("Phone found. Starting wireless ADB pairing..."));
    screencast_launch_qr_pair(self->app, pair_endpoint, self->password);
    lv_msgbox_close_async(self->dialog);
}

void wireless_qr_pair_open(app_t *app) {
    qr_pair_t *self = calloc(1, sizeof(*self));
    if (!self) {
        qr_show_error("Could not allocate QR pairing state.");
        return;
    }
    self->app = app;
    self->lock = SDL_CreateMutex();
    if (!self->lock || !qr_random_password(self->password) ||
        !qr_random_service_name(self->service_name)) {
        if (self->lock) SDL_DestroyMutex(self->lock);
        free(self);
        qr_show_error("Could not generate secure pairing credentials.");
        return;
    }
    self->started = SDL_GetTicks();

    static const char *button_texts[] = {translatable("Cancel"), ""};
    self->dialog = lv_msgbox_create_i18n(NULL, locstr("Pair android device"), NULL, button_texts, false);
    lv_obj_t *content = lv_msgbox_get_content(self->dialog);
    lv_disp_t *display = lv_disp_get_default();
    lv_coord_t screen_width = lv_disp_get_hor_res(display);
    lv_coord_t screen_height = lv_disp_get_ver_res(display);
    lv_obj_set_width(self->dialog, screen_width * 85 / 100);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, LV_DPX(10), 0);

    char payload[128];
    snprintf(payload, sizeof(payload), "WIFI:T:ADB;S:%s;P:%s;;", self->service_name, self->password);
    lv_coord_t qr_size = LV_DPX(300);
    lv_coord_t max_qr_size = screen_height * 45 / 100;
    if (qr_size > max_qr_size) qr_size = max_qr_size;
    lv_obj_t *qr = lv_qrcode_create(content, qr_size, lv_color_black(), lv_color_white());
    if (!qr || lv_qrcode_update(qr, payload, (uint32_t) strlen(payload)) != LV_RES_OK) {
        lv_obj_del(self->dialog);
        SDL_DestroyMutex(self->lock);
        free(self);
        qr_show_error("Could not render the QR code.");
        return;
    }

    lv_obj_t *instructions = lv_label_create(content);
    lv_obj_set_width(instructions, LV_PCT(100));
    lv_label_set_long_mode(instructions, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(instructions, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(instructions, "To connect device over WiFi scan this QR code under:\nDeveloper Options > Wireless Debugging > Pair device with QR code");

    self->status_label = lv_label_create(content);
    lv_obj_set_width(self->status_label, LV_PCT(100));
    lv_label_set_long_mode(self->status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(self->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(self->status_label, locstr("Waiting for your phone to scan the QR code..."));

    lv_obj_add_event_cb(self->dialog, qr_pair_dialog_event, LV_EVENT_VALUE_CHANGED, self);
    lv_obj_add_event_cb(self->dialog, qr_pair_delete, LV_EVENT_DELETE, self);
    lv_obj_center(self->dialog);
    self->thread = SDL_CreateThread(qr_pair_worker, "adb-qr-pairing", self);
    if (!self->thread) {
        self->status = QR_PAIR_FAILED;
        snprintf(self->error, sizeof(self->error), "%s", "Could not start wireless pairing discovery.");
        lv_label_set_text(self->status_label, self->error);
        return;
    }
    self->timer = lv_timer_create(qr_pair_timer, 250, self);
}

/* Saved legacy ADB phones need not advertise mDNS. Only show them while reachable. */
static bool endpoint_available(const char *endpoint) {
    char ip[INET_ADDRSTRLEN], extra;
    unsigned port;
    struct sockaddr_in address = {.sin_family = AF_INET};
    if (sscanf(endpoint, "%15[0-9.]:%u%c", ip, &port, &extra) != 2 ||
        !port || port > 65535 || inet_pton(AF_INET, ip, &address.sin_addr) != 1) return false;
    address.sin_port = htons(port);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd); return false; }
    bool available = connect(fd, (struct sockaddr *) &address, sizeof(address)) == 0;
    if (!available) {
        fd_set writes;
        FD_ZERO(&writes);
        FD_SET(fd, &writes);
        struct timeval timeout = {.tv_usec = 250000};
        int error = -1;
        socklen_t size = sizeof(error);
        available = select(fd + 1, NULL, &writes, NULL, &timeout) > 0 &&
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0;
    }
    close(fd);
    return available;
}

static bool discovery_stop(void *data) {
    wireless_discovery_t *self = data;
    if (SDL_AtomicGet(&self->stop)) return true;
    Uint32 now = SDL_GetTicks();
    if (now - self->last_probe >= 5000) {
        SDL_LockMutex(self->lock);
        size_t count = self->count;
        SDL_UnlockMutex(self->lock);
        for (size_t i = 0; i < count; ++i) {
            if (SDL_AtomicGet(&self->stop)) return true;
            char endpoint[64];
            SDL_LockMutex(self->lock);
            snprintf(endpoint, sizeof(endpoint), "%s", self->devices[i].endpoint);
            SDL_UnlockMutex(self->lock);
            bool available = endpoint_available(endpoint);
            SDL_LockMutex(self->lock);
            self->available[i] = available;
            SDL_UnlockMutex(self->lock);
        }
        self->last_probe = now;
    }
    return false;
}

/* Resolve changed addresses only for phones previously authorized over USB. */
static void discovery_received(void *data, int status, const struct rr_entry *entries) {
    wireless_discovery_t *self = data;
    if (status < 0) return;
    SDL_LockMutex(self->lock);
    for (const struct rr_entry *srv = entries; srv; srv = srv->next) {
        if (srv->type != RR_SRV || !srv->ttl || !srv->data.SRV.port) continue;
        const char *service_type = NULL;
        if (name_has_suffix(srv->name, "._adb._tcp.local")) {
            service_type = "_adb._tcp.local";
        } else if (name_has_suffix(srv->name, "._adb-tls-connect._tcp.local")) {
            service_type = "_adb-tls-connect._tcp.local";
        } else {
            continue;
        }
        for (size_t i = 0; i < self->count; ++i) {
            char prefix[160], exact[160];
            snprintf(prefix, sizeof(prefix), "adb-%s-", self->devices[i].serial);
            snprintf(exact, sizeof(exact), "%s.%s", self->devices[i].serial, service_type);
            if (strncmp(srv->name, prefix, strlen(prefix)) && strcmp(srv->name, exact)) continue;
            for (const struct rr_entry *address = entries; address; address = address->next) {
                if (address->type != RR_A || strcmp(address->name, srv->data.SRV.target)) continue;
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &address->data.A.addr, ip, sizeof(ip));
                snprintf(self->devices[i].endpoint, sizeof(self->devices[i].endpoint), "%s:%u", ip, srv->data.SRV.port);
                break;
            }
        }
    }
    SDL_UnlockMutex(self->lock);
}

static int discovery_worker(void *data) {
    wireless_discovery_t *self = data;
    struct mdns_ctx *ctx = NULL;
    const char *services[] = {"_adb._tcp.local", "_adb-tls-connect._tcp.local"};
    self->last_probe = SDL_GetTicks() - 5000;
    discovery_stop(self);
    if (mdns_init(&ctx, NULL, MDNS_PORT) >= 0) {
        mdns_listen(ctx, services, 2, RR_PTR, 5, discovery_stop, discovery_received, self);
    }
    if (ctx) mdns_destroy(ctx);
    /* Saved devices remain usable when multicast is unavailable. */
    while (!SDL_AtomicGet(&self->stop)) {
        discovery_stop(self);
        SDL_Delay(100);
    }
    return 0;
}

static void device_clicked(lv_event_t *event) {
    wireless_discovery_t *self = lv_event_get_user_data(event);
    int index = (int) (uintptr_t) lv_obj_get_user_data(lv_event_get_current_target(event));
    if (self->suppress_click && self->suppress_click_index == (size_t) index) {
        self->suppress_click = false;
        return;
    }
    char endpoint[64];
    SDL_LockMutex(self->lock);
    bool available = self->available[index];
    snprintf(endpoint, sizeof(endpoint), "%s", self->devices[index].endpoint);
    SDL_UnlockMutex(self->lock);
    if (!available) return;
    screencast_launch(self->launcher->global, endpoint);
}

static void remove_countdown(lv_timer_t *timer) {
    wireless_discovery_t *self = timer->user_data;
    if (!self->remove_seconds) {
        SDL_LockMutex(self->lock);
        char serial[sizeof(self->devices[0].serial)];
        snprintf(serial, sizeof(serial), "%s", self->devices[self->removing_index].serial);
        SDL_UnlockMutex(self->lock);
        adb_devices_remove(self->launcher->global->settings.conf_dir, serial);
        lv_timer_del(self->remove_timer);
        self->remove_timer = NULL;
        lv_group_clear_focus(self->launcher->nav_group);
        lv_obj_del(self->rows[self->removing_index]);
        self->rows[self->removing_index] = NULL;
        bool has_rows = false;
        for (size_t i = 0; i < self->count; ++i) {
            if (self->rows[i]) {
                has_rows = true;
                break;
            }
        }
        if (!has_rows) {
            lv_obj_add_flag(self->status, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    char label[64];
    snprintf(label, sizeof(label), "Removing entry in %u...", self->remove_seconds);
    lv_obj_t *child = lv_obj_get_child(self->rows[self->removing_index], 1);
    if (child) {
        lv_label_set_text(child, label);
        lv_obj_set_style_text_color(child, lv_color_hex(0xff0000), LV_PART_MAIN);
    }
    --self->remove_seconds;
}

static void device_hold(lv_event_t *event) {
    wireless_discovery_t *self = lv_event_get_user_data(event);
    lv_obj_t *row = lv_event_get_current_target(event);
    self->removing_index = (size_t)(uintptr_t)lv_obj_get_user_data(row);
    self->suppress_click_index = self->removing_index;
    self->suppress_click = true;
    if (self->remove_timer) lv_timer_del(self->remove_timer);
    self->remove_seconds = 4;
    self->remove_timer = lv_timer_create(remove_countdown, 1000, self);
}

static void device_cancel_remove(lv_event_t *event) {
    wireless_discovery_t *self = lv_event_get_user_data(event);
    lv_obj_t *row = lv_event_get_current_target(event);
    size_t index = (size_t)(uintptr_t)lv_obj_get_user_data(row);
    if (lv_event_get_code(event) == LV_EVENT_CANCEL) {
        lv_event_stop_bubbling(event);
        self->suppress_click_index = index;
        self->suppress_click = true;
    }
    if (!self->remove_timer || self->removing_index != index) return;

    lv_timer_del(self->remove_timer);
    self->remove_timer = NULL;
    self->remove_seconds = 0;

    char name[sizeof(self->devices[0].name)];
    SDL_LockMutex(self->lock);
    snprintf(name, sizeof(name), "%s", self->devices[index].name);
    SDL_UnlockMutex(self->lock);
    lv_obj_t *label = lv_obj_get_child(row, 1);
    if (label) {
        lv_label_set_text(label, name);
        lv_obj_remove_local_style_prop(label, LV_STYLE_TEXT_COLOR, LV_PART_MAIN);
    }
}

static void device_pressed(lv_event_t *event) {
    wireless_discovery_t *self = lv_event_get_user_data(event);
    size_t index = (size_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_current_target(event));
    if (self->suppress_click && self->suppress_click_index == index) {
        self->suppress_click = false;
    }
}

static void add_device_row(wireless_discovery_t *self, size_t index) {
    char label[160];
    snprintf(label, sizeof(label), "%s", self->devices[index].name);
    lv_obj_t *button = lv_list_add_btn(self->launcher->wireless_list, MAT_SYMBOL_ONDEMAND_VIDEO, label);
    lv_obj_add_style(button, &self->launcher->nav_menu_style, 0);
    lv_btn_set_icon_font(button, lv_theme_moonlight_get_iconfont_small(button));
    lv_btn_set_text_font(button, lv_theme_get_font_small(button));
    lv_obj_set_user_data(button, (void *) (uintptr_t) index);
    lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_group_add_obj(self->launcher->nav_group, button);
    lv_obj_clear_flag(self->status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(button, device_pressed, LV_EVENT_PRESSED, self);
    lv_obj_add_event_cb(button, device_clicked, LV_EVENT_SHORT_CLICKED, self);
    lv_obj_add_event_cb(button, device_hold, LV_EVENT_LONG_PRESSED, self);
    lv_obj_add_event_cb(button, device_cancel_remove, LV_EVENT_RELEASED, self);
    lv_obj_add_event_cb(button, device_cancel_remove, LV_EVENT_CANCEL, self);
    lv_obj_add_event_cb(button, device_cancel_remove, LV_EVENT_PRESS_LOST, self);
    self->rows[index] = button;
}

static void refresh_devices(wireless_discovery_t *self) {
    adb_device_t devices[ADB_DEVICE_LIMIT];
    size_t count = adb_devices_load(self->launcher->global->settings.conf_dir, devices);
    for (size_t i = 0; i < count; ++i) {
        size_t index;
        SDL_LockMutex(self->lock);
        for (index = 0; index < self->count; ++index) {
            if (!strcmp(self->devices[index].serial, devices[i].serial)) break;
        }
        if (index < self->count) {
            snprintf(self->devices[index].endpoint, sizeof(self->devices[index].endpoint), "%s",
                     devices[i].endpoint);
            snprintf(self->devices[index].name, sizeof(self->devices[index].name), "%s",
                     devices[i].name);
            bool needs_row = self->rows[index] == NULL;
            SDL_UnlockMutex(self->lock);
            if (needs_row) add_device_row(self, index);
            continue;
        }
        if (self->count == ADB_DEVICE_LIMIT) {
            SDL_UnlockMutex(self->lock);
            break;
        }
        bool was_empty = self->count == 0;
        index = self->count++;
        self->devices[index] = devices[i];
        SDL_UnlockMutex(self->lock);
        if (was_empty) lv_label_set_text(self->status, locstr("Wireless devices"));
        add_device_row(self, index);
    }
}

static void update_list(lv_timer_t *timer) {
    wireless_discovery_t *self = timer->user_data;
    refresh_devices(self);
    SDL_LockMutex(self->lock);
    for (size_t i = 0; i < self->count; ++i) {
        /* Rows remain focusable/clickable even while offline, so they can be removed. */
        if (self->rows[i]) lv_obj_clear_state(self->rows[i], LV_STATE_DISABLED);
    }
    SDL_UnlockMutex(self->lock);
}

wireless_discovery_t *wireless_list_start(launcher_fragment_t *launcher) {
    wireless_discovery_t *self = calloc(1, sizeof(*self));
    if (!self) return NULL;
    self->launcher = launcher;
    self->lock = SDL_CreateMutex();
    if (!self->lock) { free(self); return NULL; }
    adb_device_t devices[ADB_DEVICE_LIMIT];
    size_t count = adb_devices_load(launcher->global->settings.conf_dir, devices);
    self->status = lv_list_add_text(launcher->wireless_list, locstr("Wireless devices"));
    lv_obj_set_style_text_font(self->status, lv_theme_get_font_small(self->status), 0);
    lv_obj_set_style_pad_left(self->status, LV_DPX(12), 0);
    for (size_t i = 0; i < count; ++i) {
        SDL_LockMutex(self->lock);
        size_t index = self->count++;
        self->devices[index] = devices[i];
        SDL_UnlockMutex(self->lock);
        add_device_row(self, index);
    }
    lv_group_add_obj(launcher->nav_group, launcher->scrcpy_btn);
    lv_group_add_obj(launcher->nav_group, launcher->qr_pair_btn);
    lv_group_remove_obj(launcher->quit_btn);
    lv_group_add_obj(launcher->nav_group, launcher->quit_btn);
    if (!count) lv_obj_add_flag(self->status, LV_OBJ_FLAG_HIDDEN);
    self->thread = SDL_CreateThread(discovery_worker, "wireless-discovery", self);
    if (!self->thread) {
        lv_label_set_text(self->status, locstr("Wireless discovery could not start."));
    }
    self->timer = lv_timer_create(update_list, 1000, self);
    return self;
}

void wireless_list_focus_first(launcher_fragment_t *launcher) {
    wireless_discovery_t *self = launcher->wireless_discovery;
    if (self) {
        for (size_t i = 0; i < self->count; ++i) {
            if (self->rows[i]) {
                lv_group_focus_obj(self->rows[i]);
                return;
            }
        }
    }
    lv_group_focus_obj(launcher->scrcpy_btn);
}

void wireless_list_stop(wireless_discovery_t *self) {
    if (!self) return;
    lv_timer_del(self->timer);
    if (self->remove_timer) {
        lv_timer_del(self->remove_timer);
        self->remove_timer = NULL;
    }
    SDL_AtomicSet(&self->stop, 1);
    if (self->thread) SDL_WaitThread(self->thread, NULL);
    SDL_DestroyMutex(self->lock);
    free(self);
}
