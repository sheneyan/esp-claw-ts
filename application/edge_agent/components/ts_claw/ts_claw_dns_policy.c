#include "ts_claw_dns_policy.h"

#include "ts_claw_policy.h"

void ts_claw_dns_refresh(ts_claw_dns_reader_t reader,
                         void *reader_ctx,
                         size_t server_capacity,
                         ts_claw_dns_apply_t apply,
                         ts_claw_dns_refresh_result_t *result)
{
    ts_claw_dns_refresh_result_t next = {0};
    bool has_sta_resolver = false;
    bool has_exit_resolver = false;

    if (reader != NULL) {
        for (size_t i = 0; i < server_capacity; ++i) {
            ts_claw_dns_server_t server = {0};
            if (!reader(i, &server, reader_ctx)) {
                break;
            }
            if (!server.present || !server.ipv4 || server.host_order_ip == 0u) {
                continue;
            }

            if (ts_route_is_cgnat(server.host_order_ip)) {
                has_exit_resolver = true;
                continue;
            }
            if (ts_route_is_local_bypass(server.host_order_ip)) {
                has_sta_resolver = true;
                continue;
            }
            if (!ts_route_is_public_unicast(server.host_order_ip)) {
                continue;
            }

            has_sta_resolver = true;
            if (next.bypass_count >= TS_CLAW_DNS_BYPASS_MAX) {
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

    next.egress = has_sta_resolver && has_exit_resolver
                      ? TS_CLAW_DNS_EGRESS_MIXED
                      : has_sta_resolver
                            ? TS_CLAW_DNS_EGRESS_STA
                            : has_exit_resolver ? TS_CLAW_DNS_EGRESS_EXIT
                                                : TS_CLAW_DNS_EGRESS_UNAVAILABLE;

    if (apply != NULL) {
        apply(next.bypass, next.bypass_count);
    }
    if (result != NULL) {
        *result = next;
    }
}
