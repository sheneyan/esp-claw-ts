/* SPDX-License-Identifier: Apache-2.0 */
#include "http_server_priv.h"

#include <string.h>

static esp_err_t send_bad_request(httpd_req_t *req, const char *message)
{
    httpd_resp_set_status(req, "400 Bad Request");
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(req);
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", message);
    return http_server_send_json_response(req, root);
}

static esp_err_t wifi_profiles_get_handler(httpd_req_t *req)
{
    http_server_ctx_t *ctx = http_server_ctx();
    if (!ctx->services.get_wifi_profiles) return httpd_resp_send_500(req);

    http_server_wifi_profile_summary_t profiles[HTTP_SERVER_WIFI_PROFILE_MAX] = {0};
    int count = ctx->services.get_wifi_profiles(profiles, HTTP_SERVER_WIFI_PROFILE_MAX);
    if (count < 0 || count > HTTP_SERVER_WIFI_PROFILE_MAX) return httpd_resp_send_500(req);

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(req);
    cJSON *items = cJSON_AddArrayToObject(root, "profiles");
    if (!items) {
        cJSON_Delete(root);
        return httpd_resp_send_500(req);
    }
    for (int index = 0; index < count; ++index) {
        cJSON *item = cJSON_CreateObject();
        if (!item || !cJSON_AddItemToArray(items, item)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return httpd_resp_send_500(req);
        }
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddStringToObject(item, "ssid", profiles[index].ssid);
        cJSON_AddBoolToObject(item, "configured", profiles[index].configured);
        cJSON_AddBoolToObject(item, "active", profiles[index].active);
        cJSON_AddBoolToObject(item, "password_set", profiles[index].password_set);
    }
    return http_server_send_json_response(req, root);
}

static bool parse_profile(cJSON *item, http_server_wifi_profile_update_t *out)
{
    if (!cJSON_IsObject(item) || !out) return false;
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(item, "ssid");
    if (!cJSON_IsString(ssid)) return false;
    size_t ssid_len = strlen(ssid->valuestring);
    if (ssid_len == 0 || ssid_len >= sizeof(out->ssid)) return false;
    strlcpy(out->ssid, ssid->valuestring, sizeof(out->ssid));

    cJSON *clear = cJSON_GetObjectItemCaseSensitive(item, "clear_password");
    if (clear && !cJSON_IsBool(clear)) return false;
    out->clear_password = cJSON_IsTrue(clear);

    cJSON *password = cJSON_GetObjectItemCaseSensitive(item, "password");
    if (password && !cJSON_IsString(password)) return false;
    if (cJSON_IsString(password) && password->valuestring[0] != '\0') {
        size_t password_len = strlen(password->valuestring);
        if (password_len < 8 || password_len >= sizeof(out->password)) return false;
        strlcpy(out->password, password->valuestring, sizeof(out->password));
        out->password_supplied = true;
    }
    if (out->clear_password) {
        out->password[0] = '\0';
        out->password_supplied = false;
    }
    return true;
}

static esp_err_t wifi_profiles_put_handler(httpd_req_t *req)
{
    http_server_ctx_t *ctx = http_server_ctx();
    if (!ctx->services.save_wifi_profiles) return httpd_resp_send_500(req);
    cJSON *root = NULL;
    if (http_server_parse_json_body(req, &root) != ESP_OK || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_bad_request(req, "invalid_json");
    }
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "profiles");
    int count = cJSON_IsArray(items) ? cJSON_GetArraySize(items) : -1;
    if (count < 0 || count > HTTP_SERVER_WIFI_PROFILE_MAX) {
        cJSON_Delete(root);
        return send_bad_request(req, "invalid_profile_count");
    }

    http_server_wifi_profile_update_t profiles[HTTP_SERVER_WIFI_PROFILE_MAX] = {0};
    for (int index = 0; index < count; ++index) {
        if (!parse_profile(cJSON_GetArrayItem(items, index), &profiles[index])) {
            cJSON_Delete(root);
            return send_bad_request(req, "invalid_profile");
        }
        for (int previous = 0; previous < index; ++previous) {
            if (strcmp(profiles[index].ssid, profiles[previous].ssid) == 0) {
                cJSON_Delete(root);
                return send_bad_request(req, "duplicate_ssid");
            }
        }
    }
    cJSON_Delete(root);
    if (ctx->services.save_wifi_profiles(profiles, (size_t)count) != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    cJSON *response = cJSON_CreateObject();
    if (!response) return httpd_resp_send_500(req);
    cJSON_AddBoolToObject(response, "ok", true);
    return http_server_send_json_response(req, response);
}

static esp_err_t wifi_profiles_connect_handler(httpd_req_t *req)
{
    http_server_ctx_t *ctx = http_server_ctx();
    if (!ctx->services.connect_wifi_profile) return httpd_resp_send_500(req);
    cJSON *root = NULL;
    if (http_server_parse_json_body(req, &root) != ESP_OK || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_bad_request(req, "invalid_json");
    }
    cJSON *index_item = cJSON_GetObjectItemCaseSensitive(root, "index");
    if (!cJSON_IsNumber(index_item) || index_item->valuedouble != index_item->valueint ||
        index_item->valueint < 0 || index_item->valueint >= HTTP_SERVER_WIFI_PROFILE_MAX) {
        cJSON_Delete(root);
        return send_bad_request(req, "invalid_index");
    }
    size_t index = (size_t)index_item->valueint;
    cJSON_Delete(root);
    if (ctx->services.connect_wifi_profile(index) != ESP_OK) {
        return send_bad_request(req, "profile_unavailable");
    }
    cJSON *response = cJSON_CreateObject();
    if (!response) return httpd_resp_send_500(req);
    cJSON_AddBoolToObject(response, "accepted", true);
    return http_server_send_json_response(req, response);
}

esp_err_t http_server_register_wifi_profiles_routes(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        { .uri = "/api/wifi/profiles", .method = HTTP_GET, .handler = wifi_profiles_get_handler },
        { .uri = "/api/wifi/profiles", .method = HTTP_PUT, .handler = wifi_profiles_put_handler },
        { .uri = "/api/wifi/profiles/connect", .method = HTTP_POST, .handler = wifi_profiles_connect_handler },
    };
    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        esp_err_t err = httpd_register_uri_handler(server, &handlers[index]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
