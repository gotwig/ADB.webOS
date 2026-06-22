#pragma once

#include <stddef.h>
#include <stdarg.h>

typedef enum commons_log_level {
    COMMONS_LOG_LEVEL_FATAL,
    COMMONS_LOG_LEVEL_ERROR,
    COMMONS_LOG_LEVEL_WARN,
    COMMONS_LOG_LEVEL_INFO,
    COMMONS_LOG_LEVEL_DEBUG,
    COMMONS_LOG_LEVEL_VERBOSE,
} commons_log_level;

typedef enum commons_hexdump_options {
    COMMONS_HEXDUMP_NONE = 0x00,
    COMMONS_HEXDUMP_INDEX = 0x01,
    COMMONS_HEXDUMP_TEXT = 0x02,
    COMMONS_HEXDUMP_ALL = 0xFF,
} commons_hexdump_options;

void commons_logging_init(const char *context_name);

void commons_logging_deinit();

void commons_log_printf(commons_log_level level, const char *tag, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));

void commons_log_hexdump(commons_log_level level, const char *tag, const void *data, size_t len);

void commons_log_hexdump2(commons_log_level level, const char *tag, commons_hexdump_options options,
                          const void *data, size_t len);

void commons_log_vprintf(commons_log_level level, const char *tag, const char *fmt, va_list arg);

#define commons_log_fatal(tag, ...) commons_log_printf(COMMONS_LOG_LEVEL_FATAL, (tag), __VA_ARGS__)

#define commons_log_error(tag, ...) commons_log_printf(COMMONS_LOG_LEVEL_ERROR, (tag), __VA_ARGS__)

#define commons_log_warn(tag, ...) commons_log_printf(COMMONS_LOG_LEVEL_WARN, (tag), __VA_ARGS__)

#define commons_log_info(tag, ...) commons_log_printf(COMMONS_LOG_LEVEL_INFO, (tag), __VA_ARGS__)

#define commons_log_debug(tag, ...) commons_log_printf(COMMONS_LOG_LEVEL_DEBUG, (tag), __VA_ARGS__)

#define commons_log_verbose(tag, ...) commons_log_printf(COMMONS_LOG_LEVEL_VERBOSE, (tag), __VA_ARGS__)
