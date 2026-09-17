/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "http_server_priv.h"

#include <stdlib.h>

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
    };

    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(server, &handlers[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
