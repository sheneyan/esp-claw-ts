#include "cap_tailscale_contract.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_CHECK(condition) assert(condition)
#define STATIC_ASSERT(name, condition) typedef char static_assert_##name[(condition) ? 1 : -1]

STATIC_ASSERT(derp_rtt_uses_uint16, sizeof(((cap_tailscale_derp_rtt_t *)0)->rtt_ms) == sizeof(uint16_t));
STATIC_ASSERT(derp_rtt_timeout_uses_bool,
              sizeof(((cap_tailscale_derp_rtt_t *)0)->timed_out) == sizeof(bool));
STATIC_ASSERT(status_hostname_capacity,
              sizeof(((cap_tailscale_status_t *)0)->hostname) == CAP_TAILSCALE_HOSTNAME_LEN);
STATIC_ASSERT(status_vpn_ip_capacity,
              sizeof(((cap_tailscale_status_t *)0)->vpn_ip) == CAP_TAILSCALE_IP_LEN);
STATIC_ASSERT(status_path_capacity, sizeof(((cap_tailscale_status_t *)0)->path) == 16);
STATIC_ASSERT(status_peer_count_uses_int,
              sizeof(((cap_tailscale_status_t *)0)->peer_count) == sizeof(int));
STATIC_ASSERT(status_peer_online_uses_int,
              sizeof(((cap_tailscale_status_t *)0)->peer_online) == sizeof(int));
STATIC_ASSERT(status_exit_node_capacity,
              sizeof(((cap_tailscale_status_t *)0)->exit_node) == CAP_TAILSCALE_IP_LEN);
STATIC_ASSERT(status_exit_state_capacity, sizeof(((cap_tailscale_status_t *)0)->exit_state) == 16);
STATIC_ASSERT(status_egress_capacity, sizeof(((cap_tailscale_status_t *)0)->egress) == 16);
STATIC_ASSERT(status_last_error_capacity,
              sizeof(((cap_tailscale_status_t *)0)->last_error) == CAP_TAILSCALE_ERROR_LEN);
STATIC_ASSERT(status_derp_rtt_capacity,
              sizeof(((cap_tailscale_status_t *)0)->derp_rtts) /
                  sizeof(((cap_tailscale_status_t *)0)->derp_rtts[0]) == CAP_TAILSCALE_MAX_DERP_RTTS);
STATIC_ASSERT(status_heartbeat_age_uses_uint64,
              sizeof(((cap_tailscale_status_t *)0)->derp_heartbeat_age_ms) == sizeof(uint64_t));
STATIC_ASSERT(status_control_age_uses_uint64,
              sizeof(((cap_tailscale_status_t *)0)->control_rx_age_ms) == sizeof(uint64_t));
STATIC_ASSERT(status_reconnect_counter_uses_uint32,
              sizeof(((cap_tailscale_status_t *)0)->reconnect_coord_watchdog) == sizeof(uint32_t));
STATIC_ASSERT(exit_node_ip_capacity,
              sizeof(((cap_tailscale_exit_node_t *)0)->ip) == CAP_TAILSCALE_IP_LEN);
STATIC_ASSERT(exit_node_hostname_capacity,
              sizeof(((cap_tailscale_exit_node_t *)0)->hostname) == CAP_TAILSCALE_HOSTNAME_LEN);
STATIC_ASSERT(mutation_error_capacity,
              sizeof(((cap_tailscale_mutation_result_t *)0)->error) == 32);
STATIC_ASSERT(mutation_message_capacity,
              sizeof(((cap_tailscale_mutation_result_t *)0)->message) == CAP_TAILSCALE_ERROR_LEN);
STATIC_ASSERT(mutation_selected_ip_capacity,
              sizeof(((cap_tailscale_mutation_result_t *)0)->selected_ip) == CAP_TAILSCALE_IP_LEN);
STATIC_ASSERT(mutation_selected_hostname_capacity,
              sizeof(((cap_tailscale_mutation_result_t *)0)->selected_hostname) == CAP_TAILSCALE_HOSTNAME_LEN);
STATIC_ASSERT(mutation_exit_state_capacity,
              sizeof(((cap_tailscale_mutation_result_t *)0)->exit_state) == 16);
STATIC_ASSERT(mutation_egress_capacity,
              sizeof(((cap_tailscale_mutation_result_t *)0)->egress) == 16);

static esp_err_t expected_get_status(cap_tailscale_status_t *out, void *ctx)
{
    (void)out;
    (void)ctx;
    return ESP_OK;
}

static int expected_list_exit_nodes(cap_tailscale_exit_node_t *out, size_t capacity, void *ctx)
{
    (void)out;
    (void)capacity;
    (void)ctx;
    return 0;
}

static esp_err_t expected_set_exit_node(const char *selector,
                                        cap_tailscale_mutation_result_t *out,
                                        void *ctx)
{
    (void)selector;
    (void)out;
    (void)ctx;
    return ESP_OK;
}

static esp_err_t expected_clear_exit_node(cap_tailscale_mutation_result_t *out, void *ctx)
{
    (void)out;
    (void)ctx;
    return ESP_OK;
}

static esp_err_t expected_reconnect(cap_tailscale_status_t *out, void *ctx)
{
    (void)out;
    (void)ctx;
    return ESP_OK;
}

static void test_public_contract_shape(void)
{
    cap_tailscale_status_t status = {0};
    cap_tailscale_exit_node_t exit_node = {0};
    cap_tailscale_mutation_result_t mutation = {0};
    cap_tailscale_provider_t provider = {
        .get_status = expected_get_status,
        .list_exit_nodes = expected_list_exit_nodes,
        .set_exit_node = expected_set_exit_node,
        .clear_exit_node = expected_clear_exit_node,
        .reconnect = expected_reconnect,
        .ctx = NULL,
    };

    status.enabled = true;
    status.connected = true;
    status.hostname[0] = 'n';
    status.vpn_ip[0] = '1';
    status.path[0] = 'd';
    status.peer_count = 1;
    status.peer_online = 1;
    status.exit_node[0] = '1';
    status.exit_state[0] = 'o';
    status.egress[0] = 'd';
    status.last_error[0] = '\0';
    status.derp_active.id = 1;
    status.derp_default.id = 2;
    status.derp_rtts[0].region.id = 1;
    status.derp_rtts[0].rtt_ms = 1;
    status.derp_rtts[0].timed_out = false;
    status.derp_rtt_count = 1;
    status.derp_heartbeat_age_ms = 1;
    status.control_rx_age_ms = 1;
    status.reconnect_coord_watchdog = 1;
    status.reconnect_coord_transport = 1;
    status.reconnect_derp_watchdog = 1;
    status.reconnect_derp_retry = 1;
    exit_node.ip[0] = '1';
    exit_node.hostname[0] = 'n';
    exit_node.online = true;
    exit_node.direct = true;
    exit_node.derp_region.id = 1;
    mutation.ok = true;
    mutation.message[0] = 'o';
    mutation.selected_ip[0] = '1';
    mutation.selected_hostname[0] = 'n';
    mutation.persisted = true;

    TEST_CHECK(provider.get_status != NULL);
    TEST_CHECK(provider.list_exit_nodes != NULL);
    TEST_CHECK(provider.reconnect != NULL);
    TEST_CHECK(status.enabled && exit_node.direct && mutation.persisted);
}

static void test_read_and_mutation_validation(void)
{
    TEST_CHECK(cap_tailscale_validate_read_args(0) == ESP_OK);
    TEST_CHECK(cap_tailscale_validate_read_args(1) == ESP_ERR_INVALID_ARG);

    TEST_CHECK(cap_tailscale_validate_mutation_args(true, true, 0) == ESP_OK);
    TEST_CHECK(cap_tailscale_validate_mutation_args(true, false, 0) == ESP_ERR_INVALID_STATE);
    TEST_CHECK(cap_tailscale_validate_mutation_args(true, true, 1) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_mutation_args(false, true, 0) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_mutation_args(true, true, 2) == ESP_ERR_INVALID_ARG);

    TEST_CHECK(cap_tailscale_validate_mutation_confirmation(false, true, true, 0) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_mutation_confirmation(true, false, true, 0) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_mutation_confirmation(true, true, false, 0) == ESP_ERR_INVALID_STATE);
}

static void test_selector_normalization_and_cgnat_range(void)
{
    char selector[CAP_TAILSCALE_IP_LEN];
    char too_long[CAP_TAILSCALE_IP_LEN + 1];
    char oversized[CAP_TAILSCALE_HOSTNAME_LEN + 1];
    char hostname_selector[CAP_TAILSCALE_HOSTNAME_LEN];

    TEST_CHECK(cap_tailscale_normalize_selector(NULL, selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector("", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector(" \t ", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector("  100.64.0.1\t", selector, sizeof(selector)) == ESP_OK);
    TEST_CHECK(strcmp(selector, "100.64.0.1") == 0);

    memset(too_long, '1', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    TEST_CHECK(cap_tailscale_normalize_selector(too_long, selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    TEST_CHECK(cap_tailscale_normalize_selector(oversized, hostname_selector, sizeof(hostname_selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector("100.64.0.1", selector, 1) == ESP_ERR_INVALID_ARG);

    TEST_CHECK(cap_tailscale_selector_is_cgnat("100.64.0.1"));
    TEST_CHECK(cap_tailscale_selector_is_cgnat("100.127.255.254"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.128.0.1"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("192.168.1.1"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.64.0"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.64.0.1x"));
}

static void test_count_bounds(void)
{
    TEST_CHECK(cap_tailscale_bound_exit_node_count(0) == 0);
    TEST_CHECK(cap_tailscale_bound_exit_node_count(CAP_TAILSCALE_MAX_EXIT_NODES) == CAP_TAILSCALE_MAX_EXIT_NODES);
    TEST_CHECK(cap_tailscale_bound_exit_node_count(CAP_TAILSCALE_MAX_EXIT_NODES + 1) == CAP_TAILSCALE_MAX_EXIT_NODES);
    TEST_CHECK(cap_tailscale_bound_derp_rtt_count(0) == 0);
    TEST_CHECK(cap_tailscale_bound_derp_rtt_count(CAP_TAILSCALE_MAX_DERP_RTTS) == CAP_TAILSCALE_MAX_DERP_RTTS);
    TEST_CHECK(cap_tailscale_bound_derp_rtt_count(CAP_TAILSCALE_MAX_DERP_RTTS + 1) == CAP_TAILSCALE_MAX_DERP_RTTS);
}

int main(void)
{
    test_public_contract_shape();
    test_read_and_mutation_validation();
    test_selector_normalization_and_cgnat_range();
    test_count_bounds();

    puts("cap_tailscale_contract: all tests passed");
    return 0;
}
