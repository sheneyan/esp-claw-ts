#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TS_CLAW_MAX_DERP_RTTS 8
#define TS_CLAW_REGION_NAME_LEN 48
#define TS_CLAW_PEER_HOSTNAME_LEN 96

typedef struct {
    uint16_t region_id;
    char region_name[TS_CLAW_REGION_NAME_LEN];
    uint16_t rtt_ms;
    bool timed_out;
} ts_claw_derp_rtt_t;

#ifdef TS_CLAW_DIAGNOSTICS_INTERNAL
typedef struct {
    uint16_t region_id;
    uint16_t rtt_ms;
    const char *region_name;
} ts_claw_derp_rtt_source_t;

uint64_t ts_claw_timestamp_age_ms(uint64_t now_ms, uint64_t timestamp_ms);

size_t ts_claw_convert_derp_rtts(ts_claw_derp_rtt_t *out,
                                 size_t capacity,
                                 const ts_claw_derp_rtt_source_t *source,
                                 size_t source_count);
#endif
