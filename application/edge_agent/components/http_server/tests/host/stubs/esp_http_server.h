#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef void *httpd_handle_t;
typedef int httpd_err_code_t;

typedef enum {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_DELETE,
} httpd_method_t;

typedef struct httpd_req {
    size_t content_len;
    void *aux;
} httpd_req_t;

typedef struct httpd_uri {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *req);
    void *user_ctx;
} httpd_uri_t;

#define HTTPD_RESP_USE_STRLEN (-1)
#define HTTPD_404_NOT_FOUND    404

esp_err_t httpd_register_uri_handler(httpd_handle_t server,
                                     const httpd_uri_t *uri);
int httpd_req_recv(httpd_req_t *req, char *buffer, size_t buffer_size);
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field,
                             const char *value);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *body, int length);
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *body);
esp_err_t httpd_resp_send_err(httpd_req_t *req, int status,
                              const char *message);
esp_err_t httpd_resp_send_500(httpd_req_t *req);
