#include <string.h>
#include <sys/utsname.h>
#include "os_info.h"

int os_info_get(os_info_t *info) {
    memset(info, 0, sizeof(*info));
    struct utsname u;
    if (uname(&u) != 0) {
        return -1;
    }
    info->name = strdup(u.sysname);
    if (version_info_parse(&info->version, u.release) != 0) {
        info->version.major = -1;
        info->version.minor = -1;
        info->version.patch = -1;
    }
    return 0;
}

