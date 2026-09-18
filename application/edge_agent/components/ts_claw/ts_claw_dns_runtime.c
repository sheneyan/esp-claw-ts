#include "ts_claw_dns_runtime.h"

#include "esp_netif.h"
#include "lwip/ip4_addr.h"

#include <string.h>

static bool read_sta_dns_server(size_t index,
                                ts_claw_dns_server_t *server,
                                void *ctx)
{
    esp_netif_t *sta_netif = ctx;
    if (server == NULL || sta_netif == NULL ||
        index >= (size_t)ESP_NETIF_DNS_MAX) {
        return false;
    }

    esp_netif_dns_info_t dns = {0};
    if (esp_netif_get_dns_info(sta_netif, (esp_netif_dns_type_t)index,
                               &dns) != ESP_OK) {
        return true;
    }
    server->present = true;
    server->ipv4 = dns.ip.type == ESP_IPADDR_TYPE_V4;
    server->host_order_ip = server->ipv4
                                ? lwip_ntohl(dns.ip.u_addr.ip4.addr)
                                : 0u;
    return true;
}

void ts_claw_dns_runtime_refresh(esp_netif_t *sta_netif,
                                 bool exit_active,
                                 ts_claw_dns_apply_t apply,
                                 ts_claw_dns_runtime_result_t *result)
{
    ts_claw_dns_runtime_result_t next = {
        .egress = exit_active ? TS_CLAW_DNS_EGRESS_UNAVAILABLE
                              : TS_CLAW_DNS_EGRESS_STA,
    };

    if (exit_active && sta_netif != NULL) {
        ts_claw_dns_refresh_result_t captured = {0};
        ts_claw_dns_refresh(read_sta_dns_server, sta_netif,
                            (size_t)ESP_NETIF_DNS_MAX,
                            apply, &captured);
        next.egress = captured.egress;
        next.bypass_count = (uint8_t)captured.bypass_count;
        next.bypass_active = captured.bypass_count > 0u;
        memcpy(next.bypass, captured.bypass, sizeof(next.bypass));
    } else if (apply != NULL) {
        apply(NULL, 0u);
    }

    if (result != NULL) {
        *result = next;
    }
}

const char *ts_claw_dns_egress_name(ts_claw_dns_egress_t egress)
{
    switch (egress) {
    case TS_CLAW_DNS_EGRESS_STA:
        return "sta";
    case TS_CLAW_DNS_EGRESS_EXIT:
        return "exit";
    case TS_CLAW_DNS_EGRESS_MIXED:
        return "mixed";
    case TS_CLAW_DNS_EGRESS_UNAVAILABLE:
    default:
        return "unavailable";
    }
}
