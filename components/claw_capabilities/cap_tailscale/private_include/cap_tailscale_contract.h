/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "cap_tailscale.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Return ESP_OK only when a read tool received no unknown fields. */
esp_err_t cap_tailscale_validate_read_args(size_t unexpected_field_count);

/**
 * Validate an already-parsed confirmation field. A missing or wrongly typed
 * field is invalid input; an explicit false confirmation is invalid state.
 */
esp_err_t cap_tailscale_validate_mutation_confirmation(bool user_confirmed_present,
                                                        bool user_confirmed_is_bool,
                                                        bool user_confirmed,
                                                        size_t unexpected_field_count);

/**
 * Convenience for parsers that have already established the confirmation
 * field's boolean type.
 */
esp_err_t cap_tailscale_validate_mutation_args(bool user_confirmed_present,
                                               bool user_confirmed,
                                               size_t unexpected_field_count);

/** Trim ASCII whitespace from selector into caller-owned storage. */
esp_err_t cap_tailscale_normalize_selector(const char *selector,
                                           char *out_selector,
                                           size_t out_selector_size);

/** Return true only for a syntactically valid IPv4 address in 100.64.0.0/10. */
bool cap_tailscale_selector_is_cgnat(const char *selector);

/** Clamp returned provider counts to the public array capacities. */
size_t cap_tailscale_bound_exit_node_count(size_t count);
size_t cap_tailscale_bound_derp_rtt_count(size_t count);

#ifdef __cplusplus
}
#endif
