#include "logging.h"

#include <syslog.h>
#include <stdio.h>


void commons_logging_init(const char *context_name) {
    (void) context_name;
    openlog(context_name, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);
}

void commons_logging_deinit() {
    closelog();
}

void commons_log_vprintf(commons_log_level level, const char *tag, const char *fmt, va_list arg) {
    if (level >= COMMONS_LOG_LEVEL_VERBOSE) {
        return;
    }
    int priorities[COMMONS_LOG_LEVEL_VERBOSE] = {
            LOG_CRIT,
            LOG_ERR,
            LOG_WARNING,
            LOG_INFO,
            LOG_DEBUG,
    };
    char buf[1024];
    vsnprintf(buf, 1023, fmt, arg);
    syslog(priorities[level], "[%s] %s", tag, buf);
}