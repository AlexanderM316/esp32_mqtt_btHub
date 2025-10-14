#ifndef device_manager_H
#define device_manager_H

#include "lwip/ip4_addr.h"

typedef struct dns_server dns_server_t;

dns_server_t* dnsserver_new(void);
void dnsserver_set_ip(dns_server_t* server, ip4_addr_t ip);
void dnsserver_start(dns_server_t* server);
void dnsserver_stop(dns_server_t* server);
void dnsserver_free(dns_server_t* server);

#endif // device_manager_H