#include "logging_ext_lvgl.h"

#include <string.h>

static void app_lv_log_line(const char *line, size_t len);

void commons_lv_log(const char *message) {
    const char *cur = message;
    do {
        const char *start = cur;
        cur = strchr(cur, '\n');
        if (cur != NULL) {
            size_t line_len = cur - start;
            if (line_len <= 0) {
                break;
            }
            app_lv_log_line(start, line_len);
            cur = cur + 1;
        } else if (start[0] != '\0') {
            app_lv_log_line(start, strlen(start));
        }
    } while (cur != NULL);
}

static void app_lv_log_line(const char *line, size_t len) {
    const char *start = memchr(line, '\t', len) + 1;
    size_t msg_len = len - (start - line);
    static const commons_log_level level_value[] = {
            COMMONS_LOG_LEVEL_VERBOSE, COMMONS_LOG_LEVEL_INFO, COMMONS_LOG_LEVEL_WARN, COMMONS_LOG_LEVEL_ERROR, COMMONS_LOG_LEVEL_DEBUG,
    };
    static const char *level_name[] = {
            "Trace", "Info", "Warn", "Error", "User",
    };
    for (int i = 0; i < sizeof(level_value) / sizeof(commons_log_level); i++) {
        if (strncmp(level_name[i], line + 1, (start - line - 3)) == 0) {
            commons_log_printf(level_value[i], "LVGL", "%.*s", (int) msg_len, start);
            return;
        }
    }
}