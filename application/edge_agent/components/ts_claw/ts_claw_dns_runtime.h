#pragma once

#include "ts_claw_dns_policy.h"

typedef struct {
    ts_claw_dns_egress_t egress;
    bool bypass_active;
    uint8_t bypass_count;
    uint32_t bypass[TS_CLAW_DNS_BYPASS_MAX];
} ts_claw_dns_runtime_result_t;

void ts_claw_dns_runtime_refresh(bool exit_active,
                                 ts_claw_dns_apply_t apply,
                                 ts_claw_dns_runtime_result_t *result);
const char *ts_claw_dns_egress_name(ts_claw_dns_egress_t egress);
