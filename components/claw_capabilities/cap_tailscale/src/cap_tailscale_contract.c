/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_tailscale_contract.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *const s_descriptor_ids[] = {
    CAP_TAILSCALE_TOOL_STATUS,
    CAP_TAILSCALE_TOOL_LIST_EXIT_NODES,
    CAP_TAILSCALE_TOOL_SET_EXIT_NODE,
    CAP_TAILSCALE_TOOL_CLEAR_EXIT_NODE,
    CAP_TAILSCALE_TOOL_RECONNECT,
};

typedef struct {
    char *output;
    size_t output_size;
    size_t length;
    bool overflow;
} cap_tailscale_json_writer_t;

static void cap_tailscale_json_append_char(cap_tailscale_json_writer_t *writer, char value)
{
    if (writer->overflow || writer->length + 1 >= writer->output_size) {
        writer->overflow = true;
        return;
    }

    writer->output[writer->length++] = value;
    writer->output[writer->length] = '\0';
}

static void cap_tailscale_json_append_literal(cap_tailscale_json_writer_t *writer, const char *value)
{
    while (*value != '\0') {
        cap_tailscale_json_append_char(writer, *value++);
    }
}

static void cap_tailscale_json_append_uint(cap_tailscale_json_writer_t *writer, uint64_t value)
{
    char number[32];

    snprintf(number, sizeof(number), "%" PRIu64, value);
    cap_tailscale_json_append_literal(writer, number);
}

static void cap_tailscale_json_append_int(cap_tailscale_json_writer_t *writer, int value)
{
    char number[24];

    snprintf(number, sizeof(number), "%d", value);
    cap_tailscale_json_append_literal(writer, number);
}

static void cap_tailscale_json_append_bool(cap_tailscale_json_writer_t *writer, bool value)
{
    cap_tailscale_json_append_literal(writer, value ? "true" : "false");
}

static void cap_tailscale_json_append_string(cap_tailscale_json_writer_t *writer,
                                              const char *value,
                                              size_t value_capacity)
{
    static const char hex[] = "0123456789abcdef";
    size_t index;

    cap_tailscale_json_append_char(writer, '"');
    for (index = 0; index < value_capacity && value[index] != '\0'; ++index) {
        unsigned char current = (unsigned char)value[index];

        switch (current) {
            case '"':
                cap_tailscale_json_append_literal(writer, "\\\"");
                break;
            case '\\':
                cap_tailscale_json_append_literal(writer, "\\\\");
                break;
            case '\b':
                cap_tailscale_json_append_literal(writer, "\\b");
                break;
            case '\f':
                cap_tailscale_json_append_literal(writer, "\\f");
                break;
            case '\n':
                cap_tailscale_json_append_literal(writer, "\\n");
                break;
            case '\r':
                cap_tailscale_json_append_literal(writer, "\\r");
                break;
            case '\t':
                cap_tailscale_json_append_literal(writer, "\\t");
                break;
            default:
                if (current < 0x20U) {
                    cap_tailscale_json_append_literal(writer, "\\u00");
                    cap_tailscale_json_append_char(writer, hex[current >> 4]);
                    cap_tailscale_json_append_char(writer, hex[current & 0x0fU]);
                } else {
                    cap_tailscale_json_append_char(writer, (char)current);
                }
                break;
        }
    }
    cap_tailscale_json_append_char(writer, '"');
}

static esp_err_t cap_tailscale_json_finish(cap_tailscale_json_writer_t *writer)
{
    if (writer->overflow) {
        writer->output[0] = '\0';
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void cap_tailscale_json_append_region(cap_tailscale_json_writer_t *writer,
                                              const cap_tailscale_region_t *region)
{
    cap_tailscale_json_append_literal(writer, "{\"id\":");
    cap_tailscale_json_append_uint(writer, region->id);
    cap_tailscale_json_append_literal(writer, ",\"name\":");
    cap_tailscale_json_append_string(writer, region->name, sizeof(region->name));
    cap_tailscale_json_append_char(writer, '}');
}

esp_err_t cap_tailscale_validate_read_args(size_t unexpected_field_count)
{
    return unexpected_field_count == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t cap_tailscale_validate_mutation_confirmation(bool user_confirmed_present,
                                                        bool user_confirmed_is_bool,
                                                        bool user_confirmed,
                                                        size_t unexpected_field_count)
{
    if (unexpected_field_count != 0 || !user_confirmed_present || !user_confirmed_is_bool) {
        return ESP_ERR_INVALID_ARG;
    }

    return user_confirmed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cap_tailscale_validate_mutation_args(bool user_confirmed_present,
                                               bool user_confirmed,
                                               size_t unexpected_field_count)
{
    return cap_tailscale_validate_mutation_confirmation(user_confirmed_present,
                                                        true,
                                                        user_confirmed,
                                                        unexpected_field_count);
}

esp_err_t cap_tailscale_normalize_selector(const char *selector,
                                           char *out_selector,
                                           size_t out_selector_size)
{
    const char *start;
    const char *end;
    size_t length;

    if (selector == NULL || out_selector == NULL || out_selector_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    start = selector;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }

    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        --end;
    }

    length = (size_t)(end - start);
    if (length == 0 || length >= CAP_TAILSCALE_HOSTNAME_LEN || length >= out_selector_size) {
        return ESP_ERR_INVALID_ARG;
    }

    memmove(out_selector, start, length);
    out_selector[length] = '\0';
    return ESP_OK;
}

static bool parse_ipv4_octet(const char **cursor, unsigned int *out_value)
{
    const char *value_start = *cursor;
    unsigned int value = 0;
    size_t digit_count = 0;

    while (**cursor >= '0' && **cursor <= '9') {
        if (digit_count == 3) {
            return false;
        }
        value = value * 10U + (unsigned int)(**cursor - '0');
        if (value > 255U) {
            return false;
        }
        ++digit_count;
        ++*cursor;
    }

    if (digit_count == 0 || (digit_count > 1 && *value_start == '0')) {
        return false;
    }

    *out_value = value;
    return true;
}

bool cap_tailscale_selector_is_cgnat(const char *selector)
{
    const char *cursor;
    unsigned int octets[4];
    size_t index;

    if (selector == NULL) {
        return false;
    }

    cursor = selector;
    for (index = 0; index < 4; ++index) {
        if (!parse_ipv4_octet(&cursor, &octets[index])) {
            return false;
        }
        if (index < 3) {
            if (*cursor != '.') {
                return false;
            }
            ++cursor;
        }
    }

    if (*cursor != '\0') {
        return false;
    }

    return octets[0] == 100U && octets[1] >= 64U && octets[1] <= 127U;
}

static bool cap_tailscale_selector_is_ipv4_like(const char *selector)
{
    const char *cursor = selector;
    size_t segment;

    /*
     * Three leading decimal components are enough to classify an otherwise
     * malformed value as an IPv4 selector rather than silently accepting it
     * as a hostname.  Ordinary hostnames such as node.example remain outside
     * this policy.
     */
    for (segment = 0; segment < 3; ++segment) {
        const char *segment_start = cursor;

        while (*cursor >= '0' && *cursor <= '9') {
            ++cursor;
        }
        if (cursor == segment_start) {
            return false;
        }
        if (segment < 2) {
            if (*cursor != '.') {
                return false;
            }
            ++cursor;
        }
    }
    return true;
}

static bool cap_tailscale_selector_is_legacy_numeric_form(const char *selector)
{
    bool saw_dot = false;
    const char *cursor;

    for (cursor = selector; *cursor != '\0';) {
        const char *token_start = cursor;
        bool hex_prefixed;

        while (*cursor != '\0' && *cursor != '.') {
            ++cursor;
        }
        if (cursor == token_start) {
            return false;
        }
        hex_prefixed = cursor - token_start >= 2 && token_start[0] == '0' &&
                       (token_start[1] == 'x' || token_start[1] == 'X');
        if (!hex_prefixed) {
            const char *token;

            for (token = token_start; token < cursor; ++token) {
                if (*token < '0' || *token > '9') {
                    return false;
                }
            }
        }
        if (*cursor == '.') {
            saw_dot = true;
            ++cursor;
        }
    }
    return saw_dot || selector[0] != '\0';
}

esp_err_t cap_tailscale_validate_selector(const char *selector,
                                          char *out_selector,
                                          size_t out_selector_size)
{
    esp_err_t err = cap_tailscale_normalize_selector(selector, out_selector, out_selector_size);

    if (err != ESP_OK) {
        return err;
    }
    if (!cap_tailscale_selector_is_cgnat(out_selector) &&
        (cap_tailscale_selector_is_ipv4_like(out_selector) ||
         cap_tailscale_selector_is_legacy_numeric_form(out_selector))) {
        out_selector[0] = '\0';
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

size_t cap_tailscale_bound_exit_node_count(size_t count)
{
    return count > CAP_TAILSCALE_MAX_EXIT_NODES ? CAP_TAILSCALE_MAX_EXIT_NODES : count;
}

size_t cap_tailscale_bound_derp_rtt_count(size_t count)
{
    return count > CAP_TAILSCALE_MAX_DERP_RTTS ? CAP_TAILSCALE_MAX_DERP_RTTS : count;
}

size_t cap_tailscale_descriptor_count(void)
{
    return sizeof(s_descriptor_ids) / sizeof(s_descriptor_ids[0]);
}

const char *cap_tailscale_descriptor_id(size_t index)
{
    return index < cap_tailscale_descriptor_count() ? s_descriptor_ids[index] : NULL;
}

esp_err_t cap_tailscale_render_status_json(const cap_tailscale_status_t *status,
                                           char *output,
                                           size_t output_size)
{
    cap_tailscale_json_writer_t writer;
    size_t index;
    size_t rtt_count;

    if (status == NULL || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    writer = (cap_tailscale_json_writer_t) {
        .output = output,
        .output_size = output_size,
    };
    output[0] = '\0';
    rtt_count = cap_tailscale_bound_derp_rtt_count(status->derp_rtt_count);

    cap_tailscale_json_append_literal(&writer, "{\"ok\":true,\"enabled\":");
    cap_tailscale_json_append_bool(&writer, status->enabled);
    cap_tailscale_json_append_literal(&writer, ",\"connected\":");
    cap_tailscale_json_append_bool(&writer, status->connected);
    cap_tailscale_json_append_literal(&writer, ",\"hostname\":");
    cap_tailscale_json_append_string(&writer, status->hostname, sizeof(status->hostname));
    cap_tailscale_json_append_literal(&writer, ",\"vpn_ip\":");
    cap_tailscale_json_append_string(&writer, status->vpn_ip, sizeof(status->vpn_ip));
    cap_tailscale_json_append_literal(&writer, ",\"path\":");
    cap_tailscale_json_append_string(&writer, status->path, sizeof(status->path));
    cap_tailscale_json_append_literal(&writer, ",\"peer_count\":");
    cap_tailscale_json_append_int(&writer, status->peer_count);
    cap_tailscale_json_append_literal(&writer, ",\"peer_online\":");
    cap_tailscale_json_append_int(&writer, status->peer_online);
    cap_tailscale_json_append_literal(&writer, ",\"exit_node\":");
    cap_tailscale_json_append_string(&writer, status->exit_node, sizeof(status->exit_node));
    cap_tailscale_json_append_literal(&writer, ",\"state\":");
    cap_tailscale_json_append_string(&writer, status->exit_state, sizeof(status->exit_state));
    cap_tailscale_json_append_literal(&writer, ",\"egress\":");
    cap_tailscale_json_append_string(&writer, status->egress, sizeof(status->egress));
    cap_tailscale_json_append_literal(&writer, ",\"last_error\":");
    cap_tailscale_json_append_string(&writer, status->last_error, sizeof(status->last_error));
    cap_tailscale_json_append_literal(&writer, ",\"derp\":{\"active\":");
    cap_tailscale_json_append_region(&writer, &status->derp_active);
    cap_tailscale_json_append_literal(&writer, ",\"default\":");
    cap_tailscale_json_append_region(&writer, &status->derp_default);
    cap_tailscale_json_append_literal(&writer, ",\"rtt_count\":");
    cap_tailscale_json_append_uint(&writer, rtt_count);
    cap_tailscale_json_append_literal(&writer, ",\"rtts\":[");
    for (index = 0; index < rtt_count; ++index) {
        if (index != 0) {
            cap_tailscale_json_append_char(&writer, ',');
        }
        cap_tailscale_json_append_literal(&writer, "{\"region\":");
        cap_tailscale_json_append_region(&writer, &status->derp_rtts[index].region);
        cap_tailscale_json_append_literal(&writer, ",\"rtt_ms\":");
        cap_tailscale_json_append_uint(&writer, status->derp_rtts[index].rtt_ms);
        cap_tailscale_json_append_literal(&writer, ",\"timed_out\":");
        cap_tailscale_json_append_bool(&writer, status->derp_rtts[index].timed_out);
        cap_tailscale_json_append_char(&writer, '}');
    }
    cap_tailscale_json_append_literal(&writer, "]},\"timing\":{\"derp_heartbeat_age_ms\":");
    cap_tailscale_json_append_uint(&writer, status->derp_heartbeat_age_ms);
    cap_tailscale_json_append_literal(&writer, ",\"control_rx_age_ms\":");
    cap_tailscale_json_append_uint(&writer, status->control_rx_age_ms);
    cap_tailscale_json_append_literal(&writer, "},\"reconnect\":{\"coord_watchdog\":");
    cap_tailscale_json_append_uint(&writer, status->reconnect_coord_watchdog);
    cap_tailscale_json_append_literal(&writer, ",\"coord_transport\":");
    cap_tailscale_json_append_uint(&writer, status->reconnect_coord_transport);
    cap_tailscale_json_append_literal(&writer, ",\"derp_watchdog\":");
    cap_tailscale_json_append_uint(&writer, status->reconnect_derp_watchdog);
    cap_tailscale_json_append_literal(&writer, ",\"derp_retry\":");
    cap_tailscale_json_append_uint(&writer, status->reconnect_derp_retry);
    cap_tailscale_json_append_literal(&writer, "}}");

    return cap_tailscale_json_finish(&writer);
}

esp_err_t cap_tailscale_render_exit_nodes_json(const cap_tailscale_exit_node_t *nodes,
                                               size_t count,
                                               char *output,
                                               size_t output_size)
{
    cap_tailscale_json_writer_t writer;
    size_t index;

    if ((nodes == NULL && count != 0) || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    writer = (cap_tailscale_json_writer_t) {
        .output = output,
        .output_size = output_size,
    };
    output[0] = '\0';
    count = cap_tailscale_bound_exit_node_count(count);

    cap_tailscale_json_append_literal(&writer, "{\"ok\":true,\"exit_nodes\":[");
    for (index = 0; index < count; ++index) {
        if (index != 0) {
            cap_tailscale_json_append_char(&writer, ',');
        }
        cap_tailscale_json_append_literal(&writer, "{\"ip\":");
        cap_tailscale_json_append_string(&writer, nodes[index].ip, sizeof(nodes[index].ip));
        cap_tailscale_json_append_literal(&writer, ",\"hostname\":");
        cap_tailscale_json_append_string(&writer, nodes[index].hostname, sizeof(nodes[index].hostname));
        cap_tailscale_json_append_literal(&writer, ",\"online\":");
        cap_tailscale_json_append_bool(&writer, nodes[index].online);
        cap_tailscale_json_append_literal(&writer, ",\"direct\":");
        cap_tailscale_json_append_bool(&writer, nodes[index].direct);
        cap_tailscale_json_append_literal(&writer, ",\"derp_region\":");
        cap_tailscale_json_append_region(&writer, &nodes[index].derp_region);
        cap_tailscale_json_append_char(&writer, '}');
    }
    cap_tailscale_json_append_literal(&writer, "]}");

    return cap_tailscale_json_finish(&writer);
}

esp_err_t cap_tailscale_render_mutation_json(const cap_tailscale_mutation_result_t *result,
                                             char *output,
                                             size_t output_size)
{
    cap_tailscale_json_writer_t writer;

    if (result == NULL || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    writer = (cap_tailscale_json_writer_t) {
        .output = output,
        .output_size = output_size,
    };
    output[0] = '\0';

    cap_tailscale_json_append_literal(&writer, "{\"ok\":");
    cap_tailscale_json_append_bool(&writer, result->ok);
    cap_tailscale_json_append_literal(&writer, ",\"error\":");
    cap_tailscale_json_append_string(&writer, result->error, sizeof(result->error));
    cap_tailscale_json_append_literal(&writer, ",\"message\":");
    cap_tailscale_json_append_string(&writer, result->message, sizeof(result->message));
    cap_tailscale_json_append_literal(&writer, ",\"selected_node\":{\"ip\":");
    cap_tailscale_json_append_string(&writer, result->selected_ip, sizeof(result->selected_ip));
    cap_tailscale_json_append_literal(&writer, ",\"hostname\":");
    cap_tailscale_json_append_string(&writer, result->selected_hostname, sizeof(result->selected_hostname));
    cap_tailscale_json_append_literal(&writer, "},\"state\":");
    cap_tailscale_json_append_string(&writer, result->exit_state, sizeof(result->exit_state));
    cap_tailscale_json_append_literal(&writer, ",\"egress\":");
    cap_tailscale_json_append_string(&writer, result->egress, sizeof(result->egress));
    cap_tailscale_json_append_literal(&writer, ",\"persisted\":");
    cap_tailscale_json_append_bool(&writer, result->persisted);
    cap_tailscale_json_append_char(&writer, '}');

    return cap_tailscale_json_finish(&writer);
}

esp_err_t cap_tailscale_render_error_json(const char *error,
                                          const char *message,
                                          char *output,
                                          size_t output_size)
{
    cap_tailscale_json_writer_t writer;

    if (error == NULL || message == NULL || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    writer = (cap_tailscale_json_writer_t) {
        .output = output,
        .output_size = output_size,
    };
    output[0] = '\0';
    cap_tailscale_json_append_literal(&writer, "{\"ok\":false,\"error\":");
    cap_tailscale_json_append_string(&writer, error, strlen(error));
    cap_tailscale_json_append_literal(&writer, ",\"message\":");
    cap_tailscale_json_append_string(&writer, message, strlen(message));
    cap_tailscale_json_append_char(&writer, '}');
    return cap_tailscale_json_finish(&writer);
}
