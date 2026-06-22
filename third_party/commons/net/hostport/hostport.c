#include "hostport.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

struct hostport {
    char hostname[254];
    uint16_t port;
    sockaddr_t *addr;
};

static const char *find_hostname_end(const char *s);

hostport_t *hostport_new(const char *hostname, uint16_t port) {
    hostport_t *host = hostport_parse(hostname);
    if (host == NULL) {
        return NULL;
    }
    host->port = port;
    return host;
}

hostport_t *hostport_parse(const char *s) {
    sockaddr_t *parse = sockaddr_parse(s);
    if (parse != NULL) {
        hostport_t *host = calloc(1, sizeof(hostport_t));
        strncpy(host->hostname, s, 254);
        host->port = sockaddr_get_port(parse);
        sockaddr_get_ip_str(parse, host->hostname, 253);
        host->addr = parse;
        return host;
    }
    const char *end = find_hostname_end(s);
    if (end == NULL) {
        return NULL;
    }
    uint16_t port = 0;
    if (*end == ':') {
        char *leftover = NULL;
        port = strtoul(end + 1, &leftover, 10);
        if (leftover == end + 1) {
            // Port number is invalid
            return NULL;
        }
    } else if (*end != '\0') {
        // Invalid character
        return NULL;
    }
    hostport_t *host = calloc(1, sizeof(hostport_t));
    strncpy(host->hostname, s, end - s);
    host->port = port;
    return host;
}

void hostport_free(hostport_t *host) {
    if (host->addr != NULL) {
        sockaddr_free(host->addr);
    }
    free(host);
}

const char *hostport_get_hostname(const hostport_t *host) {
    return host->hostname;
}

uint16_t hostport_get_port(const hostport_t *host) {
    return host->port;
}

int hostport_to_string(const hostport_t *host, char *buf, size_t len) {
    return snprintf(buf, len, "%s:%d", host->hostname, host->port);
}

int hostport_is_ip(const hostport_t *host) {
    return host->addr != NULL;
}

const char *find_hostname_end(const char *s) {
    const char *p = s;
    // One segment can have at most 63 characters, and at most 253 characters in total.
    int seglen = 0;
    for (char c = *p; c != '\0' && p - s < 253; c = *(++p)) {
        if (c == '.') {
            if (seglen == 0) {
                return NULL;
            }
            seglen = 0;
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') {
            seglen++;
            if (seglen > 63) {
                // A segment can't have more than 63 characters.
                return NULL;
            }
        } else {
            // Invalid character.
            break;
        }
    }
    return p;
}
