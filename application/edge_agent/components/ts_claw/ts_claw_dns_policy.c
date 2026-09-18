#include "ts_claw_dns_policy.h"

#include "ts_claw_policy.h"

void ts_claw_dns_refresh(ts_claw_dns_reader_t reader,
                         void *reader_ctx,
                         size_t server_capacity,
                         ts_claw_dns_apply_t apply,
                         ts_claw_dns_refresh_result_t *result)
{
    ts_claw_dns_refresh_result_t next = {0};

    if (reader != NULL) {
        for (size_t i = 0;
             i < server_capacity && next.bypass_count < TS_CLAW_DNS_BYPASS_MAX;
             ++i) {
            ts_claw_dns_server_t server = {0};
            if (!reader(i, &server, reader_ctx)) {
                break;
            }
            if (!server.present || !server.ipv4 || server.host_order_ip == 0u ||
                ts_route_is_cgnat(server.host_order_ip) ||
                !ts_route_is_public_unicast(server.host_order_ip)) {
                continue;
            }

            bool duplicate = false;
            for (size_t j = 0; j < next.bypass_count; ++j) {
                duplicate = duplicate || next.bypass[j] == server.host_order_ip;
            }
            if (!duplicate) {
                next.bypass[next.bypass_count++] = server.host_order_ip;
            }
        }
    }

    if (apply != NULL) {
        apply(next.bypass, next.bypass_count);
    }
    if (result != NULL) {
        *result = next;
    }
}
