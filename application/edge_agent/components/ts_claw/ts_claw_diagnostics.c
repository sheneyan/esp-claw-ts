#define TS_CLAW_DIAGNOSTICS_INTERNAL
#include "ts_claw_diagnostics.h"

#include <string.h>

uint64_t ts_claw_timestamp_age_ms(uint64_t now_ms, uint64_t timestamp_ms)
{
    if (timestamp_ms == 0u || timestamp_ms >= now_ms) {
        return 0u;
    }
    return now_ms - timestamp_ms;
}

size_t ts_claw_convert_derp_rtts(ts_claw_derp_rtt_t *out,
                                 size_t capacity,
                                 const ts_claw_derp_rtt_source_t *source,
                                 size_t source_count)
{
    if (out == NULL || source == NULL || capacity == 0u) {
        return 0u;
    }

    size_t count = source_count;
    if (count > capacity) {
        count = capacity;
    }
    if (count > TS_CLAW_MAX_DERP_RTTS) {
        count = TS_CLAW_MAX_DERP_RTTS;
    }

    memset(out, 0, count * sizeof(*out));
    for (size_t i = 0; i < count; ++i) {
        out[i].region_id = source[i].region_id;
        out[i].rtt_ms = source[i].rtt_ms;
        out[i].timed_out = source[i].rtt_ms == 0u;
        if (source[i].region_name != NULL) {
            strncpy(out[i].region_name, source[i].region_name,
                    sizeof(out[i].region_name) - 1u);
            out[i].region_name[sizeof(out[i].region_name) - 1u] = '\0';
        }
    }
    return count;
}
