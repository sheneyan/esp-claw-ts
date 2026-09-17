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

#define CAP_TAILSCALE_TOOL_STATUS          "tailscale_status"
#define CAP_TAILSCALE_TOOL_LIST_EXIT_NODES "tailscale_list_exit_nodes"
#define CAP_TAILSCALE_TOOL_SET_EXIT_NODE   "tailscale_set_exit_node"
#define CAP_TAILSCALE_TOOL_CLEAR_EXIT_NODE "tailscale_clear_exit_node"
#define CAP_TAILSCALE_TOOL_RECONNECT       "tailscale_reconnect"

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

/** Normalize a selector and reject IPv4-like values outside canonical CGNAT. */
esp_err_t cap_tailscale_validate_selector(const char *selector,
                                          char *out_selector,
                                          size_t out_selector_size);

/** Clamp returned provider counts to the public array capacities. */
size_t cap_tailscale_bound_exit_node_count(size_t count);
size_t cap_tailscale_bound_derp_rtt_count(size_t count);

/** Model-facing descriptor ids in registration order. */
size_t cap_tailscale_descriptor_count(void);
const char *cap_tailscale_descriptor_id(size_t index);

/**
 * Render only the safe, model-facing status projection as JSON.  These
 * helpers are intentionally cJSON-free so host tests can exercise the
 * redaction and bounded-buffer boundary without ESP-IDF.
 */
esp_err_t cap_tailscale_render_status_json(const cap_tailscale_status_t *status,
                                           char *output,
                                           size_t output_size);
esp_err_t cap_tailscale_render_exit_nodes_json(const cap_tailscale_exit_node_t *nodes,
                                               size_t count,
                                               char *output,
                                               size_t output_size);
esp_err_t cap_tailscale_render_mutation_json(const cap_tailscale_mutation_result_t *result,
                                             char *output,
                                             size_t output_size);
esp_err_t cap_tailscale_render_error_json(const char *error,
                                          const char *message,
                                          char *output,
                                          size_t output_size);

#ifdef CAP_TAILSCALE_HOST_TEST
void cap_tailscale_test_reset(void);
void cap_tailscale_test_fail_allocations_after(int successful_allocations);
#endif

#ifdef __cplusplus
}
#endif
