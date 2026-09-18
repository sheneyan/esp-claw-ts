/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "http_server_priv.h"
#include "http_server_tailscale_request.h"

#include <stdlib.h>
#include <string.h>

#define TAILSCALE_RESPONSE_INTERNAL_JSON                                      \
    "{\"ok\":false,\"error\":\"internal_error\","                       \
    "\"message\":\"Unable to complete the request.\","                    \
    "\"selected_ip\":\"\",\"selected_hostname\":\"\","                 \
    "\"exit_state\":\"\",\"egress\":\"\",\"persisted\":false,"       \
    "\"rollback_attempted\":false,\"rollback_recovered\":false}"

static const char *tailscale_http_status_text(
    http_server_tailscale_http_status_t status)
{
    switch (status) {
    case HTTP_SERVER_TAILSCALE_HTTP_OK: return "200 OK";
    case HTTP_SERVER_TAILSCALE_HTTP_BAD_REQUEST: return "400 Bad Request";
    case HTTP_SERVER_TAILSCALE_HTTP_CONFLICT: return "409 Conflict";
    case HTTP_SERVER_TAILSCALE_HTTP_UNAVAILABLE:
        return "503 Service Unavailable";
    default: return "500 Internal Server Error";
    }
}

static void tailscale_operation_error(http_server_tailscale_operation_t *operation,
                                      const char *error,
                                      const char *message)
{
    memset(operation, 0, sizeof(*operation));
    strlcpy(operation->error, error, sizeof(operation->error));
    strlcpy(operation->message, message, sizeof(operation->message));
}

static esp_err_t tailscale_send_operation(httpd_req_t *req,
                                          http_server_tailscale_http_status_t status,
                                          const http_server_tailscale_operation_t *operation)
{
    char *payload = http_server_tailscale_render_operation_json(operation);
    if (!payload) {
        status = HTTP_SERVER_TAILSCALE_HTTP_INTERNAL_ERROR;
        payload = NULL;
    }

    esp_err_t err = httpd_resp_set_status(req, tailscale_http_status_text(status));
    if (err == ESP_OK) {
        err = httpd_resp_set_type(req, "application/json");
    }
    if (err == ESP_OK) {
        err = httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    }
    if (err == ESP_OK) {
        err = httpd_resp_sendstr(
            req, payload ? payload : TAILSCALE_RESPONSE_INTERNAL_JSON);
    }
    free(payload);
    return err;
}

static esp_err_t tailscale_send_fixed_error(httpd_req_t *req,
                                            http_server_tailscale_http_status_t status,
                                            const char *error,
                                            const char *message)
{
    http_server_tailscale_operation_t *operation = calloc(1, sizeof(*operation));
    if (!operation) {
        return tailscale_send_operation(
            req, HTTP_SERVER_TAILSCALE_HTTP_INTERNAL_ERROR, NULL);
    }
    tailscale_operation_error(operation, error, message);
    esp_err_t err = tailscale_send_operation(req, status, operation);
    free(operation);
    return err;
}

static esp_err_t tailscale_read_request_body(httpd_req_t *req,
                                             char **out_body,
                                             size_t *out_length)
{
    if (!req || !out_body || !out_length ||
        req->content_len > HTTP_SERVER_TAILSCALE_REQUEST_BODY_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_body = NULL;
    *out_length = (size_t)req->content_len;
    if (req->content_len == 0) {
        return ESP_OK;
    }

    char *body = http_server_alloc_scratch_buffer();
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0u;
    while (received < (size_t)req->content_len) {
        int chunk = httpd_req_recv(req, body + received,
                                   (size_t)req->content_len - received);
        if (chunk <= 0) {
            free(body);
            return ESP_ERR_INVALID_RESPONSE;
        }
        received += (size_t)chunk;
    }
    body[received] = '\0';
    *out_body = body;
    return ESP_OK;
}

static esp_err_t tailscale_send_request_error(
    httpd_req_t *req,
    http_server_tailscale_request_result_t parse_result)
{
    if (parse_result == HTTP_SERVER_TAILSCALE_REQUEST_NO_MEMORY) {
        return tailscale_send_fixed_error(
            req, HTTP_SERVER_TAILSCALE_HTTP_INTERNAL_ERROR, "internal_error",
            "Unable to parse the request.");
    }
    return tailscale_send_fixed_error(
        req, HTTP_SERVER_TAILSCALE_HTTP_BAD_REQUEST, "invalid_request",
        "Request body is invalid.");
}

static esp_err_t tailscale_set_exit_node_handler(httpd_req_t *req)
{
    http_server_ctx_t *ctx = http_server_ctx();
    if (!ctx->services.set_tailscale_exit_node) {
        return tailscale_send_fixed_error(
            req, HTTP_SERVER_TAILSCALE_HTTP_UNAVAILABLE, "unavailable",
            "Tailscale control is unavailable.");
    }

    char *body = NULL;
    size_t body_length = 0u;
    esp_err_t err = tailscale_read_request_body(req, &body, &body_length);
    if (err != ESP_OK) {
        return tailscale_send_fixed_error(
            req, err == ESP_ERR_NO_MEM
                     ? HTTP_SERVER_TAILSCALE_HTTP_INTERNAL_ERROR
                     : HTTP_SERVER_TAILSCALE_HTTP_BAD_REQUEST,
            err == ESP_ERR_NO_MEM ? "internal_error" : "invalid_request",
            err == ESP_ERR_NO_MEM ? "Unable to read the request."
                                  : "Request body is invalid.");
    }

    char selector[HTTP_SERVER_TAILSCALE_SELECTOR_LEN];
    http_server_tailscale_request_result_t parse_result =
        http_server_tailscale_parse_set_request(body, body_length, selector);
    free(body);
    if (parse_result != HTTP_SERVER_TAILSCALE_REQUEST_OK) {
        return tailscale_send_request_error(req, parse_result);
    }

    http_server_tailscale_operation_t *operation = calloc(1, sizeof(*operation));
    if (!operation) {
        return tailscale_send_fixed_error(
            req, HTTP_SERVER_TAILSCALE_HTTP_INTERNAL_ERROR, "internal_error",
            "Unable to start the operation.");
    }
    err = ctx->services.set_tailscale_exit_node(selector, operation);
    http_server_tailscale_http_status_t status =
        http_server_tailscale_operation_http_status(operation, err);
    if (err != ESP_OK) {
        tailscale_operation_error(operation, "internal_error",
                                  "Tailscale control failed internally.");
    }
    esp_err_t send_err = tailscale_send_operation(req, status, operation);
    free(operation);
    return send_err;
}

static esp_err_t tailscale_empty_action_handler(
    httpd_req_t *req,
    esp_err_t (*callback)(http_server_tailscale_operation_t *out))
{
    if (!callback) {
        return tailscale_send_fixed_error(
            req, HTTP_SERVER_TAILSCALE_HTTP_UNAVAILABLE, "unavailable",
            "Tailscale control is unavailable.");
    }

    char *body = NULL;
    size_t body_length = 0u;
    esp_err_t err = tailscale_read_request_body(req, &body, &body_length);
    if (err != ESP_OK) {
        return tailscale_send_fixed_error(
            req, err == ESP_ERR_NO_MEM
                     ? HTTP_SERVER_TAILSCALE_HTTP_INTERNAL_ERROR
                     : HTTP_SERVER_TAILSCALE_HTTP_BAD_REQUEST,
            err == ESP_ERR_NO_MEM ? "internal_error" : "invalid_request",
            err == ESP_ERR_NO_MEM ? "Unable to read the request."
                                  : "Request body is invalid.");
    }

    http_server_tailscale_request_result_t parse_result =
        http_server_tailscale_parse_empty_request(body, body_length);
    free(body);
    if (parse_result != HTTP_SERVER_TAILSCALE_REQUEST_OK) {
        return tailscale_send_request_error(req, parse_result);
    }

    http_server_tailscale_operation_t *operation = calloc(1, sizeof(*operation));
    if (!operation) {
        return tailscale_send_fixed_error(
            req, HTTP_SERVER_TAILSCALE_HTTP_INTERNAL_ERROR, "internal_error",
            "Unable to start the operation.");
    }
    err = callback(operation);
    http_server_tailscale_http_status_t status =
        http_server_tailscale_operation_http_status(operation, err);
    if (err != ESP_OK) {
        tailscale_operation_error(operation, "internal_error",
                                  "Tailscale control failed internally.");
    }
    esp_err_t send_err = tailscale_send_operation(req, status, operation);
    free(operation);
    return send_err;
}

static esp_err_t tailscale_clear_exit_node_handler(httpd_req_t *req)
{
    return tailscale_empty_action_handler(
        req, http_server_ctx()->services.clear_tailscale_exit_node);
}

static esp_err_t tailscale_reconnect_handler(httpd_req_t *req)
{
    return tailscale_empty_action_handler(
        req, http_server_ctx()->services.reconnect_tailscale);
}

static esp_err_t tailscale_unavailable(httpd_req_t *req, const char *message)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}

static bool add_status_fields(cJSON *root, const http_server_tailscale_status_t *status)
{
    return cJSON_AddBoolToObject(root, "enabled", status->enabled) != NULL &&
           cJSON_AddBoolToObject(root, "connected", status->connected) != NULL &&
           cJSON_AddStringToObject(root, "vpn_ip", status->vpn_ip) != NULL &&
           cJSON_AddStringToObject(root, "path", status->path) != NULL &&
           cJSON_AddNumberToObject(root, "peer_count", status->peer_count) != NULL &&
           cJSON_AddNumberToObject(root, "peer_online", status->peer_online) != NULL &&
           cJSON_AddStringToObject(root, "exit_node", status->exit_node) != NULL &&
           cJSON_AddStringToObject(root, "exit_state", status->exit_state) != NULL &&
           cJSON_AddStringToObject(root, "egress", status->egress) != NULL &&
           cJSON_AddStringToObject(root, "dns_egress", status->dns_egress) != NULL &&
           cJSON_AddBoolToObject(root, "dns_bypass_active",
                                 status->dns_bypass_active) != NULL &&
           cJSON_AddNumberToObject(root, "dns_bypass_count",
                                   status->dns_bypass_count) != NULL &&
           cJSON_AddStringToObject(root, "last_error", status->last_error) != NULL &&
           cJSON_AddBoolToObject(root, "auth_key_set", status->auth_key_set) != NULL &&
           cJSON_AddNumberToObject(root, "heap_internal_free",
                                   (double)status->heap_internal_free) != NULL &&
           cJSON_AddNumberToObject(root, "heap_internal_largest",
                                   (double)status->heap_internal_largest) != NULL &&
           cJSON_AddNumberToObject(root, "heap_psram_free",
                                   (double)status->heap_psram_free) != NULL;
}

static esp_err_t tailscale_status_handler(httpd_req_t *req)
{
    http_server_ctx_t *ctx = http_server_ctx();
    if (!ctx->services.get_tailscale_status) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Tailscale is unavailable");
    }

    http_server_tailscale_status_t *status = calloc(1, sizeof(*status));
    if (!status) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ctx->services.get_tailscale_status(status);
    if (err != ESP_OK) {
        free(status);
        return tailscale_unavailable(req, "Tailscale status is unavailable");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root || !add_status_fields(root, status)) {
        free(status);
        cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    free(status);
    return http_server_send_json_response(req, root);
}

static bool add_exit_node_fields(cJSON *item, const http_server_tailscale_exit_node_t *node)
{
    return cJSON_AddStringToObject(item, "ip", node->ip) != NULL &&
           cJSON_AddStringToObject(item, "hostname", node->hostname) != NULL &&
           cJSON_AddBoolToObject(item, "online", node->online) != NULL &&
           cJSON_AddBoolToObject(item, "direct", node->direct) != NULL &&
           cJSON_AddNumberToObject(item, "derp_region", node->derp_region) != NULL;
}

static esp_err_t tailscale_exit_nodes_handler(httpd_req_t *req)
{
    http_server_ctx_t *ctx = http_server_ctx();
    if (!ctx->services.get_tailscale_exit_nodes) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Tailscale is unavailable");
    }

    http_server_tailscale_exit_node_t *nodes =
        calloc(HTTP_SERVER_TAILSCALE_MAX_EXIT_NODES, sizeof(*nodes));
    if (!nodes) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    int count = ctx->services.get_tailscale_exit_nodes(
        nodes, HTTP_SERVER_TAILSCALE_MAX_EXIT_NODES);
    if (count < 0 || count > HTTP_SERVER_TAILSCALE_MAX_EXIT_NODES) {
        free(nodes);
        return tailscale_unavailable(req, "Tailscale exit nodes are unavailable");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    if (!root || !items || !cJSON_AddItemToObject(root, "items", items)) {
        free(nodes);
        cJSON_Delete(root);
        cJSON_Delete(items);
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item || !add_exit_node_fields(item, &nodes[i]) ||
            !cJSON_AddItemToArray(items, item)) {
            free(nodes);
            cJSON_Delete(item);
            cJSON_Delete(root);
            httpd_resp_send_500(req);
            return ESP_ERR_NO_MEM;
        }
    }

    free(nodes);
    return http_server_send_json_response(req, root);
}

esp_err_t http_server_register_tailscale_routes(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        { .uri = "/api/tailscale/status", .method = HTTP_GET, .handler = tailscale_status_handler },
        { .uri = "/api/tailscale/exit-nodes", .method = HTTP_GET,
          .handler = tailscale_exit_nodes_handler },
        { .uri = "/api/tailscale/exit-node", .method = HTTP_POST,
          .handler = tailscale_set_exit_node_handler },
        { .uri = "/api/tailscale/exit-node", .method = HTTP_DELETE,
          .handler = tailscale_clear_exit_node_handler },
        { .uri = "/api/tailscale/reconnect", .method = HTTP_POST,
          .handler = tailscale_reconnect_handler },
    };

    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(server, &handlers[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
