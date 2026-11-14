#pragma once

#include "uuidstr.h"
#include "client.h"

typedef struct app_t app_t;

typedef struct scrcpy_connection_t {
    /* Empty serial selects the existing USB ADB path. */
    char serial[128];
    char pair_endpoint[64];
    char pair_code[7];
    enum {
        SCRCPY_ADB_USB,
        SCRCPY_ADB_MANUAL,
        SCRCPY_ADB_QR
    } mode;
} scrcpy_connection_t;

int app_session_begin(app_t *app, const uuidstr_t *uuid, const APP_LIST *gs_app);

int app_session_begin_scrcpy(app_t *app, const scrcpy_connection_t *connection);

void app_session_destroy(app_t *app);
