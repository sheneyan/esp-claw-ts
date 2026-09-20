/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "wifi_manager.h"
#include "wifi_profile_runtime.h"

typedef struct wifi_profile_worker *wifi_profile_worker_handle_t;

esp_err_t wifi_profile_worker_start(wifi_profile_runtime_t *runtime,
                                    const wifi_manager_config_t *base_config,
                                    wifi_profile_worker_handle_t *out_handle);
esp_err_t wifi_profile_worker_notify(wifi_profile_worker_handle_t handle, bool connected);
esp_err_t wifi_profile_worker_connect_now(wifi_profile_worker_handle_t handle, size_t index);
