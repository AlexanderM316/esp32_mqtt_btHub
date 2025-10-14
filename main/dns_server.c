#include "dns_server.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define DNS_PORT 53
static const char* TAG = "DNS SERVER";

struct dns_server {
    ip4_addr_t redirect_ip;
    int sock;
    TaskHandle_t task;
    bool running;
};

static void dnsserver_task(void* arg)
{
    dns_server_t* server = (dns_server_t*)arg;
    uint8_t buf[512];
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    ESP_LOGI(TAG, "listening on port %d", DNS_PORT);

    while (server->running) {
        int len = recvfrom(server->sock, buf, sizeof(buf), 0,
                           (struct sockaddr*)&src_addr, &addr_len);
        if (len < 12) continue; // too short for DNS header

        uint8_t resp[512];
        memcpy(resp, buf, len);

        // Set flags: response, recursion available, no error
        resp[2] |= 0x80; // QR = response
        resp[3] |= 0x80; // RA = 1
        resp[7] = 1;     // ANCOUNT = 1

        // Find end of question (QNAME)
        int qname_end = 12;
        while (qname_end < len && buf[qname_end]) qname_end += buf[qname_end] + 1;
        qname_end++; // skip null terminator

        int answer_start = qname_end + 4; // QTYPE + QCLASS
        memcpy(&resp[answer_start], "\xC0\x0C\x00\x01\x00\x01\x00\x00\x00\x3C\x00\x04", 12);

        // Write redirect IP
        resp[answer_start + 12] = ip4_addr1(&server->redirect_ip);
        resp[answer_start + 13] = ip4_addr2(&server->redirect_ip);
        resp[answer_start + 14] = ip4_addr3(&server->redirect_ip);
        resp[answer_start + 15] = ip4_addr4(&server->redirect_ip);

        int resp_len = answer_start + 16;

        sendto(server->sock, resp, resp_len, 0,
               (struct sockaddr*)&src_addr, addr_len);
    }

    ESP_LOGI(TAG, "stopped");
    vTaskDelete(NULL);
}

dns_server_t* dnsserver_new(void)
{
    dns_server_t* s = calloc(1, sizeof(dns_server_t));
    IP4_ADDR(&s->redirect_ip, 192, 168, 4, 1);
    s->sock = -1;
    return s;
}

void dnsserver_set_ip(dns_server_t* server, ip4_addr_t ip)
{
    server->redirect_ip = ip;
}

void dnsserver_start(dns_server_t* server)
{
    if (server->running) return;

    server->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server->sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DNS_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(server->sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket");
        close(server->sock);
        server->sock = -1;
        return;
    }

    server->running = true;
    xTaskCreate(dnsserver_task, "dnsserver_task", 4096, server, 5, &server->task);
}

void dnsserver_stop(dns_server_t* server)
{
    if (!server->running) return;
    server->running = false;
    shutdown(server->sock, SHUT_RDWR);
    close(server->sock);
    server->sock = -1;
}

void dnsserver_free(dns_server_t* server)
{
    if (!server) return;
    dnsserver_stop(server);
    free(server);
}
