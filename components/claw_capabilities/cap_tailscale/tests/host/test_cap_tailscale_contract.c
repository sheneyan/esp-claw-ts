#include "cap_tailscale_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_check(bool condition, const char *expression, const char *file, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: test failed: %s\n", file, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define TEST_CHECK(condition) test_check((condition), #condition, __FILE__, __LINE__)
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
    char max_length_selector[CAP_TAILSCALE_HOSTNAME_LEN];
    char in_place_selector[] = " \t100.64.0.1 \n";
    char overlapping_selector[] = "  node.example  ";

    TEST_CHECK(cap_tailscale_normalize_selector(NULL, selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector("node", NULL, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector("node", selector, 0) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector("", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector(" \t ", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector("  100.64.0.1\t", selector, sizeof(selector)) == ESP_OK);
    TEST_CHECK(strcmp(selector, "100.64.0.1") == 0);
    TEST_CHECK(selector[strlen("100.64.0.1")] == '\0');

    TEST_CHECK(cap_tailscale_normalize_selector(in_place_selector,
                                                 in_place_selector,
                                                 sizeof(in_place_selector)) == ESP_OK);
    TEST_CHECK(strcmp(in_place_selector, "100.64.0.1") == 0);
    TEST_CHECK(in_place_selector[strlen("100.64.0.1")] == '\0');

    TEST_CHECK(cap_tailscale_normalize_selector(overlapping_selector,
                                                 overlapping_selector + 1,
                                                 sizeof(overlapping_selector) - 1) == ESP_OK);
    TEST_CHECK(strcmp(overlapping_selector + 1, "node.example") == 0);
    TEST_CHECK(overlapping_selector[1 + strlen("node.example")] == '\0');

    memset(too_long, '1', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    TEST_CHECK(cap_tailscale_normalize_selector(too_long, selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    TEST_CHECK(cap_tailscale_normalize_selector(oversized, hostname_selector, sizeof(hostname_selector)) == ESP_ERR_INVALID_ARG);
    memset(max_length_selector, 'a', sizeof(max_length_selector) - 1);
    max_length_selector[sizeof(max_length_selector) - 1] = '\0';
    TEST_CHECK(cap_tailscale_normalize_selector(max_length_selector,
                                                 hostname_selector,
                                                 sizeof(hostname_selector)) == ESP_OK);
    TEST_CHECK(hostname_selector[CAP_TAILSCALE_HOSTNAME_LEN - 1] == '\0');
    TEST_CHECK(cap_tailscale_normalize_selector("100.64.0.1", selector, 1) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("  100.64.0.1  ", selector, sizeof(selector)) == ESP_OK);
    TEST_CHECK(strcmp(selector, "100.64.0.1") == 0);
    TEST_CHECK(cap_tailscale_validate_selector("192.168.1.1", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("100.128.0.1", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("100.064.0.1", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("100.64.0", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("100.64.0.1x", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("127.1", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("0xC0A80101", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("2130706433", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("127.0x1", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("192.0xa80101", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_selector("node.example", selector, sizeof(selector)) == ESP_OK);
    TEST_CHECK(strcmp(selector, "node.example") == 0);
    TEST_CHECK(cap_tailscale_validate_selector("edge-01.example", selector, sizeof(selector)) == ESP_OK);
    TEST_CHECK(strcmp(selector, "edge-01.example") == 0);

    TEST_CHECK(cap_tailscale_selector_is_cgnat("100.64.0.1"));
    TEST_CHECK(cap_tailscale_selector_is_cgnat("100.127.255.254"));
    TEST_CHECK(cap_tailscale_selector_is_cgnat("100.64.0.0"));
    TEST_CHECK(cap_tailscale_selector_is_cgnat("100.127.255.255"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.128.0.1"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("192.168.1.1"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.64.0"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.64.0.1x"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.64.0.1.1"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.064.0.1"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.64.00.1"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.64.0.000000000000001"));
}

static void test_count_bounds(void)
{
    TEST_CHECK(cap_tailscale_bound_exit_node_count(0) == 0);
    TEST_CHECK(cap_tailscale_bound_exit_node_count(CAP_TAILSCALE_MAX_EXIT_NODES) == CAP_TAILSCALE_MAX_EXIT_NODES);
    TEST_CHECK(cap_tailscale_bound_exit_node_count(CAP_TAILSCALE_MAX_EXIT_NODES + 1) == CAP_TAILSCALE_MAX_EXIT_NODES);
    TEST_CHECK(cap_tailscale_bound_exit_node_count(SIZE_MAX) == CAP_TAILSCALE_MAX_EXIT_NODES);
    TEST_CHECK(cap_tailscale_bound_derp_rtt_count(0) == 0);
    TEST_CHECK(cap_tailscale_bound_derp_rtt_count(CAP_TAILSCALE_MAX_DERP_RTTS) == CAP_TAILSCALE_MAX_DERP_RTTS);
    TEST_CHECK(cap_tailscale_bound_derp_rtt_count(CAP_TAILSCALE_MAX_DERP_RTTS + 1) == CAP_TAILSCALE_MAX_DERP_RTTS);
    TEST_CHECK(cap_tailscale_bound_derp_rtt_count(SIZE_MAX) == CAP_TAILSCALE_MAX_DERP_RTTS);
}

static void test_model_descriptor_ids_and_safe_status_rendering(void)
{
    static const char *const expected_ids[] = {
        "tailscale_status",
        "tailscale_list_exit_nodes",
        "tailscale_set_exit_node",
        "tailscale_clear_exit_node",
        "tailscale_reconnect",
    };
    static const char *const forbidden[] = {
        "auth_key",
        "wan_public_ip",
        "public_key",
        "private_key",
        "control_payload",
        "AUTH_KEY_SENTINEL",
        "WAN_PUBLIC_IP_SENTINEL",
        "PUBLIC_KEY_SENTINEL",
        "PRIVATE_KEY_SENTINEL",
        "CONTROL_PAYLOAD_SENTINEL",
    };
    cap_tailscale_status_t status = {0};
    char output[4096];
    char tiny_output[8];
    size_t index;

    status.enabled = true;
    status.connected = true;
    snprintf(status.hostname, sizeof(status.hostname), "hostname-sentinel");
    snprintf(status.vpn_ip, sizeof(status.vpn_ip), "vpn-ip-sentinel");
    snprintf(status.path, sizeof(status.path), "path-sentinel");
    status.peer_count = 17;
    status.peer_online = 8;
    snprintf(status.exit_node, sizeof(status.exit_node), "exit-sentinel");
    snprintf(status.exit_state, sizeof(status.exit_state), "state-sentinel");
    snprintf(status.egress, sizeof(status.egress), "egress-sentinel");
    snprintf(status.last_error, sizeof(status.last_error), "safe-error-sentinel");
    status.derp_active.id = 1;
    snprintf(status.derp_active.name, sizeof(status.derp_active.name), "active-derp-sentinel");
    status.derp_default.id = 2;
    snprintf(status.derp_default.name, sizeof(status.derp_default.name), "default-derp-sentinel");
    status.derp_rtts[0].region.id = 3;
    snprintf(status.derp_rtts[0].region.name, sizeof(status.derp_rtts[0].region.name), "rtt-derp-sentinel");
    status.derp_rtts[0].rtt_ms = 42;
    status.derp_rtts[0].timed_out = false;
    status.derp_rtts[1].region.id = 4;
    snprintf(status.derp_rtts[1].region.name, sizeof(status.derp_rtts[1].region.name), "rtt-timeout-sentinel");
    status.derp_rtts[1].rtt_ms = 77;
    status.derp_rtts[1].timed_out = true;
    status.derp_rtt_count = 2;
    status.derp_heartbeat_age_ms = 1234;
    status.control_rx_age_ms = 5678;
    status.reconnect_coord_watchdog = 1;
    status.reconnect_coord_transport = 2;
    status.reconnect_derp_watchdog = 3;
    status.reconnect_derp_retry = 4;

    TEST_CHECK(cap_tailscale_descriptor_count() == sizeof(expected_ids) / sizeof(expected_ids[0]));
    for (index = 0; index < sizeof(expected_ids) / sizeof(expected_ids[0]); ++index) {
        TEST_CHECK(strcmp(cap_tailscale_descriptor_id(index), expected_ids[index]) == 0);
    }
    TEST_CHECK(cap_tailscale_descriptor_id(sizeof(expected_ids) / sizeof(expected_ids[0])) == NULL);

    TEST_CHECK(cap_tailscale_render_status_json(&status, output, sizeof(output)) == ESP_OK);
    TEST_CHECK(strstr(output, "hostname-sentinel") != NULL);
    TEST_CHECK(strstr(output, "vpn-ip-sentinel") != NULL);
    TEST_CHECK(strstr(output, "\"enabled\":true") != NULL);
    TEST_CHECK(strstr(output, "\"connected\":true") != NULL);
    TEST_CHECK(strstr(output, "\"path\":\"path-sentinel\"") != NULL);
    TEST_CHECK(strstr(output, "\"peer_count\":17") != NULL);
    TEST_CHECK(strstr(output, "\"peer_online\":8") != NULL);
    TEST_CHECK(strstr(output, "exit-sentinel") != NULL);
    TEST_CHECK(strstr(output, "\"state\":\"state-sentinel\"") != NULL);
    TEST_CHECK(strstr(output, "\"egress\":\"egress-sentinel\"") != NULL);
    TEST_CHECK(strstr(output, "safe-error-sentinel") != NULL);
    TEST_CHECK(strstr(output, "\"active\":{\"id\":1,\"name\":\"active-derp-sentinel\"}") != NULL);
    TEST_CHECK(strstr(output, "\"default\":{\"id\":2,\"name\":\"default-derp-sentinel\"}") != NULL);
    TEST_CHECK(strstr(output, "\"rtt_count\":2") != NULL);
    TEST_CHECK(strstr(output, "active-derp-sentinel") != NULL);
    TEST_CHECK(strstr(output, "rtt-derp-sentinel") != NULL);
    TEST_CHECK(strstr(output, "\"id\":3,\"name\":\"rtt-derp-sentinel\"") != NULL);
    TEST_CHECK(strstr(output, "\"rtt_ms\":42") != NULL);
    TEST_CHECK(strstr(output, "\"timed_out\":false") != NULL);
    TEST_CHECK(strstr(output, "\"id\":4,\"name\":\"rtt-timeout-sentinel\"") != NULL);
    TEST_CHECK(strstr(output, "\"rtt_ms\":77") != NULL);
    TEST_CHECK(strstr(output, "\"timed_out\":true") != NULL);
    TEST_CHECK(strstr(output, "\"derp_heartbeat_age_ms\":1234") != NULL);
    TEST_CHECK(strstr(output, "\"control_rx_age_ms\":5678") != NULL);
    TEST_CHECK(strstr(output, "\"coord_watchdog\":1") != NULL);
    TEST_CHECK(strstr(output, "\"coord_transport\":2") != NULL);
    TEST_CHECK(strstr(output, "\"derp_watchdog\":3") != NULL);
    TEST_CHECK(strstr(output, "\"derp_retry\":4") != NULL);
    for (index = 0; index < sizeof(forbidden) / sizeof(forbidden[0]); ++index) {
        TEST_CHECK(strstr(output, forbidden[index]) == NULL);
    }

    memset(tiny_output, 'x', sizeof(tiny_output));
    TEST_CHECK(cap_tailscale_render_status_json(&status, tiny_output, sizeof(tiny_output)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(tiny_output[0] == '\0');
    TEST_CHECK(cap_tailscale_render_status_json(NULL, output, sizeof(output)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_render_status_json(&status, NULL, sizeof(output)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_render_status_json(&status, output, 0) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    test_public_contract_shape();
    test_read_and_mutation_validation();
    test_selector_normalization_and_cgnat_range();
    test_count_bounds();
    test_model_descriptor_ids_and_safe_status_rendering();

    puts("cap_tailscale_contract: all tests passed");
    return 0;
}
