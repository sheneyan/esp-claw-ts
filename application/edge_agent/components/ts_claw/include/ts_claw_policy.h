#ifndef TS_CLAW_POLICY_H
#define TS_CLAW_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* Delegate special destinations such as loopback to lwIP's native route. */
    TS_ROUTE_DEFAULT,
    TS_ROUTE_STA,
    TS_ROUTE_WG,
} ts_route_target_t;

typedef enum {
    TS_EXIT_DISABLED,
    TS_EXIT_PENDING,
    TS_EXIT_ACTIVE,
    TS_EXIT_FALLBACK,
} ts_exit_state_t;

typedef struct {
    ts_exit_state_t state;
    unsigned int consecutive_successes;
    unsigned int consecutive_failures;
    bool configured;
    bool tunnel_up;
} ts_exit_policy_t;

typedef enum {
    TS_RESOURCE_GUARD_NONE,
    TS_RESOURCE_GUARD_STOP,
    TS_RESOURCE_GUARD_RETRY,
} ts_resource_guard_action_t;

typedef struct {
    uint64_t stopped_at_ms;
    unsigned int consecutive_low_samples;
    bool stopped;
} ts_resource_guard_t;

void ts_exit_policy_init(ts_exit_policy_t *policy, bool configured);
void ts_exit_policy_set_tunnel(ts_exit_policy_t *policy, bool tunnel_up);
void ts_exit_policy_on_probe(ts_exit_policy_t *policy, bool success);
bool ts_exit_policy_routes_public(const ts_exit_policy_t *policy);

/*
 * IP inputs are in CPU host byte order. Callers using lwIP's
 * ip4_addr_get_u32() must apply lwip_ntohl() before calling these functions.
 */
bool ts_route_is_cgnat(uint32_t host_order_ip);
bool ts_route_is_private(uint32_t host_order_ip);
bool ts_route_is_loopback(uint32_t host_order_ip);
bool ts_route_is_local_bypass(uint32_t host_order_ip);
bool ts_route_is_public_unicast(uint32_t host_order_ip);
ts_route_target_t ts_route_classify(uint32_t host_order_ip, bool exit_active);
uint32_t ts_exit_probe_interface_binding(void);

void ts_resource_guard_init(ts_resource_guard_t *guard);
ts_resource_guard_action_t ts_resource_guard_sample(ts_resource_guard_t *guard,
                                                    uint64_t now_ms,
                                                    size_t internal_free,
                                                    size_t internal_largest);
void ts_resource_guard_retry_completed(ts_resource_guard_t *guard,
                                       uint64_t now_ms,
                                       bool succeeded);

uint64_t ts_start_retry_schedule(uint64_t now_ms);
bool ts_start_retry_due(uint64_t next_retry_ms, uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
