/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    PROVISION_BUTTON_ARM_RELEASE_SAMPLES = 4,
};

typedef struct {
    uint8_t released_samples;
    bool armed;
} provision_button_arm_state_t;

typedef enum {
    PROVISION_BUTTON_ACTION_NONE = 0,
    PROVISION_BUTTON_ACTION_REOPEN_AP,
    PROVISION_BUTTON_ACTION_FACTORY_RESET,
} provision_button_action_t;

void provision_button_arm_init(provision_button_arm_state_t *state);
bool provision_button_arm_update(provision_button_arm_state_t *state, bool pressed);
provision_button_action_t provision_button_classify_press(uint32_t held_ms);
