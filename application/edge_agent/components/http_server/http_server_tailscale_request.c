/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "http_server_tailscale_request.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define TAILSCALE_ERROR_INVALID_NODE         "invalid_node"
#define TAILSCALE_ERROR_NOT_ENABLED          "not_enabled"
#define TAILSCALE_ERROR_NOT_CONNECTED        "not_connected"
#define TAILSCALE_ERROR_AMBIGUOUS_NODE       "ambiguous_node"
#define TAILSCALE_ERROR_NODE_NOT_FOUND       "node_not_found"
#define TAILSCALE_ERROR_NODE_OFFLINE         "node_offline"
#define TAILSCALE_ERROR_NOT_EXIT_NODE        "not_exit_node"
#define TAILSCALE_ERROR_SWITCH_TIMEOUT       "switch_timeout"
#define TAILSCALE_ERROR_BUSY                 "busy"
#define TAILSCALE_ERROR_RUNTIME_APPLY_FAILED "runtime_apply_failed"
#define TAILSCALE_ERROR_RECONNECT_FAILED     "reconnect_failed"

static bool is_json_hex_digit(unsigned char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static bool strict_json_lexically_valid(const char *body, size_t length)
{
    bool in_string = false;
    bool escaped = false;

    for (size_t index = 0u; index < length; ++index) {
        unsigned char value = (unsigned char)body[index];
        if (!in_string) {
            if (value < 0x20u && value != '\t' && value != '\n' &&
                value != '\r') {
                return false;
            }
            if (value == '"') {
                in_string = true;
            }
            continue;
        }

        if (escaped) {
            if (value == 'u') {
                if (index + 4u >= length) {
                    return false;
                }
                for (size_t digit = 1u; digit <= 4u; ++digit) {
                    if (!is_json_hex_digit((unsigned char)body[index + digit])) {
                        return false;
                    }
                }
                index += 4u;
            } else if (value != '"' && value != '\\' && value != '/' &&
                       value != 'b' && value != 'f' && value != 'n' &&
                       value != 'r' && value != 't') {
                return false;
            }
            escaped = false;
            continue;
        }

        if (value < 0x20u) {
            return false;
        }
        if (value == '\\') {
            escaped = true;
        } else if (value == '"') {
            in_string = false;
        }
    }

    return !in_string && !escaped;
}

static bool contains_decoded_nul_escape(const char *body, size_t length)
{
    for (size_t index = 0u; index + 4u < length; ++index) {
        if (body[index] != 'u' || body[index + 1u] != '0' ||
            body[index + 2u] != '0' || body[index + 3u] != '0' ||
            body[index + 4u] != '0') {
            continue;
        }

        size_t slashes = 0u;
        size_t cursor = index;
        while (cursor > 0u && body[cursor - 1u] == '\\') {
            --cursor;
            ++slashes;
        }
        if ((slashes & 1u) != 0u) {
            return true;
        }
    }
    return false;
}

static http_server_tailscale_request_result_t parse_complete_json(
    const char *body,
    size_t body_length,
    cJSON **out)
{
    if (!out || (body_length > 0u && !body) ||
        body_length > HTTP_SERVER_TAILSCALE_REQUEST_BODY_MAX ||
        (body_length > 0u && memchr(body, '\0', body_length) != NULL) ||
        !strict_json_lexically_valid(body, body_length) ||
        contains_decoded_nul_escape(body, body_length)) {
        return HTTP_SERVER_TAILSCALE_REQUEST_INVALID;
    }

    char *terminated = calloc(1u, body_length + 1u);
    if (!terminated) {
        return HTTP_SERVER_TAILSCALE_REQUEST_NO_MEMORY;
    }
    if (body_length > 0u) {
        memcpy(terminated, body, body_length);
    }

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(terminated, body_length + 1u,
                                            &parse_end, true);
    free(terminated);
    if (!root) {
        return HTTP_SERVER_TAILSCALE_REQUEST_INVALID;
    }

    *out = root;
    return HTTP_SERVER_TAILSCALE_REQUEST_OK;
}

http_server_tailscale_request_result_t http_server_tailscale_parse_set_request(
    const char *body,
    size_t body_length,
    char selector[HTTP_SERVER_TAILSCALE_SELECTOR_LEN])
{
    if (!selector) {
        return HTTP_SERVER_TAILSCALE_REQUEST_INVALID;
    }
    selector[0] = '\0';

    cJSON *root = NULL;
    http_server_tailscale_request_result_t result =
        parse_complete_json(body, body_length, &root);
    if (result != HTTP_SERVER_TAILSCALE_REQUEST_OK) {
        return result;
    }

    cJSON *node = root->child;
    if (!cJSON_IsObject(root) || !node || node->next || !node->string ||
        strcmp(node->string, "node") != 0 || !cJSON_IsString(node) ||
        !node->valuestring) {
        cJSON_Delete(root);
        return HTTP_SERVER_TAILSCALE_REQUEST_INVALID;
    }

    const char *begin = node->valuestring;
    while (*begin != '\0' && isspace((unsigned char)*begin)) {
        ++begin;
    }
    const char *end = begin + strlen(begin);
    while (end > begin && isspace((unsigned char)end[-1])) {
        --end;
    }
    size_t selector_length = (size_t)(end - begin);
    if (selector_length == 0u ||
        selector_length >= HTTP_SERVER_TAILSCALE_SELECTOR_LEN) {
        cJSON_Delete(root);
        return HTTP_SERVER_TAILSCALE_REQUEST_INVALID;
    }

    memcpy(selector, begin, selector_length);
    selector[selector_length] = '\0';
    cJSON_Delete(root);
    return HTTP_SERVER_TAILSCALE_REQUEST_OK;
}

http_server_tailscale_request_result_t http_server_tailscale_parse_empty_request(
    const char *body,
    size_t body_length)
{
    if (body_length == 0u) {
        return HTTP_SERVER_TAILSCALE_REQUEST_OK;
    }

    cJSON *root = NULL;
    http_server_tailscale_request_result_t result =
        parse_complete_json(body, body_length, &root);
    if (result != HTTP_SERVER_TAILSCALE_REQUEST_OK) {
        return result;
    }

    bool valid = cJSON_IsObject(root) && root->child == NULL;
    cJSON_Delete(root);
    return valid ? HTTP_SERVER_TAILSCALE_REQUEST_OK
                 : HTTP_SERVER_TAILSCALE_REQUEST_INVALID;
}

http_server_tailscale_http_status_t http_server_tailscale_operation_http_status(
    const http_server_tailscale_operation_t *operation,
    esp_err_t callback_error)
{
    if (callback_error != ESP_OK || !operation) {
        return HTTP_SERVER_TAILSCALE_HTTP_INTERNAL_ERROR;
    }
    if (operation->ok) {
        return HTTP_SERVER_TAILSCALE_HTTP_OK;
    }
    if (strcmp(operation->error, TAILSCALE_ERROR_INVALID_NODE) == 0) {
        return HTTP_SERVER_TAILSCALE_HTTP_BAD_REQUEST;
    }
    if (strcmp(operation->error, TAILSCALE_ERROR_NOT_ENABLED) == 0 ||
        strcmp(operation->error, TAILSCALE_ERROR_NOT_CONNECTED) == 0 ||
        strcmp(operation->error, TAILSCALE_ERROR_SWITCH_TIMEOUT) == 0 ||
        strcmp(operation->error, TAILSCALE_ERROR_RUNTIME_APPLY_FAILED) == 0 ||
        strcmp(operation->error, TAILSCALE_ERROR_RECONNECT_FAILED) == 0) {
        return HTTP_SERVER_TAILSCALE_HTTP_UNAVAILABLE;
    }
    if (strcmp(operation->error, TAILSCALE_ERROR_AMBIGUOUS_NODE) == 0 ||
        strcmp(operation->error, TAILSCALE_ERROR_NODE_NOT_FOUND) == 0 ||
        strcmp(operation->error, TAILSCALE_ERROR_NODE_OFFLINE) == 0 ||
        strcmp(operation->error, TAILSCALE_ERROR_NOT_EXIT_NODE) == 0 ||
        strcmp(operation->error, TAILSCALE_ERROR_BUSY) == 0) {
        return HTTP_SERVER_TAILSCALE_HTTP_CONFLICT;
    }
    return HTTP_SERVER_TAILSCALE_HTTP_INTERNAL_ERROR;
}

char *http_server_tailscale_render_operation_json(
    const http_server_tailscale_operation_t *operation)
{
    if (!operation) {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root ||
        !cJSON_AddBoolToObject(root, "ok", operation->ok) ||
        !cJSON_AddStringToObject(root, "error", operation->error) ||
        !cJSON_AddStringToObject(root, "message", operation->message) ||
        !cJSON_AddStringToObject(root, "selected_ip", operation->selected_ip) ||
        !cJSON_AddStringToObject(root, "selected_hostname",
                                 operation->selected_hostname) ||
        !cJSON_AddStringToObject(root, "exit_state", operation->exit_state) ||
        !cJSON_AddStringToObject(root, "egress", operation->egress) ||
        !cJSON_AddBoolToObject(root, "persisted", operation->persisted) ||
        !cJSON_AddBoolToObject(root, "rollback_attempted",
                               operation->rollback_attempted) ||
        !cJSON_AddBoolToObject(root, "rollback_recovered",
                               operation->rollback_recovered)) {
        cJSON_Delete(root);
        return NULL;
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}
