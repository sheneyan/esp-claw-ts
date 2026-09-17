/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "app_config_tailscale_validation.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool fail_with(char *message, size_t message_size, const char *text)
{
    if (message && message_size > 0) {
        snprintf(message, message_size, "%s", text);
    }
    return false;
}

static bool parse_decimal_range(const char *value, unsigned minimum, unsigned maximum)
{
    unsigned parsed = 0;

    if (!value || !value[0]) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor; cursor++) {
        if (!isdigit(*cursor)) {
            return false;
        }
        parsed = parsed * 10U + (unsigned)(*cursor - '0');
        if (parsed > maximum) {
            return false;
        }
    }
    return parsed >= minimum;
}

static bool parse_ipv4(const char *value, uint32_t *address)
{
    uint32_t result = 0;
    const char *cursor = value;

    if (!value || !value[0] || !address) {
        return false;
    }

    for (unsigned octet_index = 0; octet_index < 4; octet_index++) {
        unsigned octet = 0;
        unsigned digits = 0;

        while (*cursor && *cursor != '.') {
            if (!isdigit((unsigned char)*cursor) || digits == 3) {
                return false;
            }
            octet = octet * 10U + (unsigned)(*cursor - '0');
            if (octet > 255U) {
                return false;
            }
            digits++;
            cursor++;
        }
        if (digits == 0) {
            return false;
        }
        result = (result << 8) | octet;

        if (octet_index < 3) {
            if (*cursor != '.') {
                return false;
            }
            cursor++;
        } else if (*cursor != '\0') {
            return false;
        }
    }

    *address = result;
    return true;
}

static bool is_cgnat_ipv4(const char *value)
{
    uint32_t address = 0;
    const uint32_t cgnat_first = UINT32_C(0x64400000);
    const uint32_t cgnat_last = UINT32_C(0x647fffff);

    return parse_ipv4(value, &address) && address >= cgnat_first && address <= cgnat_last;
}

static bool is_valid_login_server(const char *value)
{
    const char *authority = value;
    const char *colon = NULL;

    if (!value || !value[0]) {
        return true;
    }
    if (strncmp(value, "http://", 7) == 0) {
        authority = value + 7;
    } else if (strncmp(value, "https://", 8) == 0) {
        authority = value + 8;
    } else if (strstr(value, "://")) {
        return false;
    }
    if (!authority[0]) {
        return false;
    }

    for (const unsigned char *cursor = (const unsigned char *)authority; *cursor; cursor++) {
        if (isspace(*cursor) || *cursor == '/' || *cursor == '?' || *cursor == '#') {
            return false;
        }
        if (*cursor == ':') {
            if (colon) {
                return false;
            }
            colon = (const char *)cursor;
            continue;
        }
        if (!isalnum(*cursor) && *cursor != '.' && *cursor != '-') {
            return false;
        }
    }

    size_t host_length = colon ? (size_t)(colon - authority) : strlen(authority);
    if (host_length == 0 || authority[0] == '.' || authority[0] == '-' ||
            authority[host_length - 1] == '.' || authority[host_length - 1] == '-') {
        return false;
    }
    if (colon && !parse_decimal_range(colon + 1, 1, 65535)) {
        return false;
    }
    return true;
}

bool app_config_tailscale_validate(const app_config_tailscale_view_t *cfg,
                                   char *message,
                                   size_t message_size)
{
    bool enabled = false;

    if (message && message_size > 0) {
        message[0] = '\0';
    }
    if (!cfg) {
        return fail_with(message, message_size, "Missing Tailscale configuration");
    }

    if (cfg->enabled && (strcmp(cfg->enabled, "false") == 0 || strcmp(cfg->enabled, "0") == 0)) {
        enabled = false;
    } else if (cfg->enabled && (strcmp(cfg->enabled, "true") == 0 || strcmp(cfg->enabled, "1") == 0)) {
        enabled = true;
    } else {
        return fail_with(message, message_size, "tailscale_enabled must be true, false, 1, or 0");
    }

    if (!enabled) {
        return true;
    }
    if (!cfg->hostname || strlen(cfg->hostname) < 1 || strlen(cfg->hostname) > 63) {
        return fail_with(message, message_size, "tailscale_hostname must be 1-63 characters when enabled");
    }
    if (!parse_decimal_range(cfg->max_peers, 1, 64)) {
        return fail_with(message, message_size, "tailscale_max_peers must be an integer from 1 to 64");
    }
    if (cfg->exit_node && cfg->exit_node[0] && !is_cgnat_ipv4(cfg->exit_node)) {
        return fail_with(message, message_size, "tailscale_exit_node must be empty or a CGNAT IPv4 address");
    }
    if (!is_valid_login_server(cfg->login_server)) {
        return fail_with(message, message_size, "tailscale_login_server must be empty, host, host:port, or an HTTP(S) host");
    }

    return true;
}
