#pragma once

#include <stdbool.h>
#include <stdio.h>

int ini_write_section(FILE *fp, const char *name);

int ini_write_string(FILE *fp, const char *name, const char *value);

int ini_write_int(FILE *fp, const char *name, int value);

int ini_write_bool(FILE *fp, const char *name, bool value);

int ini_write_comment(FILE *fp, const char *fmt, ...);