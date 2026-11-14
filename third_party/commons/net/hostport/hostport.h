#pragma once

#include "sockaddr.h"

typedef struct hostport hostport_t;

/**
 * Create a new host.
 * @param hostname Hostname.
 * @param port Port.
 * @return New host. NULL if the hostname isn't valid.
 */
hostport_t *hostport_new(const char *hostname, uint16_t port) __attribute__((nonnull (1)));

/**
 * Parse a host string.
 * @param s Host string. Format: hostname[:port]
 * @return Parsed host or NULL if the string is invalid.
 */
hostport_t *hostport_parse(const char *s) __attribute__((nonnull (1)));

void hostport_free(hostport_t *host) __attribute__((nonnull (1)));

const char *hostport_get_hostname(const hostport_t *host) __attribute__((nonnull (1)));

uint16_t hostport_get_port(const hostport_t *host) __attribute__((nonnull (1)));

int hostport_to_string(const hostport_t *host, char *buf, size_t len) __attribute__((nonnull (1, 2)));

int hostport_is_ip(const hostport_t *host) __attribute__((nonnull (1)));