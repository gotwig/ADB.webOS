#include "sockaddr.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#if __WIN32__
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#endif

struct sockaddr *sockaddr_new() {
    return calloc(1, sizeof(struct sockaddr_storage));
}

sockaddr_t *sockaddr_clone(const sockaddr_t *addr) {
    sockaddr_t *new_addr = sockaddr_new();
    memcpy(new_addr, addr, sizeof(sockaddr_t));
    return new_addr;
}

sockaddr_t *sockaddr_parse(const char *address) {
    assert(address != NULL);
    char buf[128];
    strncpy(buf, address, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int af = AF_UNSPEC;
    char *addr_start = NULL, *addr_end = NULL, *port_start = NULL;
    if (buf[0] == '[') {
        af = AF_INET6;
        addr_start = buf + 1;
        addr_end = strchr(addr_start, ']');
        if (addr_end == NULL) {
            return NULL;
        }
        *addr_end = '\0';
        port_start = strchr(addr_end + 1, ':');
        if (port_start != NULL) {
            *port_start = '\0';
            port_start++;
        }
    } else {
        af = AF_INET;
        addr_start = buf;
        addr_end = strchr(addr_start, ':');
        if (addr_end != NULL) {
            *addr_end = '\0';
            port_start = addr_end + 1;
        }
    }
    sockaddr_t *addr = sockaddr_new();
    assert(addr != NULL);
    if (sockaddr_set_ip_str(addr, af, addr_start)) {
        free(addr);
        return NULL;
    }
    if (port_start != NULL) {
        char *endptr = NULL;
        uint16_t port = (uint16_t) strtol(port_start, &endptr, 10);
        if (endptr == NULL || *endptr != '\0') {
            free(addr);
            return NULL;
        }
        sockaddr_set_port(addr, port);
    }
    return addr;
}

int sockaddr_set_ip(sockaddr_t *addr, int family, const void *ip) {
    switch (family) {
        case AF_INET: {
            struct sockaddr_in *addr4 = (struct sockaddr_in *) addr;
            addr4->sin_family = AF_INET;
            addr4->sin_addr = *(struct in_addr *) ip;
            return 0;
        }
        case AF_INET6: {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) addr;
            addr6->sin6_family = AF_INET6;
            addr6->sin6_addr = *(struct in6_addr *) ip;
            return 0;
        }
        default:
            return -1;
    }
}

int sockaddr_set_ip_str(sockaddr_t *addr, int family, const char *ip_str) {
    assert(ip_str != NULL);
    switch (family) {
        case AF_INET: {
            struct sockaddr_in *addr4 = (struct sockaddr_in *) addr;
            addr4->sin_family = AF_INET;
            return inet_pton(AF_INET, ip_str, &addr4->sin_addr) == 1 ? 0 : -1;
        }
        case AF_INET6: {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) addr;
            addr6->sin6_family = AF_INET6;
            return inet_pton(AF_INET6, ip_str, &addr6->sin6_addr) == 1 ? 0 : -1;
        }
        default:
            return -1;
    }
}

int sockaddr_get_ip_str(const sockaddr_t *addr, char *dest, size_t len) {
    assert(dest != NULL);
    assert(len > 0);
    if (addr == NULL) {
        if (len < 5) {
            return -1;
        }
        strncpy(dest, "NULL", len);
        return 0;
    }
    switch (addr->sa_family) {
        case AF_INET: {
            const struct sockaddr_in *addr4 = (const struct sockaddr_in *) addr;
            return inet_ntop(AF_INET, &addr4->sin_addr, dest, len) == NULL ? -1 : 0;
        }
        case AF_INET6: {
            const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *) addr;
            return inet_ntop(AF_INET6, &addr6->sin6_addr, dest, len) == NULL ? -1 : 0;
        }
        default:
            return -1;
    }
}

int sockaddr_set_port(sockaddr_t *addr, uint16_t port) {
    assert(addr != NULL);
    struct sockaddr_in *addr4 = (struct sockaddr_in *) addr;
    addr4->sin_port = htons(port);
    return 0;
}

uint16_t sockaddr_get_port(const sockaddr_t *addr) {
    const struct sockaddr_in *addr4 = (const struct sockaddr_in *) addr;
    return ntohs(addr4->sin_port);
}


int sockaddr_to_string(const sockaddr_t *addr, char *dest, size_t len) {
    assert(dest != NULL);
    assert(len > 0);
    if (addr == NULL) {
        return sockaddr_get_ip_str(addr, dest, len);
    }
    char ip[64];
    if (sockaddr_get_ip_str(addr, ip, sizeof(ip)) != 0) {
        return -1;
    }
    uint16_t port = sockaddr_get_port(addr);
    if (addr->sa_family == AF_INET6) {
        if (port == 0) {
            snprintf(dest, len, "[%s]", ip);
        } else {
            snprintf(dest, len, "[%s]:%d", ip, port);
        }
    } else if (port == 0) {
        snprintf(dest, len, "%s", ip);
    } else {
        snprintf(dest, len, "%s:%d", ip, port);
    }
    return 0;
}

int sockaddr_compare(const sockaddr_t *a, const sockaddr_t *b) {
    return memcmp(a, b, sizeof(sockaddr_t));
}