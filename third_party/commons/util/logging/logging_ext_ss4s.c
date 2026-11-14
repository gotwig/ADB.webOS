#include "logging_ext_ss4s.h"

#include <string.h>
#include <stdio.h>

void commons_ss4s_logf(SS4S_LogLevel level, const char *tag, const char *fmt, ...) {
    char app_tag[32] = "SS4S.";
    strncpy(app_tag + 5, tag, 27);
    va_list arg;
    va_start(arg, fmt);
    char msg[1024];
    vsnprintf(msg, 1024, fmt, arg);
    va_end(arg);
    commons_log_printf((commons_log_level) level, app_tag, "%s", msg);
}