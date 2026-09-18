#include "ts_claw_dns_runtime.h"

#include "lwip/dns.h"
#include "lwip/ip_addr.h"

#include <string.h>

static bool read_lwip_dns_server(size_t index,
                                 ts_claw_dns_server_t *server,
                                 void *ctx)
{
    (void)ctx;
    if (server == NULL || index >= DNS_MAX_SERVERS) {
        return false;
    }

    const ip_addr_t *address = dns_getserver((u8_t)index);
    server->present = address != NULL;
    server->ipv4 = address != NULL && IP_IS_V4(address);
    server->host_order_ip = server->ipv4
                                ? lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(address)))
                                : 0u;
    return true;
}

void ts_claw_dns_runtime_refresh(bool exit_active,
                                 ts_claw_dns_apply_t apply,
                                 ts_claw_dns_runtime_result_t *result)
{
    ts_claw_dns_runtime_result_t next = {
        .egress = exit_active ? TS_CLAW_DNS_EGRESS_UNAVAILABLE
                              : TS_CLAW_DNS_EGRESS_STA,
    };

    if (exit_active) {
        ts_claw_dns_refresh_result_t captured = {0};
        ts_claw_dns_refresh(read_lwip_dns_server, NULL, DNS_MAX_SERVERS,
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
