#include "uuidstr.h"
#include <windows.h>

bool uuidstr_random(uuidstr_t *dest) {
    UUID uuid;
    UuidCreate(&uuid);
    RPC_CSTR szUuid = NULL;
    if (UuidToString(&uuid, &szUuid) != RPC_S_OK) {
        return false;
    }
    strncpy((char *) dest, (char *) szUuid, sizeof(uuidstr_t));
    RpcStringFree(&szUuid);
    return true;
}