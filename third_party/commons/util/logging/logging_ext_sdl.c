#include "logging_ext_sdl.h"

void commons_sdl_log(void *userdata, int category, SDL_LogPriority priority, const char *message) {
    (void) userdata;
    static const commons_log_level priority_name[SDL_NUM_LOG_PRIORITIES] = {
            COMMONS_LOG_LEVEL_VERBOSE,
            COMMONS_LOG_LEVEL_DEBUG,
            COMMONS_LOG_LEVEL_INFO,
            COMMONS_LOG_LEVEL_WARN,
            COMMONS_LOG_LEVEL_ERROR,
            COMMONS_LOG_LEVEL_FATAL
    };
    static const char *category_name[SDL_LOG_CATEGORY_TEST + 1] = {
            "SDL.APPLICATION",
            "SDL.ERROR",
            "SDL.ASSERT",
            "SDL.SYSTEM",
            "SDL.AUDIO",
            "SDL.VIDEO",
            "SDL.RENDER",
            "SDL.INPUT",
            "SDL.TEST",
    };
    const char *tag = category > SDL_LOG_CATEGORY_TEST ? "SDL" : category_name[category];
    commons_log_printf(priority_name[priority - SDL_LOG_PRIORITY_VERBOSE], tag, "%s", message);
}