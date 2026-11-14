#pragma once
#include <stddef.h>

#define ADB_DEVICE_LIMIT 32

typedef struct {
    char serial[128];
    char endpoint[64];
    char name[128];
} adb_device_t;

size_t adb_devices_load(const char *directory, adb_device_t devices[ADB_DEVICE_LIMIT]);
int adb_devices_remember(const char *directory, const adb_device_t *device);
int adb_devices_remove(const char *directory, const char *serial);
