#pragma once
#include "launcher.controller.h"

typedef struct wireless_discovery_t wireless_discovery_t;
wireless_discovery_t *wireless_list_start(launcher_fragment_t *launcher);
void wireless_list_focus_first(launcher_fragment_t *launcher);
void wireless_list_stop(wireless_discovery_t *discovery);
void screencast_launch(app_t *app, const char *endpoint);
void wireless_qr_pair_open(app_t *app);
