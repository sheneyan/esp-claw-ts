#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TS_CLAW_DNS_BYPASS_MAX 3u

typedef struct {
    bool present;
    bool ipv4;
    uint32_t host_order_ip;
} ts_claw_dns_server_t;

typedef bool (*ts_claw_dns_reader_t)(size_t index,
                                     ts_claw_dns_server_t *server,
                                     void *ctx);
typedef void (*ts_claw_dns_apply_t)(const uint32_t *host_order_ips,
                                    size_t count);

typedef struct {
    uint32_t bypass[TS_CLAW_DNS_BYPASS_MAX];
    size_t bypass_count;
} ts_claw_dns_refresh_result_t;

void ts_claw_dns_refresh(ts_claw_dns_reader_t reader,
                         void *reader_ctx,
                         size_t server_capacity,
                         ts_claw_dns_apply_t apply,
                         ts_claw_dns_refresh_result_t *result);
