#include "cap_tailscale.h"
#include "claw_cap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const claw_cap_group_t *s_registered_group;
static bool s_group_exists;
static int s_status_calls;
static int s_list_calls;
static int s_set_calls;
static int s_clear_calls;
static int s_reconnect_calls;
static esp_err_t s_status_result;
static esp_err_t s_set_result;
static bool s_set_semantic_rejection;
static char s_last_selector[CAP_TAILSCALE_HOSTNAME_LEN];

static void test_check(bool condition, const char *expression, const char *file, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: test failed: %s\n", file, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define TEST_CHECK(condition) test_check((condition), #condition, __FILE__, __LINE__)

bool claw_cap_group_exists(const char *group_id)
{
    return s_group_exists && strcmp(group_id, "cap_tailscale") == 0;
}

esp_err_t claw_cap_register_group(const claw_cap_group_t *group)
{
    s_registered_group = group;
    s_group_exists = true;
    return ESP_OK;
}

static void populate_status(cap_tailscale_status_t *out)
{
    *out = (cap_tailscale_status_t) {
        .enabled = true,
        .connected = true,
        .peer_count = 9,
        .peer_online = 7,
        .derp_heartbeat_age_ms = 100,
        .control_rx_age_ms = 200,
        .reconnect_coord_watchdog = 1,
        .reconnect_coord_transport = 2,
        .reconnect_derp_watchdog = 3,
        .reconnect_derp_retry = 4,
    };
    snprintf(out->hostname, sizeof(out->hostname), "handler-host");
    snprintf(out->vpn_ip, sizeof(out->vpn_ip), "100.64.0.9");
    snprintf(out->path, sizeof(out->path), "derp");
    snprintf(out->exit_node, sizeof(out->exit_node), "100.64.0.2");
    snprintf(out->exit_state, sizeof(out->exit_state), "active");
    snprintf(out->egress, sizeof(out->egress), "exit_node");
    snprintf(out->last_error, sizeof(out->last_error), "safe-status-error");
    out->derp_active.id = 1;
    snprintf(out->derp_active.name, sizeof(out->derp_active.name), "active-derp");
    out->derp_default.id = 2;
    snprintf(out->derp_default.name, sizeof(out->derp_default.name), "default-derp");
    out->derp_rtts[0].region.id = 3;
    snprintf(out->derp_rtts[0].region.name, sizeof(out->derp_rtts[0].region.name), "rtt-derp");
    out->derp_rtts[0].rtt_ms = 33;
    out->derp_rtts[0].timed_out = false;
    out->derp_rtt_count = 1;
}

static esp_err_t fake_get_status(cap_tailscale_status_t *out, void *ctx)
{
    (void)ctx;
    ++s_status_calls;
    if (s_status_result != ESP_OK) {
        return s_status_result;
    }
    populate_status(out);
    return ESP_OK;
}

static int fake_list_exit_nodes(cap_tailscale_exit_node_t *out, size_t capacity, void *ctx)
{
    (void)ctx;
    ++s_list_calls;
    if (capacity == 0) {
        return 0;
    }
    out[0].online = true;
    out[0].direct = false;
    snprintf(out[0].ip, sizeof(out[0].ip), "100.64.0.3");
    snprintf(out[0].hostname, sizeof(out[0].hostname), "node.example");
    out[0].derp_region.id = 4;
    snprintf(out[0].derp_region.name, sizeof(out[0].derp_region.name), "node-derp");
    return 1;
}

static esp_err_t fake_set_exit_node(const char *selector,
                                    cap_tailscale_mutation_result_t *out,
                                    void *ctx)
{
    (void)ctx;
    ++s_set_calls;
    snprintf(s_last_selector, sizeof(s_last_selector), "%s", selector);
    if (s_set_result != ESP_OK) {
        return s_set_result;
    }
    if (s_set_semantic_rejection) {
        out->ok = false;
        snprintf(out->error, sizeof(out->error), "exit_node_denied");
        snprintf(out->message, sizeof(out->message), "Requested node is not eligible.");
        snprintf(out->exit_state, sizeof(out->exit_state), "unchanged");
        snprintf(out->egress, sizeof(out->egress), "direct");
        return ESP_OK;
    }
    out->ok = true;
    snprintf(out->message, sizeof(out->message), "Exit Node selected.");
    snprintf(out->selected_hostname, sizeof(out->selected_hostname), "%s", selector);
    snprintf(out->exit_state, sizeof(out->exit_state), "active");
    snprintf(out->egress, sizeof(out->egress), "exit_node");
    out->persisted = true;
    return ESP_OK;
}

static esp_err_t fake_clear_exit_node(cap_tailscale_mutation_result_t *out, void *ctx)
{
    (void)ctx;
    ++s_clear_calls;
    out->ok = true;
    snprintf(out->message, sizeof(out->message), "Exit Node cleared.");
    snprintf(out->exit_state, sizeof(out->exit_state), "cleared");
    snprintf(out->egress, sizeof(out->egress), "direct");
    out->persisted = true;
    return ESP_OK;
}

static esp_err_t fake_reconnect(cap_tailscale_status_t *out, void *ctx)
{
    (void)ctx;
    ++s_reconnect_calls;
    populate_status(out);
    return ESP_OK;
}

static const claw_cap_descriptor_t *descriptor(size_t index)
{
    TEST_CHECK(s_registered_group != NULL);
    TEST_CHECK(index < s_registered_group->descriptor_count);
    return &s_registered_group->descriptors[index];
}

static esp_err_t execute(size_t index, const char *input, char *output, size_t output_size)
{
    return descriptor(index)->execute(input, NULL, output, output_size);
}

static void test_registration_schemas_and_read_handlers(void)
{
    static const char *const expected_ids[] = {
        "tailscale_status",
        "tailscale_list_exit_nodes",
        "tailscale_set_exit_node",
        "tailscale_clear_exit_node",
        "tailscale_reconnect",
    };
    static const char *const expected_schemas[] = {
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        "{\"type\":\"object\",\"properties\":{\"node\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":95},\"user_confirmed\":{\"type\":\"boolean\",\"const\":true}},\"required\":[\"node\",\"user_confirmed\"],\"additionalProperties\":false}",
        "{\"type\":\"object\",\"properties\":{\"user_confirmed\":{\"type\":\"boolean\",\"const\":true}},\"required\":[\"user_confirmed\"],\"additionalProperties\":false}",
        "{\"type\":\"object\",\"properties\":{\"user_confirmed\":{\"type\":\"boolean\",\"const\":true}},\"required\":[\"user_confirmed\"],\"additionalProperties\":false}",
    };
    char output[4096];
    size_t index;

    TEST_CHECK(cap_tailscale_register_group() == ESP_OK);
    TEST_CHECK(s_registered_group != NULL);
    TEST_CHECK(strcmp(s_registered_group->group_id, "cap_tailscale") == 0);
    TEST_CHECK(s_registered_group->descriptor_count == 5);
    for (index = 0; index < s_registered_group->descriptor_count; ++index) {
        TEST_CHECK(strcmp(descriptor(index)->id, expected_ids[index]) == 0);
        TEST_CHECK(strcmp(descriptor(index)->input_schema_json, expected_schemas[index]) == 0);
    }

    TEST_CHECK(execute(0, "{}", output, sizeof(output)) == ESP_OK);
    TEST_CHECK(s_status_calls == 1);
    TEST_CHECK(strstr(output, "handler-host") != NULL);
    TEST_CHECK(execute(1, "{}", output, sizeof(output)) == ESP_OK);
    TEST_CHECK(s_list_calls == 1);
    TEST_CHECK(strstr(output, "node.example") != NULL);
    TEST_CHECK(strstr(output, "\"online\":true") != NULL);
    TEST_CHECK(strstr(output, "\"direct\":false") != NULL);
    TEST_CHECK(strstr(output, "node-derp") != NULL);
}

static void test_confirmed_mutations_and_rejections(void)
{
    char output[4096];
    int set_before;
    int clear_before;
    int reconnect_before;

    TEST_CHECK(execute(2, "{\"node\":\"node.example\",\"user_confirmed\":true}",
                       output, sizeof(output)) == ESP_OK);
    TEST_CHECK(s_set_calls == 1);
    TEST_CHECK(strcmp(s_last_selector, "node.example") == 0);
    TEST_CHECK(execute(3, "{\"user_confirmed\":true}", output, sizeof(output)) == ESP_OK);
    TEST_CHECK(s_clear_calls == 1);
    TEST_CHECK(execute(4, "{\"user_confirmed\":true}", output, sizeof(output)) == ESP_OK);
    TEST_CHECK(s_reconnect_calls == 1);

    set_before = s_set_calls;
    clear_before = s_clear_calls;
    reconnect_before = s_reconnect_calls;
    TEST_CHECK(execute(2, "{\"node\":\"node.example\"}", output, sizeof(output)) == ESP_ERR_INVALID_STATE);
    TEST_CHECK(strstr(output, "confirmation_required") != NULL);
    TEST_CHECK(execute(3, "{\"user_confirmed\":false}", output, sizeof(output)) == ESP_ERR_INVALID_STATE);
    TEST_CHECK(strstr(output, "confirmation_required") != NULL);
    TEST_CHECK(execute(4, "{\"user_confirmed\":\"true\"}", output, sizeof(output)) == ESP_ERR_INVALID_STATE);
    TEST_CHECK(strstr(output, "confirmation_required") != NULL);
    TEST_CHECK(execute(2, "{\"node\":\"node.example\",\"user_confirmed\":true,\"extra\":1}",
                       output, sizeof(output)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(strstr(output, "invalid_input") != NULL);
    TEST_CHECK(s_set_calls == set_before);
    TEST_CHECK(s_clear_calls == clear_before);
    TEST_CHECK(s_reconnect_calls == reconnect_before);
}

static void test_selector_and_provider_result_boundaries(void)
{
    char output[4096];
    int set_before = s_set_calls;

    TEST_CHECK(execute(2, "{\"node\":\"192.168.1.1\",\"user_confirmed\":true}",
                       output, sizeof(output)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(strstr(output, "invalid_input") != NULL);
    TEST_CHECK(s_set_calls == set_before);
    TEST_CHECK(execute(2, "{\"node\":\"node.example\",\"user_confirmed\":true}",
                       output, sizeof(output)) == ESP_OK);
    TEST_CHECK(s_set_calls == set_before + 1);
    TEST_CHECK(strcmp(s_last_selector, "node.example") == 0);

    s_set_semantic_rejection = true;
    TEST_CHECK(execute(2, "{\"node\":\"node.example\",\"user_confirmed\":true}",
                       output, sizeof(output)) == ESP_OK);
    TEST_CHECK(strstr(output, "exit_node_denied") != NULL);
    TEST_CHECK(strstr(output, "Requested node is not eligible.") != NULL);
    s_set_semantic_rejection = false;
}

static void test_unavailable_and_transport_errors_are_safe(void)
{
    char output[4096];
    cap_tailscale_provider_t provider = {
        .get_status = fake_get_status,
        .list_exit_nodes = fake_list_exit_nodes,
        .set_exit_node = fake_set_exit_node,
        .clear_exit_node = fake_clear_exit_node,
        .reconnect = fake_reconnect,
    };

    TEST_CHECK(cap_tailscale_set_provider(NULL) == ESP_OK);
    TEST_CHECK(execute(0, "{}", output, sizeof(output)) == ESP_ERR_INVALID_STATE);
    TEST_CHECK(strstr(output, "provider_unavailable") != NULL);
    TEST_CHECK(execute(2, "{\"node\":\"node.example\",\"user_confirmed\":true}",
                       output, sizeof(output)) == ESP_ERR_INVALID_STATE);
    TEST_CHECK(strstr(output, "provider_unavailable") != NULL);
    TEST_CHECK(strstr(output, "\"state\":\"unchanged\"") != NULL);
    TEST_CHECK(cap_tailscale_set_provider(&provider) == ESP_OK);
    s_status_result = ESP_FAIL;
    TEST_CHECK(execute(0, "{}", output, sizeof(output)) == ESP_FAIL);
    TEST_CHECK(strstr(output, "status_unavailable") != NULL);
    TEST_CHECK(strstr(output, "auth_key") == NULL);
    TEST_CHECK(strstr(output, "control_payload") == NULL);
    s_status_result = ESP_OK;
    s_set_result = ESP_FAIL;
    TEST_CHECK(execute(2, "{\"node\":\"node.example\",\"user_confirmed\":true}",
                       output, sizeof(output)) == ESP_FAIL);
    TEST_CHECK(strstr(output, "set_exit_node_failed") != NULL);
    TEST_CHECK(strstr(output, "auth_key") == NULL);
    TEST_CHECK(strstr(output, "control_payload") == NULL);
    s_set_result = ESP_OK;
}

int main(void)
{
    cap_tailscale_provider_t provider = {
        .get_status = fake_get_status,
        .list_exit_nodes = fake_list_exit_nodes,
        .set_exit_node = fake_set_exit_node,
        .clear_exit_node = fake_clear_exit_node,
        .reconnect = fake_reconnect,
    };

    TEST_CHECK(cap_tailscale_set_provider(&provider) == ESP_OK);
    test_registration_schemas_and_read_handlers();
    test_confirmed_mutations_and_rejections();
    test_selector_and_provider_result_boundaries();
    test_unavailable_and_transport_errors_are_safe();
    puts("cap_tailscale_handlers: all tests passed");
    return 0;
}
