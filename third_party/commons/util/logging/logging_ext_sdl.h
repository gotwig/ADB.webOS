#pragma once

#include "logging.h"
#include <SDL_log.h>

void commons_sdl_log(void *userdata, int category, SDL_LogPriority priority, const char *message);