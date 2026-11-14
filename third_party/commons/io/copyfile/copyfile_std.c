#include "copyfile.h"

#include <stdio.h>

#define BUFFER_SIZE 8192

/**
 * @param source Source file path
 * @param dest Destination file path
 * @return 0 if file has been copied. -1 otherwise
 */
int copyfile(const char *source, const char *dest) {
    FILE *src_f = fopen(source, "rb");
    if (src_f == NULL) {
        return -1;
    }
    FILE *dest_f = fopen(dest, "wb");
    if (dest_f == NULL) {
        fclose(src_f);
        return -1;
    }
    int ret = 0;
    unsigned char buf[BUFFER_SIZE];
    size_t n;
    while ((n = fread(buf, 1, BUFFER_SIZE, dest_f)) > 0) {
        if (fwrite(buf, 1, n, dest_f) < n) {
            ret = -1;
            break;
        }
    }
    fclose(dest_f);
    fclose(src_f);
    return ret;
}