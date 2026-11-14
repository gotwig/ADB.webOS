#include <assert.h>
#include <string.h>

#include "hostport.h"

void test_host_parse() {
    hostport_t *host;

    host = hostport_parse("www.google.com:80");
    assert(host != NULL);
    assert(strcmp(hostport_get_hostname(host), "www.google.com") == 0);
    assert(hostport_get_port(host) == 80);
    hostport_free(host);

    assert(hostport_parse("www.google.com:") == NULL);
}

void test_host_new() {
    hostport_t *host = hostport_new("www.google.com", 80);
    assert(host != NULL);
    assert(strcmp(hostport_get_hostname(host), "www.google.com") == 0);
    hostport_free(host);

    host = hostport_new("192.168.1.1", 442);
    assert(host != NULL);
    assert(hostport_is_ip(host) == 1);
    hostport_free(host);
}

int main() {
    test_host_parse();
    test_host_new();
}