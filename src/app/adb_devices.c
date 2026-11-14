#include "adb_devices.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

static int valid_endpoint(const char *value) {
    char ip[INET_ADDRSTRLEN], extra;
    unsigned port;
    struct in_addr address;
    return sscanf(value, "%15[0-9.]:%u%c", ip, &port, &extra) == 2 &&
           port > 0 && port <= 65535 && inet_pton(AF_INET, ip, &address) == 1;
}

size_t adb_devices_load(const char *directory, adb_device_t devices[ADB_DEVICE_LIMIT]) {
    if (!directory || !directory[0]) return 0;
    char path[1024], line[512];
    snprintf(path, sizeof(path), "%s/adb_wireless_devices", directory);
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    size_t count = 0;
    while (count < ADB_DEVICE_LIMIT && fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;
        char *endpoint = strchr(line, '\t');
        if (!endpoint) continue;
        *endpoint++ = 0;
        char *name = strchr(endpoint, '\t');
        if (!name) continue;
        *name++ = 0;
        if (!line[0] || strlen(line) >= sizeof(devices[count].serial) ||
            !valid_endpoint(endpoint) || strlen(endpoint) >= sizeof(devices[count].endpoint) ||
            !name[0] || strlen(name) >= sizeof(devices[count].name)) continue;
        memcpy(devices[count].serial, line, strlen(line) + 1);
        memcpy(devices[count].endpoint, endpoint, strlen(endpoint) + 1);
        memcpy(devices[count].name, name, strlen(name) + 1);
        ++count;
    }
    fclose(file);
    return count;
}

int adb_devices_remember(const char *directory, const adb_device_t *device) {
    if (!directory || !directory[0] || !device->serial[0] || !device->name[0] ||
        !valid_endpoint(device->endpoint) || strpbrk(device->serial, "\t\r\n") ||
        strpbrk(device->name, "\t\r\n")) return -1;
    adb_device_t devices[ADB_DEVICE_LIMIT];
    size_t count = adb_devices_load(directory, devices), index;
    for (index = 0; index < count; ++index) if (!strcmp(devices[index].serial, device->serial)) break;
    if (index == ADB_DEVICE_LIMIT) return -1;
    devices[index] = *device;
    if (index == count) ++count;
    char path[1024], temporary[1032];
    snprintf(path, sizeof(path), "%s/adb_wireless_devices", directory);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE *file = fopen(temporary, "w");
    if (!file) return -1;
    int failed = 0;
    for (index = 0; index < count; ++index) {
        if (fprintf(file, "%s\t%s\t%s\n", devices[index].serial, devices[index].endpoint, devices[index].name) < 0) failed = 1;
    }
    if (fclose(file)) failed = 1;
    if (failed || rename(temporary, path)) { remove(temporary); return -1; }
    return 0;
}

int adb_devices_remove(const char *directory, const char *serial) {
    if (!directory || !directory[0] || !serial || !serial[0]) return -1;
    adb_device_t devices[ADB_DEVICE_LIMIT], kept[ADB_DEVICE_LIMIT];
    size_t count = adb_devices_load(directory, devices), kept_count = 0;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(devices[i].serial, serial)) kept[kept_count++] = devices[i];
    if (kept_count == count) return -1;
    char path[1024], temporary[1032];
    snprintf(path, sizeof(path), "%s/adb_wireless_devices", directory);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE *file = fopen(temporary, "w");
    if (!file) return -1;
    int failed = 0;
    for (size_t i = 0; i < kept_count; ++i)
        if (fprintf(file, "%s\t%s\t%s\n", kept[i].serial, kept[i].endpoint, kept[i].name) < 0) failed = 1;
    if (fclose(file)) failed = 1;
    if (failed || rename(temporary, path)) { remove(temporary); return -1; }
    return 0;
}
