/* SPDX-License-Identifier: Apache-2.0 */
#include "wifi_profile_worker.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "wifi_manager_profile_policy.h"
#include "wifi_profile_worker_config.h"

static const char *TAG = "wifi_profile_worker";

enum {
    WIFI_PROFILE_SCAN_LIMIT = 32,
    WIFI_PROFILE_CONNECT_TIMEOUT_MS = 15000,
    WIFI_PROFILE_RETRY_INTERVAL_MS = 30000,
    WIFI_PROFILE_WORKER_STACK = 5120,
};

typedef enum {
    WIFI_PROFILE_REQUEST_AUTO = 0,
    WIFI_PROFILE_REQUEST_MANUAL,
} wifi_profile_request_kind_t;

typedef struct {
    wifi_profile_request_kind_t kind;
    size_t index;
} wifi_profile_request_t;

struct wifi_profile_worker {
    wifi_profile_runtime_t *runtime;
    wifi_profile_worker_config_t base_config;
    QueueHandle_t queue;
    TaskHandle_t task;
    volatile bool busy;
};

static esp_err_t apply_profile(struct wifi_profile_worker *worker, size_t index)
{
    const wifi_profile_t *profile = &worker->runtime->profiles.entries[index];
    wifi_manager_config_t config = worker->base_config.config;
    config.sta_ssid = profile->ssid;
    config.sta_password = profile->password;
    esp_err_t err = wifi_manager_apply_sta_config(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Profile %u (%s) could not start: %s", (unsigned)index,
                 profile->ssid, esp_err_to_name(err));
        return err;
    }
    err = wifi_manager_wait_connected(WIFI_PROFILE_CONNECT_TIMEOUT_MS);
    if (err == ESP_OK) {
        worker->runtime->selected_index = (int)index;
        ESP_LOGI(TAG, "Connected profile %u (%s)", (unsigned)index, profile->ssid);
    } else {
        ESP_LOGW(TAG, "Profile %u (%s) timed out", (unsigned)index, profile->ssid);
    }
    return err;
}

static bool run_auto(struct wifi_profile_worker *worker, int skip_index)
{
    wifi_manager_status_t status = {0};
    wifi_manager_get_status(&status);
    if (status.sta_connected) return true;

    wifi_manager_scan_record_t *records = calloc(WIFI_PROFILE_SCAN_LIMIT, sizeof(*records));
    wifi_manager_profile_visible_t *visible = calloc(WIFI_PROFILE_SCAN_LIMIT, sizeof(*visible));
    if (!records || !visible) {
        free(records);
        free(visible);
        ESP_LOGE(TAG, "No memory for Wi-Fi profile scan");
        return false;
    }

    uint16_t count = 0;
    esp_err_t scan_err = wifi_manager_scan_aps(records, WIFI_PROFILE_SCAN_LIMIT, &count);
    if (scan_err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi scan returned %u AP(s)", (unsigned)count);
        for (uint16_t i = 0; i < count; ++i) {
            strlcpy(visible[i].ssid, records[i].ssid, sizeof(visible[i].ssid));
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi scan failed (%s); trying saved profiles directly",
                 esp_err_to_name(scan_err));
        count = 0;
        for (size_t i = 0; i < WIFI_PROFILES_MAX_COUNT; ++i) {
            if (worker->runtime->profiles.entries[i].ssid[0]) {
                strlcpy(visible[count++].ssid, worker->runtime->profiles.entries[i].ssid,
                        sizeof(visible[0].ssid));
            }
        }
    }

    for (size_t profile_index = 0; profile_index < WIFI_PROFILES_MAX_COUNT; ++profile_index) {
        const char *ssid = worker->runtime->profiles.entries[profile_index].ssid;
        if (!ssid[0]) continue;
        bool is_visible = false;
        for (uint16_t visible_index = 0; visible_index < count; ++visible_index) {
            if (strcmp(ssid, visible[visible_index].ssid) == 0) {
                is_visible = true;
                break;
            }
        }
        ESP_LOGI(TAG, "Saved profile %u (%s): %s", (unsigned)profile_index, ssid,
                 is_visible ? "visible" : "not visible");
    }

    wifi_manager_profile_attempt_t attempt;
    wifi_manager_profile_attempt_begin(&attempt);
    for (;;) {
        int index = wifi_manager_profile_attempt_next(&attempt, &worker->runtime->profiles,
                                                      visible, count);
        if (index < 0) break;
        if (index == skip_index) continue;
        ESP_LOGI(TAG, "Trying saved profile %u (%s)", (unsigned)index,
                 worker->runtime->profiles.entries[index].ssid);
        if (apply_profile(worker, (size_t)index) == ESP_OK) {
            free(records);
            free(visible);
            return true;
        }
    }
    ESP_LOGW(TAG, "No saved Wi-Fi profile connected in this cycle");
    free(records);
    free(visible);
    return false;
}

static void worker_task(void *arg)
{
    struct wifi_profile_worker *worker = arg;
    wifi_profile_request_t request;
    bool retry_pending = false;
    for (;;) {
        TickType_t wait = retry_pending
                              ? pdMS_TO_TICKS(WIFI_PROFILE_RETRY_INTERVAL_MS)
                              : portMAX_DELAY;
        if (xQueueReceive(worker->queue, &request, wait) != pdTRUE) {
            request = (wifi_profile_request_t){ .kind = WIFI_PROFILE_REQUEST_AUTO };
        }
        worker->busy = true;
        bool connected = false;
        if (request.kind == WIFI_PROFILE_REQUEST_MANUAL) {
            connected = apply_profile(worker, request.index) == ESP_OK;
            if (!connected) {
                ESP_LOGW(TAG, "Manual profile %u failed; trying remaining saved profiles",
                         (unsigned)request.index);
                connected = run_auto(worker, (int)request.index);
            }
        } else {
            connected = run_auto(worker, -1);
        }
        worker->busy = false;
        retry_pending = !connected;
    }
}

esp_err_t wifi_profile_worker_start(wifi_profile_runtime_t *runtime,
                                    const wifi_manager_config_t *base_config,
                                    wifi_profile_worker_handle_t *out_handle)
{
    if (!runtime || !base_config || !out_handle) return ESP_ERR_INVALID_ARG;
    struct wifi_profile_worker *worker = calloc(1, sizeof(*worker));
    if (!worker) return ESP_ERR_NO_MEM;
    worker->runtime = runtime;
    esp_err_t err = wifi_profile_worker_config_init(&worker->base_config, base_config);
    if (err != ESP_OK) {
        free(worker);
        return err;
    }
    worker->queue = xQueueCreate(4, sizeof(wifi_profile_request_t));
    if (!worker->queue) {
        free(worker);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(worker_task, "wifi_profiles", WIFI_PROFILE_WORKER_STACK,
                    worker, 4, &worker->task) != pdPASS) {
        vQueueDelete(worker->queue);
        free(worker);
        return ESP_ERR_NO_MEM;
    }
    *out_handle = worker;
    return wifi_profile_worker_notify(worker, false);
}

esp_err_t wifi_profile_worker_notify(wifi_profile_worker_handle_t handle, bool connected)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (connected) return ESP_OK;
    if (handle->busy) return ESP_OK;
    const wifi_profile_request_t request = { .kind = WIFI_PROFILE_REQUEST_AUTO };
    return xQueueSend(handle->queue, &request, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wifi_profile_worker_connect_now(wifi_profile_worker_handle_t handle, size_t index)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (index >= WIFI_PROFILES_MAX_COUNT) return ESP_ERR_INVALID_ARG;
    if (handle->runtime->profiles.entries[index].ssid[0] == '\0') return ESP_ERR_NOT_FOUND;
    const wifi_profile_request_t request = {
        .kind = WIFI_PROFILE_REQUEST_MANUAL,
        .index = index,
    };
    return xQueueSend(handle->queue, &request, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
