#include "logging.h"

#include <stdarg.h>
#include <stdio.h>

static int check_level(int level, const char *tag);

void commons_log_printf(commons_log_level level, const char *tag, const char *fmt, ...) {
    if (!check_level(level, tag)) {
        return;
    }
    va_list arg;
    va_start(arg, fmt);
    commons_log_vprintf(level, tag, fmt, arg);
    va_end(arg);
}

void commons_log_hexdump(commons_log_level level, const char *tag, const void *data, size_t len) {
    commons_log_hexdump2(level, tag, COMMONS_HEXDUMP_ALL, data, len);
}

void commons_log_hexdump2(commons_log_level level, const char *tag, commons_hexdump_options options,
                          const void *data, size_t len) {
    if (!check_level(level, tag)) {
        return;
    }
    char line[80];
    static const char hex_table[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (int i = 0; i < len; i += 16) {
        int offset = 0;
        if (options & COMMONS_HEXDUMP_INDEX) {
            snprintf(line, 80, "%04x  ", i);
            offset += 6;
        }
        for (int col = i, col_end = i + 16; col < col_end; ++col) {
            if (col >= len) {
                line[offset++] = ' ';
                line[offset++] = ' ';
            } else {
                line[offset++] = hex_table[(((const unsigned char *) data)[col] >> 4) & 0xF];
                line[offset++] = hex_table[(((const unsigned char *) data)[col]) & 0xF];
            }
            line[offset++] = ' ';
            if (col - i == 7) {
                line[offset++] = ' ';
            }
        }
        if (options & COMMONS_HEXDUMP_TEXT) {
            line[offset++] = ' ';
            line[offset++] = '|';
            for (int col = i, col_end = i + 16; col < col_end && col < len; ++col) {
                if (col >= len) {
                    line[offset++] = ' ';
                } else {
                    unsigned char ch = ((const unsigned char *) data)[col];
                    line[offset++] = ch >= ' ' && ch <= '~' ? ch : '.';
                }
            }
            line[offset++] = '|';
        }
        line[offset] = '\0';
        commons_log_printf(level, tag, "%s", line);
    }
}

static int check_level(int level, const char *tag) {
    (void) tag;
    return level < COMMONS_LOG_LEVEL_VERBOSE;
}