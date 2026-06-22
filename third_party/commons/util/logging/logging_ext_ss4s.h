#pragma once

#include "logging.h"
#include "ss4s/logging.h"

void commons_ss4s_logf(SS4S_LogLevel level, const char *tag, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));