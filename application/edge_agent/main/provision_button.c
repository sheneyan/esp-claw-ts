/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "provision_button.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "provision_button_policy.h"
#include "wifi_manager.h"

static const char *TAG = "provision_button";

enum {
    PROVISION_BUTTON_GPIO = GPIO_NUM_0,
    PROVISION_BUTTON_BOOT_GUARD_MS = 2000,
    PROVISION_BUTTON_POLL_MS = 50,
    PROVISION_BUTTON_TASK_STACK = 3072,
};

static TaskHandle_t s_button_task;
static provision_button_factory_reset_cb_t s_factory_reset_cb;

static void provision_button_task(void *arg)
{
    (void)arg;
    bool was_pressed = false;
    int64_t pressed_at_us = 0;
    provision_button_arm_state_t arm;
    provision_button_arm_init(&arm);

    vTaskDelay(pdMS_TO_TICKS(PROVISION_BUTTON_BOOT_GUARD_MS));

    for (;;) {
        const bool pressed = gpio_get_level(PROVISION_BUTTON_GPIO) == 0;
        const int64_t now_us = esp_timer_get_time();

        if (!arm.armed) {
            if (provision_button_arm_update(&arm, pressed)) {
                ESP_LOGI(TAG, "BOOT button armed after stable release");
            }
            was_pressed = false;
            vTaskDelay(pdMS_TO_TICKS(PROVISION_BUTTON_POLL_MS));
            continue;
        }

        if (pressed && !was_pressed) {
            pressed_at_us = now_us;
        } else if (!pressed && was_pressed) {
            uint32_t held_ms = (uint32_t)((now_us - pressed_at_us) / 1000);
            provision_button_action_t action = provision_button_classify_press(held_ms);

            if (action == PROVISION_BUTTON_ACTION_REOPEN_AP) {
                esp_err_t err = wifi_manager_set_provisioning_ap(true);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to enqueue provisioning AP reopen: %s",
                             esp_err_to_name(err));
                }
            } else if (action == PROVISION_BUTTON_ACTION_FACTORY_RESET) {
                ESP_LOGW(TAG, "Factory reset requested by BOOT button");
                esp_err_t err = s_factory_reset_cb();
                if (err == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    esp_restart();
                }
                ESP_LOGE(TAG, "Factory reset failed; device not restarted: %s",
                         esp_err_to_name(err));
            }
        }

        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(PROVISION_BUTTON_POLL_MS));
    }
}

esp_err_t provision_button_start(provision_button_factory_reset_cb_t factory_reset_cb)
{
    if (!factory_reset_cb) return ESP_ERR_INVALID_ARG;
    if (s_button_task) return ESP_ERR_INVALID_STATE;
    s_factory_reset_cb = factory_reset_cb;

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << PROVISION_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) return err;

    BaseType_t created = xTaskCreate(provision_button_task, "provision_btn",
                                     PROVISION_BUTTON_TASK_STACK,
                                     NULL, 4, &s_button_task);
    if (created != pdPASS) {
        s_button_task = NULL;
        s_factory_reset_cb = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
