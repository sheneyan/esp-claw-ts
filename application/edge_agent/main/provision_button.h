/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_err.h"

typedef esp_err_t (*provision_button_factory_reset_cb_t)(void);

esp_err_t provision_button_start(provision_button_factory_reset_cb_t factory_reset_cb);
