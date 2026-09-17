/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_tailscale.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "cap_tailscale_contract.h"
#include "claw_cap.h"

static cap_tailscale_provider_t s_provider;
static bool s_provider_installed;
static bool s_provider_frozen;

#ifdef CAP_TAILSCALE_HOST_TEST
static int s_successful_allocations_before_failure = -1;
#endif

static void *cap_tailscale_calloc(size_t count, size_t size)
{
#ifdef CAP_TAILSCALE_HOST_TEST
    if (s_successful_allocations_before_failure == 0) {
        return NULL;
    }
    if (s_successful_allocations_before_failure > 0) {
        --s_successful_allocations_before_failure;
    }
#endif
    return calloc(count, size);
}

static void cap_tailscale_copy_text(char *destination, size_t destination_size, const char *source)
{
    if (destination_size == 0) {
        return;
    }
    snprintf(destination, destination_size, "%s", source ? source : "");
}

static void cap_tailscale_write_compact_error(char *output, size_t output_size)
{
    static const char compact_error[] = "{\"ok\":false}";

    if (output == NULL || output_size == 0) {
        return;
    }
    if (output_size < sizeof(compact_error)) {
        output[0] = '\0';
        return;
    }
    memcpy(output, compact_error, sizeof(compact_error));
}

static void cap_tailscale_write_error(char *output,
                                      size_t output_size,
                                      const char *error,
                                      const char *message)
{
    if (output == NULL || output_size == 0) {
        return;
    }
    if (cap_tailscale_render_error_json(error, message, output, output_size) != ESP_OK) {
        cap_tailscale_write_compact_error(output, output_size);
    }
}

static void cap_tailscale_write_mutation_error(char *output,
                                               size_t output_size,
                                               const char *error,
                                               const char *message)
{
    cap_tailscale_mutation_result_t *result;

    result = cap_tailscale_calloc(1, sizeof(*result));
    if (result == NULL) {
        cap_tailscale_write_error(output, output_size, error, message);
        return;
    }
    result->ok = false;
    cap_tailscale_copy_text(result->error, sizeof(result->error), error);
    cap_tailscale_copy_text(result->message, sizeof(result->message), message);
    cap_tailscale_copy_text(result->exit_state, sizeof(result->exit_state), "unchanged");
    cap_tailscale_copy_text(result->egress, sizeof(result->egress), "unknown");
    if (cap_tailscale_render_mutation_json(result, output, output_size) != ESP_OK) {
        cap_tailscale_write_compact_error(output, output_size);
    }
    free(result);
}

static esp_err_t cap_tailscale_parse_object(const char *input_json, cJSON **out_root)
{
    cJSON *root;
    const char *parse_end = NULL;
    size_t index;
    size_t input_length;

    if (input_json == NULL || out_root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    input_length = strlen(input_json);
    for (index = 0; index < input_length; ++index) {
        if (input_json[index] != '\\') {
            continue;
        }
        if (index + 1 >= input_length) {
            return ESP_ERR_INVALID_ARG;
        }
        if (input_json[index + 1] == 'u' && index + 5 >= input_length) {
            return ESP_ERR_INVALID_ARG;
        }
        if (input_json[index + 1] == 'u' && input_json[index + 2] == '0' &&
            input_json[index + 3] == '0' && input_json[index + 4] == '0' &&
            input_json[index + 5] == '0') {
            return ESP_ERR_INVALID_ARG;
        }
        ++index;
    }
    root = cJSON_ParseWithOpts(input_json, &parse_end, 1);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    *out_root = root;
    return ESP_OK;
}

static size_t cap_tailscale_unknown_field_count(const cJSON *root,
                                                const char *const *allowed_fields,
                                                size_t allowed_count)
{
    const cJSON *field;
    size_t unknown_count = 0;

    cJSON_ArrayForEach(field, root) {
        size_t index;
        bool known = false;

        for (index = 0; index < allowed_count; ++index) {
            if (field->string != NULL && strcmp(field->string, allowed_fields[index]) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            ++unknown_count;
        }
    }
    return unknown_count;
}

static size_t cap_tailscale_field_count(const cJSON *root, const char *field_name)
{
    const cJSON *field;
    size_t count = 0;

    cJSON_ArrayForEach(field, root) {
        if (field->string != NULL && strcmp(field->string, field_name) == 0) {
            ++count;
        }
    }
    return count;
}

static esp_err_t cap_tailscale_require_confirmation(const cJSON *root,
                                                    size_t unexpected_field_count)
{
    const cJSON *confirmed = cJSON_GetObjectItemCaseSensitive(root, "user_confirmed");
    esp_err_t err = cap_tailscale_validate_mutation_confirmation(confirmed != NULL,
                                                                  cJSON_IsBool(confirmed),
                                                                  cJSON_IsTrue(confirmed),
                                                                  unexpected_field_count);

    if (unexpected_field_count != 0 || cap_tailscale_field_count(root, "user_confirmed") > 1) {
        return ESP_ERR_INVALID_ARG;
    }
    return err == ESP_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t cap_tailscale_read_status_execute(const char *input_json,
                                                    const claw_cap_call_context_t *ctx,
                                                    char *output,
                                                    size_t output_size)
{
    cJSON *root = NULL;
    cap_tailscale_status_t *status;
    esp_err_t err;

    (void)ctx;
    err = cap_tailscale_parse_object(input_json, &root);
    if (err != ESP_OK || cap_tailscale_validate_read_args(
                             cap_tailscale_unknown_field_count(root, NULL, 0)) != ESP_OK) {
        cJSON_Delete(root);
        cap_tailscale_write_error(output, output_size, "invalid_input", "Input must be an empty object.");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_Delete(root);
    if (!s_provider_installed) {
        cap_tailscale_write_error(output, output_size, "provider_unavailable",
                                  "Tailscale service is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    status = cap_tailscale_calloc(1, sizeof(*status));
    if (status == NULL) {
        cap_tailscale_write_error(output, output_size, "out_of_memory",
                                  "Unable to allocate Tailscale status.");
        return ESP_ERR_NO_MEM;
    }
    err = s_provider.get_status(status, s_provider.ctx);
    if (err != ESP_OK) {
        free(status);
        cap_tailscale_write_error(output, output_size, "status_unavailable",
                                  "Unable to read Tailscale status.");
        return err;
    }
    err = cap_tailscale_render_status_json(status, output, output_size);
    free(status);
    if (err != ESP_OK) {
        cap_tailscale_write_error(output, output_size, "output_too_small",
                                  "Unable to render Tailscale status.");
    }
    return err;
}

static esp_err_t cap_tailscale_list_exit_nodes_execute(const char *input_json,
                                                        const claw_cap_call_context_t *ctx,
                                                        char *output,
                                                        size_t output_size)
{
    cJSON *root = NULL;
    cap_tailscale_exit_node_t *nodes;
    int count;
    esp_err_t err;

    (void)ctx;
    err = cap_tailscale_parse_object(input_json, &root);
    if (err != ESP_OK || cap_tailscale_validate_read_args(
                             cap_tailscale_unknown_field_count(root, NULL, 0)) != ESP_OK) {
        cJSON_Delete(root);
        cap_tailscale_write_error(output, output_size, "invalid_input", "Input must be an empty object.");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_Delete(root);
    if (!s_provider_installed) {
        cap_tailscale_write_error(output, output_size, "provider_unavailable",
                                  "Tailscale service is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    nodes = cap_tailscale_calloc(CAP_TAILSCALE_MAX_EXIT_NODES, sizeof(*nodes));
    if (nodes == NULL) {
        cap_tailscale_write_error(output, output_size, "out_of_memory",
                                  "Unable to allocate Exit Nodes.");
        return ESP_ERR_NO_MEM;
    }
    count = s_provider.list_exit_nodes(nodes, CAP_TAILSCALE_MAX_EXIT_NODES, s_provider.ctx);
    if (count < 0) {
        free(nodes);
        cap_tailscale_write_error(output, output_size, "exit_nodes_unavailable",
                                  "Unable to list Exit Nodes.");
        return ESP_FAIL;
    }
    err = cap_tailscale_render_exit_nodes_json(nodes, (size_t)count, output, output_size);
    free(nodes);
    if (err != ESP_OK) {
        cap_tailscale_write_error(output, output_size, "output_too_small",
                                  "Unable to render Exit Nodes.");
    }
    return err;
}

static esp_err_t cap_tailscale_parse_set_request(const char *input_json, char *selector, size_t selector_size)
{
    static const char *const allowed_fields[] = {"node", "user_confirmed"};
    cJSON *root = NULL;
    const cJSON *node;
    size_t unknown_count;
    esp_err_t err;

    err = cap_tailscale_parse_object(input_json, &root);
    if (err != ESP_OK) {
        return err;
    }
    unknown_count = cap_tailscale_unknown_field_count(root, allowed_fields,
                                                       sizeof(allowed_fields) / sizeof(allowed_fields[0]));
    err = cap_tailscale_require_confirmation(root, unknown_count);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }
    if (cap_tailscale_field_count(root, "node") != 1) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    node = cJSON_GetObjectItemCaseSensitive(root, "node");
    if (!cJSON_IsString(node) || node->valuestring == NULL ||
        strlen(node->valuestring) > CAP_TAILSCALE_HOSTNAME_LEN - 1) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    err = cap_tailscale_validate_selector(node->valuestring, selector, selector_size);
    cJSON_Delete(root);
    return err;
}

static esp_err_t cap_tailscale_set_exit_node_execute(const char *input_json,
                                                      const claw_cap_call_context_t *ctx,
                                                      char *output,
                                                      size_t output_size)
{
    char selector[CAP_TAILSCALE_HOSTNAME_LEN];
    cap_tailscale_mutation_result_t *result;
    esp_err_t err;

    (void)ctx;
    err = cap_tailscale_parse_set_request(input_json, selector, sizeof(selector));
    if (err != ESP_OK) {
        cap_tailscale_write_mutation_error(output, output_size,
                                           err == ESP_ERR_INVALID_STATE ? "confirmation_required" : "invalid_input",
                                           err == ESP_ERR_INVALID_STATE ?
                                           "The current user must explicitly confirm this change." :
                                           "Invalid Exit Node selection.");
        return err;
    }
    if (!s_provider_installed) {
        cap_tailscale_write_mutation_error(output, output_size, "provider_unavailable",
                                           "Tailscale service is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }
    result = cap_tailscale_calloc(1, sizeof(*result));
    if (result == NULL) {
        cap_tailscale_write_mutation_error(output, output_size, "out_of_memory",
                                           "Unable to allocate the Exit Node result.");
        return ESP_ERR_NO_MEM;
    }
    err = s_provider.set_exit_node(selector, result, s_provider.ctx);
    if (err != ESP_OK) {
        free(result);
        cap_tailscale_write_mutation_error(output, output_size, "set_exit_node_failed",
                                           "Unable to change the Exit Node.");
        return err;
    }
    err = cap_tailscale_render_mutation_json(result, output, output_size);
    free(result);
    if (err != ESP_OK) {
        cap_tailscale_write_mutation_error(output, output_size, "output_too_small",
                                           "Unable to render the Exit Node result.");
    }
    return err;
}

static esp_err_t cap_tailscale_parse_confirmed_request(const char *input_json)
{
    static const char *const allowed_fields[] = {"user_confirmed"};
    cJSON *root = NULL;
    esp_err_t err;

    err = cap_tailscale_parse_object(input_json, &root);
    if (err != ESP_OK) {
        return err;
    }
    err = cap_tailscale_require_confirmation(
        root,
        cap_tailscale_unknown_field_count(root, allowed_fields,
                                          sizeof(allowed_fields) / sizeof(allowed_fields[0])));
    cJSON_Delete(root);
    return err;
}

static esp_err_t cap_tailscale_clear_exit_node_execute(const char *input_json,
                                                        const claw_cap_call_context_t *ctx,
                                                        char *output,
                                                        size_t output_size)
{
    cap_tailscale_mutation_result_t *result;
    esp_err_t err;

    (void)ctx;
    err = cap_tailscale_parse_confirmed_request(input_json);
    if (err != ESP_OK) {
        cap_tailscale_write_mutation_error(output, output_size,
                                           err == ESP_ERR_INVALID_STATE ? "confirmation_required" : "invalid_input",
                                           err == ESP_ERR_INVALID_STATE ?
                                           "The current user must explicitly confirm this change." :
                                           "Invalid input.");
        return err;
    }
    if (!s_provider_installed) {
        cap_tailscale_write_mutation_error(output, output_size, "provider_unavailable",
                                           "Tailscale service is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }
    result = cap_tailscale_calloc(1, sizeof(*result));
    if (result == NULL) {
        cap_tailscale_write_mutation_error(output, output_size, "out_of_memory",
                                           "Unable to allocate the Exit Node result.");
        return ESP_ERR_NO_MEM;
    }
    err = s_provider.clear_exit_node(result, s_provider.ctx);
    if (err != ESP_OK) {
        free(result);
        cap_tailscale_write_mutation_error(output, output_size, "clear_exit_node_failed",
                                           "Unable to clear the Exit Node.");
        return err;
    }
    err = cap_tailscale_render_mutation_json(result, output, output_size);
    free(result);
    if (err != ESP_OK) {
        cap_tailscale_write_mutation_error(output, output_size, "output_too_small",
                                           "Unable to render the Exit Node result.");
    }
    return err;
}

static esp_err_t cap_tailscale_reconnect_execute(const char *input_json,
                                                  const claw_cap_call_context_t *ctx,
                                                  char *output,
                                                  size_t output_size)
{
    cap_tailscale_status_t *status;
    cap_tailscale_mutation_result_t *result;
    esp_err_t err;

    (void)ctx;
    err = cap_tailscale_parse_confirmed_request(input_json);
    if (err != ESP_OK) {
        cap_tailscale_write_mutation_error(output, output_size,
                                           err == ESP_ERR_INVALID_STATE ? "confirmation_required" : "invalid_input",
                                           err == ESP_ERR_INVALID_STATE ?
                                           "The current user must explicitly confirm this change." :
                                           "Invalid input.");
        return err;
    }
    if (!s_provider_installed) {
        cap_tailscale_write_mutation_error(output, output_size, "provider_unavailable",
                                           "Tailscale service is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }
    status = cap_tailscale_calloc(1, sizeof(*status));
    if (status == NULL) {
        cap_tailscale_write_mutation_error(output, output_size, "out_of_memory",
                                           "Unable to allocate Tailscale status.");
        return ESP_ERR_NO_MEM;
    }
    result = cap_tailscale_calloc(1, sizeof(*result));
    if (result == NULL) {
        free(status);
        cap_tailscale_write_mutation_error(output, output_size, "out_of_memory",
                                           "Unable to allocate the reconnect result.");
        return ESP_ERR_NO_MEM;
    }
    err = s_provider.reconnect(status, s_provider.ctx);
    if (err != ESP_OK) {
        free(result);
        free(status);
        cap_tailscale_write_mutation_error(output, output_size, "reconnect_failed",
                                           "Unable to reconnect Tailscale.");
        return err;
    }

    result->ok = true;
    cap_tailscale_copy_text(result->message, sizeof(result->message), "Reconnect requested.");
    cap_tailscale_copy_text(result->exit_state, sizeof(result->exit_state), status->exit_state);
    cap_tailscale_copy_text(result->egress, sizeof(result->egress), status->egress);
    result->persisted = true;
    err = cap_tailscale_render_mutation_json(result, output, output_size);
    free(result);
    free(status);
    if (err != ESP_OK) {
        cap_tailscale_write_mutation_error(output, output_size, "output_too_small",
                                           "Unable to render the reconnect result.");
    }
    return err;
}

static const claw_cap_descriptor_t s_tailscale_descriptors[] = {
    {
        .id = CAP_TAILSCALE_TOOL_STATUS,
        .name = CAP_TAILSCALE_TOOL_STATUS,
        .family = "tailscale",
        .description = "Inspect this device's safe Tailscale status.",
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        .execute = cap_tailscale_read_status_execute,
    },
    {
        .id = CAP_TAILSCALE_TOOL_LIST_EXIT_NODES,
        .name = CAP_TAILSCALE_TOOL_LIST_EXIT_NODES,
        .family = "tailscale",
        .description = "List available Exit Nodes for this device.",
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
        .execute = cap_tailscale_list_exit_nodes_execute,
    },
    {
        .id = CAP_TAILSCALE_TOOL_SET_EXIT_NODE,
        .name = CAP_TAILSCALE_TOOL_SET_EXIT_NODE,
        .family = "tailscale",
        .description = "Set this device's Exit Node after explicit confirmation.",
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"node\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":95},\"user_confirmed\":{\"type\":\"boolean\",\"const\":true}},\"required\":[\"node\",\"user_confirmed\"],\"additionalProperties\":false}",
        .execute = cap_tailscale_set_exit_node_execute,
    },
    {
        .id = CAP_TAILSCALE_TOOL_CLEAR_EXIT_NODE,
        .name = CAP_TAILSCALE_TOOL_CLEAR_EXIT_NODE,
        .family = "tailscale",
        .description = "Clear this device's Exit Node after explicit confirmation.",
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"user_confirmed\":{\"type\":\"boolean\",\"const\":true}},\"required\":[\"user_confirmed\"],\"additionalProperties\":false}",
        .execute = cap_tailscale_clear_exit_node_execute,
    },
    {
        .id = CAP_TAILSCALE_TOOL_RECONNECT,
        .name = CAP_TAILSCALE_TOOL_RECONNECT,
        .family = "tailscale",
        .description = "Reconnect this device's Tailscale runtime after explicit confirmation.",
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"user_confirmed\":{\"type\":\"boolean\",\"const\":true}},\"required\":[\"user_confirmed\"],\"additionalProperties\":false}",
        .execute = cap_tailscale_reconnect_execute,
    },
};

static const claw_cap_group_t cap_tailscale = {
    .group_id = "cap_tailscale",
    .descriptors = s_tailscale_descriptors,
    .descriptor_count = sizeof(s_tailscale_descriptors) / sizeof(s_tailscale_descriptors[0]),
};

esp_err_t cap_tailscale_set_provider(const cap_tailscale_provider_t *provider)
{
    if (s_provider_frozen) {
        return ESP_ERR_INVALID_STATE;
    }
    if (provider == NULL) {
        memset(&s_provider, 0, sizeof(s_provider));
        s_provider_installed = false;
        return ESP_OK;
    }
    if (provider->get_status == NULL || provider->list_exit_nodes == NULL ||
        provider->set_exit_node == NULL || provider->clear_exit_node == NULL ||
        provider->reconnect == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_provider = *provider;
    s_provider_installed = true;
    return ESP_OK;
}

esp_err_t cap_tailscale_register_group(void)
{
    esp_err_t err;

    if (claw_cap_group_exists(cap_tailscale.group_id)) {
        s_provider_frozen = true;
        return ESP_OK;
    }
    err = claw_cap_register_group(&cap_tailscale);
    if (err == ESP_OK) {
        s_provider_frozen = true;
    }
    return err;
}

#ifdef CAP_TAILSCALE_HOST_TEST
void cap_tailscale_test_reset(void)
{
    memset(&s_provider, 0, sizeof(s_provider));
    s_provider_installed = false;
    s_provider_frozen = false;
    s_successful_allocations_before_failure = -1;
}

void cap_tailscale_test_fail_allocations_after(int successful_allocations)
{
    s_successful_allocations_before_failure = successful_allocations;
}
#endif
