/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>

#include "http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_SERVER_TAILSCALE_REQUEST_BODY_MAX 256u

typedef enum {
    HTTP_SERVER_TAILSCALE_REQUEST_OK = 0,
    HTTP_SERVER_TAILSCALE_REQUEST_INVALID,
    HTTP_SERVER_TAILSCALE_REQUEST_NO_MEMORY,
} http_server_tailscale_request_result_t;

typedef enum {
    HTTP_SERVER_TAILSCALE_HTTP_OK = 200,
    HTTP_SERVER_TAILSCALE_HTTP_BAD_REQUEST = 400,
    HTTP_SERVER_TAILSCALE_HTTP_CONFLICT = 409,
    HTTP_SERVER_TAILSCALE_HTTP_INTERNAL_ERROR = 500,
    HTTP_SERVER_TAILSCALE_HTTP_UNAVAILABLE = 503,
} http_server_tailscale_http_status_t;

http_server_tailscale_request_result_t http_server_tailscale_parse_set_request(
    const char *body,
    size_t body_length,
    char selector[HTTP_SERVER_TAILSCALE_SELECTOR_LEN]);

/* Zero bytes and a JSON object with no members are both valid. */
http_server_tailscale_request_result_t http_server_tailscale_parse_empty_request(
    const char *body,
    size_t body_length);

http_server_tailscale_http_status_t http_server_tailscale_operation_http_status(
    const http_server_tailscale_operation_t *operation,
    esp_err_t callback_error);

/* Returns a malloc-owned complete JSON document, or NULL on allocation failure. */
char *http_server_tailscale_render_operation_json(
    const http_server_tailscale_operation_t *operation);

#ifdef __cplusplus
}
#endif
