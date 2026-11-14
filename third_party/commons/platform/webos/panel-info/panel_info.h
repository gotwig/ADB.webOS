#pragma once

#include <stdbool.h>

typedef struct webos_panel_info_t {
    int width;
    int height;
    int rate;
} webos_panel_info_t;

int commons_webos_get_panel_info(webos_panel_info_t *info);