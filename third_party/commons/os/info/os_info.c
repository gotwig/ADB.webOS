#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "os_info.h"

void os_info_clear(os_info_t *info) {
    if (info->name != NULL) {
        free(info->name);
    }
    if (info->extras != NULL) {
        free(info->extras);
    }
}

char *os_info_str(const os_info_t *info) {
    const char *name = info->name;
    if (name == NULL) {
        name = "Unknown";
    }
    if (!version_info_valid(&info->version) && info->extras == NULL) {
        return strdup(name);
    } else if (info->extras == NULL) {
        // Version info is valid
        char *version_str = version_info_str(&info->version);
        assert(version_str != NULL);
        size_t len = strlen(name) + 1 + strlen(version_str) + 1;
        char *result = malloc(len);
        snprintf(result, len, "%s %s", name, version_str);
        free(version_str);
        return result;
    } else if (!version_info_valid(&info->version)) {
        // Extra info is present
        size_t len = strlen(name) + 2 + strlen(info->extras) + 2;
        char *result = malloc(len);
        snprintf(result, len, "%s (%s)", name, info->extras);
        return result;
    } else {
        char *version_str = version_info_str(&info->version);
        assert(version_str != NULL);
        size_t len = strlen(name) + 1 + strlen(version_str) + 2 + strlen(info->extras) + 2;
        char *result = malloc(len);
        snprintf(result, len, "%s %s (%s)", name, version_str, info->extras);
        free(version_str);
        return result;
    }
}