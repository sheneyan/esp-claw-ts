/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cap_tailscale_contract.h"

#include <ctype.h>
#include <string.h>

esp_err_t cap_tailscale_validate_read_args(size_t unexpected_field_count)
{
    return unexpected_field_count == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t cap_tailscale_validate_mutation_confirmation(bool user_confirmed_present,
                                                        bool user_confirmed_is_bool,
                                                        bool user_confirmed,
                                                        size_t unexpected_field_count)
{
    if (unexpected_field_count != 0 || !user_confirmed_present || !user_confirmed_is_bool) {
        return ESP_ERR_INVALID_ARG;
    }

    return user_confirmed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cap_tailscale_validate_mutation_args(bool user_confirmed_present,
                                               bool user_confirmed,
                                               size_t unexpected_field_count)
{
    return cap_tailscale_validate_mutation_confirmation(user_confirmed_present,
                                                        true,
                                                        user_confirmed,
                                                        unexpected_field_count);
}

esp_err_t cap_tailscale_normalize_selector(const char *selector,
                                           char *out_selector,
                                           size_t out_selector_size)
{
    const char *start;
    const char *end;
    size_t length;

    if (selector == NULL || out_selector == NULL || out_selector_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    start = selector;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }

    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        --end;
    }

    length = (size_t)(end - start);
    if (length == 0 || length >= CAP_TAILSCALE_HOSTNAME_LEN || length >= out_selector_size) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(out_selector, start, length);
    out_selector[length] = '\0';
    return ESP_OK;
}

static bool parse_ipv4_octet(const char **cursor, unsigned int *out_value)
{
    const char *value_start = *cursor;
    unsigned int value = 0;

    while (**cursor >= '0' && **cursor <= '9') {
        value = value * 10U + (unsigned int)(**cursor - '0');
        if (value > 255U) {
            return false;
        }
        ++*cursor;
    }

    if (*cursor == value_start) {
        return false;
    }

    *out_value = value;
    return true;
}

bool cap_tailscale_selector_is_cgnat(const char *selector)
{
    const char *cursor;
    unsigned int octets[4];
    size_t index;

    if (selector == NULL) {
        return false;
    }

    cursor = selector;
    for (index = 0; index < 4; ++index) {
        if (!parse_ipv4_octet(&cursor, &octets[index])) {
            return false;
        }
        if (index < 3) {
            if (*cursor != '.') {
                return false;
            }
            ++cursor;
        }
    }

    if (*cursor != '\0') {
        return false;
    }

    return octets[0] == 100U && octets[1] >= 64U && octets[1] <= 127U;
}

size_t cap_tailscale_bound_exit_node_count(size_t count)
{
    return count > CAP_TAILSCALE_MAX_EXIT_NODES ? CAP_TAILSCALE_MAX_EXIT_NODES : count;
}

size_t cap_tailscale_bound_derp_rtt_count(size_t count)
{
    return count > CAP_TAILSCALE_MAX_DERP_RTTS ? CAP_TAILSCALE_MAX_DERP_RTTS : count;
}
