#pragma once

/**
 * Copy file for path @p source @p to @p destination @p
 * @param source Source file path
 * @param dest Destination file path
 * @return 0 if file has been copied. -1 otherwise
 */
int copyfile(const char *source, const char *dest);