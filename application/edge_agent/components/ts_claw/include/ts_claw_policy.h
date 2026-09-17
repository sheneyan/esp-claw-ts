#ifndef TS_CLAW_POLICY_H
#define TS_CLAW_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
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

void ts_exit_policy_init(ts_exit_policy_t *policy, bool configured);
void ts_exit_policy_set_tunnel(ts_exit_policy_t *policy, bool tunnel_up);
void ts_exit_policy_on_probe(ts_exit_policy_t *policy, bool success);
bool ts_exit_policy_routes_public(const ts_exit_policy_t *policy);

bool ts_route_is_cgnat(uint32_t ipv4);
bool ts_route_is_private(uint32_t ipv4);
ts_route_target_t ts_route_classify(uint32_t ipv4, bool exit_active);

#ifdef __cplusplus
}
#endif

#endif
