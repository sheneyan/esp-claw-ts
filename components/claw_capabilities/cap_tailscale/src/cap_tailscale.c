/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_tailscale.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "cap_tailscale_contract.h"
#include "claw_cap.h"

static cap_tailscale_provider_t s_provider;
static bool s_provider_installed;

static void cap_tailscale_copy_text(char *destination, size_t destination_size, const char *source)
{
    if (destination_size == 0) {
        return;
    }
    snprintf(destination, destination_size, "%s", source ? source : "");
}

static void cap_tailscale_write_error(char *output,
                                      size_t output_size,
                                      const char *error,
                                      const char *message)
{
    if (output == NULL || output_size == 0) {
        return;
    }
    snprintf(output,
             output_size,
             "{\"ok\":false,\"error\":\"%s\",\"message\":\"%s\"}",
             error,
             message);
}

static void cap_tailscale_write_mutation_error(char *output,
                                               size_t output_size,
                                               const char *error,
                                               const char *message)
{
    cap_tailscale_mutation_result_t result = {
        .ok = false,
    };

    cap_tailscale_copy_text(result.error, sizeof(result.error), error);
    cap_tailscale_copy_text(result.message, sizeof(result.message), message);
    cap_tailscale_copy_text(result.exit_state, sizeof(result.exit_state), "unchanged");
    cap_tailscale_copy_text(result.egress, sizeof(result.egress), "unknown");
    if (cap_tailscale_render_mutation_json(&result, output, output_size) != ESP_OK &&
        output != NULL && output_size != 0) {
        output[0] = '\0';
    }
}

static esp_err_t cap_tailscale_parse_object(const char *input_json, cJSON **out_root)
{
    cJSON *root;

    if (input_json == NULL || out_root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    root = cJSON_Parse(input_json);
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

static esp_err_t cap_tailscale_require_confirmation(const cJSON *root,
                                                    size_t unexpected_field_count)
{
    const cJSON *confirmed = cJSON_GetObjectItemCaseSensitive(root, "user_confirmed");
    esp_err_t err = cap_tailscale_validate_mutation_confirmation(confirmed != NULL,
                                                                  cJSON_IsBool(confirmed),
                                                                  cJSON_IsTrue(confirmed),
                                                                  unexpected_field_count);

    if (err == ESP_ERR_INVALID_STATE) {
        return err;
    }
    return unexpected_field_count == 0 ? ESP_ERR_INVALID_STATE : err;
}

static esp_err_t cap_tailscale_read_status_execute(const char *input_json,
                                                    const claw_cap_call_context_t *ctx,
                                                    char *output,
                                                    size_t output_size)
{
    cJSON *root = NULL;
    cap_tailscale_status_t status = {0};
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

    err = s_provider.get_status(&status, s_provider.ctx);
    if (err != ESP_OK) {
        cap_tailscale_write_error(output, output_size, "status_unavailable",
                                  "Unable to read Tailscale status.");
        return err;
    }
    err = cap_tailscale_render_status_json(&status, output, output_size);
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
    cap_tailscale_exit_node_t nodes[CAP_TAILSCALE_MAX_EXIT_NODES] = {0};
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

    count = s_provider.list_exit_nodes(nodes, CAP_TAILSCALE_MAX_EXIT_NODES, s_provider.ctx);
    if (count < 0) {
        cap_tailscale_write_error(output, output_size, "exit_nodes_unavailable",
                                  "Unable to list Exit Nodes.");
        return ESP_FAIL;
    }
    err = cap_tailscale_render_exit_nodes_json(nodes, (size_t)count, output, output_size);
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
    node = cJSON_GetObjectItemCaseSensitive(root, "node");
    if (!cJSON_IsString(node) || node->valuestring == NULL ||
        strlen(node->valuestring) > CAP_TAILSCALE_HOSTNAME_LEN - 1) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    err = cap_tailscale_normalize_selector(node->valuestring, selector, selector_size);
    cJSON_Delete(root);
    return err;
}

static esp_err_t cap_tailscale_set_exit_node_execute(const char *input_json,
                                                      const claw_cap_call_context_t *ctx,
                                                      char *output,
                                                      size_t output_size)
{
    char selector[CAP_TAILSCALE_HOSTNAME_LEN];
    cap_tailscale_mutation_result_t result = {0};
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
    err = s_provider.set_exit_node(selector, &result, s_provider.ctx);
    if (err != ESP_OK) {
        cap_tailscale_write_mutation_error(output, output_size, "set_exit_node_failed",
                                           "Unable to change the Exit Node.");
        return err;
    }
    err = cap_tailscale_render_mutation_json(&result, output, output_size);
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
    cap_tailscale_mutation_result_t result = {0};
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
    err = s_provider.clear_exit_node(&result, s_provider.ctx);
    if (err != ESP_OK) {
        cap_tailscale_write_mutation_error(output, output_size, "clear_exit_node_failed",
                                           "Unable to clear the Exit Node.");
        return err;
    }
    err = cap_tailscale_render_mutation_json(&result, output, output_size);
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
    cap_tailscale_status_t status = {0};
    cap_tailscale_mutation_result_t result = {
        .ok = true,
    };
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
    err = s_provider.reconnect(&status, s_provider.ctx);
    if (err != ESP_OK) {
        cap_tailscale_write_mutation_error(output, output_size, "reconnect_failed",
                                           "Unable to reconnect Tailscale.");
        return err;
    }

    cap_tailscale_copy_text(result.message, sizeof(result.message), "Reconnect requested.");
    cap_tailscale_copy_text(result.exit_state, sizeof(result.exit_state), status.exit_state);
    cap_tailscale_copy_text(result.egress, sizeof(result.egress), status.egress);
    result.persisted = true;
    err = cap_tailscale_render_mutation_json(&result, output, output_size);
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
    if (claw_cap_group_exists(cap_tailscale.group_id)) {
        return ESP_OK;
    }
    return claw_cap_register_group(&cap_tailscale);
}
