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

void provision_button_arm_init(provision_button_arm_state_t *state)
{
    if (!state) return;
    state->released_samples = 0;
    state->armed = false;
}

bool provision_button_arm_update(provision_button_arm_state_t *state, bool pressed)
{
    if (!state) return false;
    if (state->armed) return true;
    if (pressed) {
        state->released_samples = 0;
        return false;
    }
    if (state->released_samples < PROVISION_BUTTON_ARM_RELEASE_SAMPLES) {
        state->released_samples++;
    }
    if (state->released_samples == PROVISION_BUTTON_ARM_RELEASE_SAMPLES) {
        state->armed = true;
    }
    return state->armed;
}

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
