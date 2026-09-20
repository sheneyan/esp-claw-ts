/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "http_server_priv.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

typedef struct { const char *body; size_t length; size_t offset; } request_stream_t;
static http_server_ctx_t context;
static httpd_uri_t routes[4];
static size_t route_count;
static char response_status[32];
static char response_body[2048];
static int save_calls;
static int connect_calls;
static size_t saved_count;
static http_server_wifi_profile_update_t saved[HTTP_SERVER_WIFI_PROFILE_MAX];
static size_t connected_index;

http_server_ctx_t *http_server_ctx(void) { return &context; }
char *http_server_alloc_scratch_buffer(void) { return malloc(HTTP_SERVER_SCRATCH_SIZE); }

esp_err_t http_server_send_json_response(httpd_req_t *req, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    esp_err_t err = json ? httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN) : ESP_ERR_NO_MEM;
    free(json);
    cJSON_Delete(root);
    return err;
}

esp_err_t http_server_parse_json_body(httpd_req_t *req, cJSON **out_root)
{
    request_stream_t *stream = req->aux;
    char *body = calloc(1, stream->length + 1u);
    if (!body) return ESP_ERR_NO_MEM;
    memcpy(body, stream->body, stream->length);
    *out_root = cJSON_Parse(body);
    free(body);
    return *out_root ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *uri)
{
    (void)server;
    CHECK(route_count < 4u);
    if (route_count >= 4u) return ESP_FAIL;
    routes[route_count++] = *uri;
    return ESP_OK;
}

int httpd_req_recv(httpd_req_t *req, char *buffer, size_t size)
{
    request_stream_t *stream = req->aux;
    size_t remaining = stream->length - stream->offset;
    size_t copied = remaining < size ? remaining : size;
    memcpy(buffer, stream->body + stream->offset, copied);
    stream->offset += copied;
    return (int)copied;
}

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status) { (void)req; strlcpy(response_status, status, sizeof(response_status)); return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type) { (void)req; (void)type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field, const char *value) { (void)req; (void)field; (void)value; return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t *req, const char *body, int length)
{
    (void)req;
    size_t count = length == HTTPD_RESP_USE_STRLEN ? strlen(body) : (size_t)length;
    CHECK(count < sizeof(response_body));
    if (count >= sizeof(response_body)) return ESP_FAIL;
    memcpy(response_body, body, count);
    response_body[count] = '\0';
    return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *body) { return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN); }
esp_err_t httpd_resp_send_err(httpd_req_t *req, int status, const char *message) { (void)status; return httpd_resp_sendstr(req, message); }
esp_err_t httpd_resp_send_500(httpd_req_t *req) { return httpd_resp_sendstr(req, "internal error"); }

static int get_profiles(http_server_wifi_profile_summary_t *profiles, int capacity)
{
    CHECK(capacity == HTTP_SERVER_WIFI_PROFILE_MAX);
    strlcpy(profiles[0].ssid, "Office", sizeof(profiles[0].ssid));
    profiles[0].configured = true;
    profiles[0].active = true;
    profiles[0].password_set = true;
    return HTTP_SERVER_WIFI_PROFILE_MAX;
}

static esp_err_t save_profiles(const http_server_wifi_profile_update_t *profiles, size_t count)
{
    ++save_calls;
    saved_count = count;
    memcpy(saved, profiles, count * sizeof(*profiles));
    return ESP_OK;
}

static esp_err_t connect_profile(size_t index)
{
    ++connect_calls;
    connected_index = index;
    return ESP_OK;
}

static esp_err_t (*find_handler(const char *uri, httpd_method_t method))(httpd_req_t *)
{
    for (size_t i = 0; i < route_count; ++i) if (routes[i].method == method && strcmp(routes[i].uri, uri) == 0) return routes[i].handler;
    return NULL;
}

static void invoke(const char *uri, httpd_method_t method, const char *body)
{
    request_stream_t stream = { .body = body, .length = strlen(body) };
    httpd_req_t req = { .content_len = stream.length, .aux = &stream };
    esp_err_t (*handler)(httpd_req_t *) = find_handler(uri, method);
    CHECK(handler != NULL);
    response_status[0] = response_body[0] = '\0';
    if (handler) CHECK(handler(&req) == ESP_OK);
}

static void test_get_is_password_safe(void)
{
    invoke("/api/wifi/profiles", HTTP_GET, "");
    CHECK(strstr(response_body, "\"ssid\":\"Office\"") != NULL);
    CHECK(strstr(response_body, "\"active\":true") != NULL);
    CHECK(strstr(response_body, "\"password_set\":true") != NULL);
    CHECK(strstr(response_body, "password\"") == NULL);
}

static void test_put_and_connect(void)
{
    invoke("/api/wifi/profiles", HTTP_PUT,
           "{\"profiles\":[{\"ssid\":\"Office\"},{\"ssid\":\"Phone\",\"password\":\"secret123\"},{\"ssid\":\"Guest\",\"clear_password\":true}]}");
    CHECK(save_calls == 1);
    CHECK(saved_count == 3u);
    CHECK(!saved[0].password_supplied);
    CHECK(saved[1].password_supplied && strcmp(saved[1].password, "secret123") == 0);
    CHECK(saved[2].clear_password);

    invoke("/api/wifi/profiles/connect", HTTP_POST, "{\"index\":1}");
    CHECK(connect_calls == 1);
    CHECK(connected_index == 1u);
    CHECK(strstr(response_body, "\"accepted\":true") != NULL);
}

static void test_rejects_duplicates_and_sixth_profile(void)
{
    invoke("/api/wifi/profiles", HTTP_PUT,
           "{\"profiles\":[{\"ssid\":\"Short\",\"password\":\"bad\"}]}");
    CHECK(save_calls == 1);
    CHECK(strcmp(response_status, "400 Bad Request") == 0);
    invoke("/api/wifi/profiles", HTTP_PUT, "{\"profiles\":[{\"ssid\":\"A\"},{\"ssid\":\"A\"}]}");
    CHECK(save_calls == 1);
    CHECK(strcmp(response_status, "400 Bad Request") == 0);
    invoke("/api/wifi/profiles", HTTP_PUT,
           "{\"profiles\":[{\"ssid\":\"1\"},{\"ssid\":\"2\"},{\"ssid\":\"3\"},{\"ssid\":\"4\"},{\"ssid\":\"5\"},{\"ssid\":\"6\"}]}");
    CHECK(save_calls == 1);
    CHECK(strcmp(response_status, "400 Bad Request") == 0);
}

int main(void)
{
    context.services.get_wifi_profiles = get_profiles;
    context.services.save_wifi_profiles = save_profiles;
    context.services.connect_wifi_profile = connect_profile;
    CHECK(http_server_register_wifi_profiles_routes((httpd_handle_t)1) == ESP_OK);
    test_get_is_password_safe();
    test_put_and_connect();
    test_rejects_duplicates_and_sixth_profile();
    if (failures) return 1;
    puts("http_server Wi-Fi profile handler tests passed");
    return 0;
}
