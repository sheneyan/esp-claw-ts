/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "http_server_priv.h"

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                                \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

typedef struct {
    const char *body;
    size_t length;
    size_t offset;
} request_stream_t;

static http_server_ctx_t test_context;
static httpd_uri_t routes[8];
static size_t route_count;
static int set_calls;
static int clear_calls;
static int reconnect_calls;
static char response_status[32];
static char response_body[512];

http_server_ctx_t *http_server_ctx(void)
{
    return &test_context;
}

char *http_server_alloc_scratch_buffer(void)
{
    return malloc(HTTP_SERVER_SCRATCH_SIZE);
}

esp_err_t http_server_send_json_response(httpd_req_t *req, cJSON *root)
{
    (void)req;
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t server,
                                     const httpd_uri_t *uri)
{
    (void)server;
    CHECK(route_count < sizeof(routes) / sizeof(routes[0]));
    if (route_count >= sizeof(routes) / sizeof(routes[0])) {
        return ESP_FAIL;
    }
    routes[route_count++] = *uri;
    return ESP_OK;
}

int httpd_req_recv(httpd_req_t *req, char *buffer, size_t buffer_size)
{
    request_stream_t *stream = req->aux;
    size_t remaining = stream->length - stream->offset;
    size_t copied = remaining < buffer_size ? remaining : buffer_size;
    if (copied == 0u) {
        return 0;
    }
    memcpy(buffer, stream->body + stream->offset, copied);
    stream->offset += copied;
    return (int)copied;
}

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status)
{
    (void)req;
    snprintf(response_status, sizeof(response_status), "%s", status);
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{
    (void)req;
    (void)type;
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field,
                             const char *value)
{
    (void)req;
    (void)field;
    (void)value;
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *req, const char *body, int length)
{
    (void)req;
    size_t body_length = length == HTTPD_RESP_USE_STRLEN
                             ? strlen(body)
                             : (size_t)length;
    CHECK(body_length < sizeof(response_body));
    if (body_length >= sizeof(response_body)) {
        return ESP_FAIL;
    }
    memcpy(response_body, body, body_length);
    response_body[body_length] = '\0';
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *body)
{
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t httpd_resp_send_err(httpd_req_t *req, int status,
                              const char *message)
{
    (void)status;
    return httpd_resp_sendstr(req, message);
}

esp_err_t httpd_resp_send_500(httpd_req_t *req)
{
    return httpd_resp_sendstr(req, "internal error");
}

static esp_err_t set_exit_node(const char *selector,
                               http_server_tailscale_operation_t *out)
{
    ++set_calls;
    out->ok = true;
    snprintf(out->error, sizeof(out->error), "ok");
    snprintf(out->message, sizeof(out->message), "%s", selector);
    return ESP_OK;
}

static esp_err_t clear_exit_node(http_server_tailscale_operation_t *out)
{
    ++clear_calls;
    out->ok = true;
    snprintf(out->error, sizeof(out->error), "ok");
    return ESP_OK;
}

static esp_err_t reconnect(http_server_tailscale_operation_t *out)
{
    ++reconnect_calls;
    out->ok = true;
    snprintf(out->error, sizeof(out->error), "ok");
    return ESP_OK;
}

static esp_err_t (*find_handler(const char *uri, httpd_method_t method))(
    httpd_req_t *req)
{
    for (size_t index = 0u; index < route_count; ++index) {
        if (routes[index].method == method &&
            strcmp(routes[index].uri, uri) == 0) {
            return routes[index].handler;
        }
    }
    return NULL;
}

static void invoke(const char *uri, httpd_method_t method,
                   const char *body, size_t body_length)
{
    request_stream_t stream = {
        .body = body,
        .length = body_length,
    };
    httpd_req_t req = {
        .content_len = body_length,
        .aux = &stream,
    };
    esp_err_t (*handler)(httpd_req_t *req) = find_handler(uri, method);
    CHECK(handler != NULL);
    if (!handler) {
        return;
    }
    response_status[0] = '\0';
    response_body[0] = '\0';
    CHECK(handler(&req) == ESP_OK);
}

static void test_malformed_bodies_never_reach_mutation_callbacks(void)
{
    const char raw_newline[] = "{\"node\":\"bad\nnode\"}";
    const char trailing_control[] = {'{', '}', 0x01};
    const char leading_vertical_tab[] = {0x0b, '{', '}'};

    invoke("/api/tailscale/exit-node", HTTP_POST,
           raw_newline, sizeof(raw_newline) - 1u);
    CHECK(set_calls == 0);
    CHECK(strcmp(response_status, "400 Bad Request") == 0);

    invoke("/api/tailscale/exit-node", HTTP_DELETE,
           trailing_control, sizeof(trailing_control));
    CHECK(clear_calls == 0);
    CHECK(strcmp(response_status, "400 Bad Request") == 0);

    invoke("/api/tailscale/reconnect", HTTP_POST,
           leading_vertical_tab, sizeof(leading_vertical_tab));
    CHECK(reconnect_calls == 0);
    CHECK(strcmp(response_status, "400 Bad Request") == 0);

    CHECK(set_calls == 0);
    CHECK(clear_calls == 0);
    CHECK(reconnect_calls == 0);
}

static void test_valid_body_reaches_callback_once(void)
{
    const char body[] = "{\"node\":\"exit.example\"}";
    invoke("/api/tailscale/exit-node", HTTP_POST, body, sizeof(body) - 1u);
    CHECK(set_calls == 1);
    CHECK(strcmp(response_status, "200 OK") == 0);
}

int main(void)
{
    test_context.services.set_tailscale_exit_node = set_exit_node;
    test_context.services.clear_tailscale_exit_node = clear_exit_node;
    test_context.services.reconnect_tailscale = reconnect;
    CHECK(http_server_register_tailscale_routes((httpd_handle_t)1) == ESP_OK);

    test_malformed_bodies_never_reach_mutation_callbacks();
    test_valid_body_reaches_callback_once();

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("http_server_tailscale handler tests passed");
    return 0;
}
