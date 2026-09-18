#pragma once

#include "esp_err.h"

#include <stdint.h>

#define ESP_IPADDR_TYPE_V4 0u
#define ESP_IPADDR_TYPE_V6 6u

typedef struct esp_netif_obj {
    int marker;
} esp_netif_t;

typedef enum {
    ESP_NETIF_DNS_MAIN = 0,
    ESP_NETIF_DNS_BACKUP,
    ESP_NETIF_DNS_FALLBACK,
    ESP_NETIF_DNS_MAX,
} esp_netif_dns_type_t;

typedef struct {
    union {
        struct {
            uint32_t addr;
        } ip4;
        struct {
            uint32_t addr[4];
        } ip6;
    } u_addr;
    uint8_t type;
} esp_ip_addr_t;

typedef struct {
    esp_ip_addr_t ip;
} esp_netif_dns_info_t;

esp_err_t esp_netif_get_dns_info(esp_netif_t *esp_netif,
                                 esp_netif_dns_type_t type,
                                 esp_netif_dns_info_t *dns);
