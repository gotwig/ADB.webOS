#pragma once

#include <stdint.h>

#if __WIN32__
#include <winsock2.h>
#else

#include <arpa/inet.h>

#endif

typedef struct sockaddr sockaddr_t;

sockaddr_t *sockaddr_new();

#define sockaddr_free(addr) free(addr)

sockaddr_t *sockaddr_clone(const sockaddr_t *addr);

sockaddr_t *sockaddr_parse(const char *address) __attribute__((nonnull (1)));

int sockaddr_set_ip(sockaddr_t *addr, int family, const void *ip);

int sockaddr_set_ip_str(sockaddr_t *addr, int family, const char *ip_str);

int sockaddr_get_ip_str(const sockaddr_t *addr, char *dest, size_t len);

int sockaddr_set_port(sockaddr_t *addr, uint16_t port);

uint16_t sockaddr_get_port(const sockaddr_t *addr);

int sockaddr_to_string(const sockaddr_t *addr, char *dest, size_t len);

int sockaddr_compare(const sockaddr_t *a, const sockaddr_t *b);