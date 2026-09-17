/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "provision_button_policy.h"

enum {
    PROVISION_BUTTON_REOPEN_MIN_MS = 3000,
    PROVISION_BUTTON_FACTORY_RESET_MIN_MS = 10000,
};

provision_button_action_t provision_button_classify_press(uint32_t held_ms)
{
    if (held_ms >= PROVISION_BUTTON_FACTORY_RESET_MIN_MS) {
        return PROVISION_BUTTON_ACTION_FACTORY_RESET;
    }
    if (held_ms >= PROVISION_BUTTON_REOPEN_MIN_MS) {
        return PROVISION_BUTTON_ACTION_REOPEN_AP;
    }
    return PROVISION_BUTTON_ACTION_NONE;
}
