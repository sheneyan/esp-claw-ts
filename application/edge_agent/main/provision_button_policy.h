/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

typedef enum {
    PROVISION_BUTTON_ACTION_NONE = 0,
    PROVISION_BUTTON_ACTION_REOPEN_AP,
    PROVISION_BUTTON_ACTION_FACTORY_RESET,
} provision_button_action_t;

provision_button_action_t provision_button_classify_press(uint32_t held_ms);
