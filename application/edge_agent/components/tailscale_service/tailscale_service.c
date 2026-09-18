/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tailscale_service.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAILSCALE_SERVICE_MAX_PEERS 16u
#define TAILSCALE_SERVICE_CGNAT_MIN UINT32_C(0x64400000)
#define TAILSCALE_SERVICE_CGNAT_MAX UINT32_C(0x647fffff)

typedef struct {
    ts_claw_diagnostics_t diagnostics;
    ts_claw_peer_t peers[TAILSCALE_SERVICE_MAX_PEERS];
    size_t peer_count;
    char old_persisted[16];
    uint32_t old_ip;
    char selector[96];
    ts_claw_runtime_result_t runtime;
} tailscale_service_workspace_t;

struct tailscale_service {
    tailscale_service_ops_t ops;
    SemaphoreHandle_t mutation_mutex;
    tailscale_service_workspace_t *workspace;
};

typedef enum {
    SELECTOR_RESOLVED,
    SELECTOR_AMBIGUOUS,
    SELECTOR_NOT_FOUND,
} selector_resolution_t;

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (destination == NULL || capacity == 0u) {
        return;
    }
    if (source == NULL) {
        source = "";
    }
    (void)snprintf(destination, capacity, "%s", source);
}

static void result_init(tailscale_service_result_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->error = TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED;
    out->exit_state = TS_EXIT_DISABLED;
    copy_text(out->egress, sizeof(out->egress), "unknown");
}

static void result_error(tailscale_service_result_t *out,
                         tailscale_service_error_t error,
                         const char *message)
{
    out->ok = false;
    out->error = error;
    copy_text(out->message, sizeof(out->message), message);
}

static void format_ip(uint32_t ip, char out[16])
{
    if (ip == 0u) {
        out[0] = '\0';
        return;
    }
    (void)snprintf(out, 16, "%u.%u.%u.%u",
                   (unsigned)((ip >> 24) & 0xffu),
                   (unsigned)((ip >> 16) & 0xffu),
                   (unsigned)((ip >> 8) & 0xffu),
                   (unsigned)(ip & 0xffu));
}

static bool parse_canonical_ipv4(const char *text, uint32_t *out)
{
    uint32_t value = 0u;
    const char *cursor = text;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }
    for (unsigned part = 0u; part < 4u; ++part) {
        unsigned octet = 0u;
        unsigned digits = 0u;
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        if (*cursor == '0' && cursor[1] >= '0' && cursor[1] <= '9') {
            return false;
        }
        while (*cursor >= '0' && *cursor <= '9') {
            octet = octet * 10u + (unsigned)(*cursor - '0');
            if (++digits > 3u || octet > 255u) {
                return false;
            }
            ++cursor;
        }
        value = (value << 8) | octet;
        if (part < 3u) {
            if (*cursor != '.') {
                return false;
            }
            ++cursor;
        } else if (*cursor != '\0') {
            return false;
        }
    }
    *out = value;
    return true;
}

static bool is_cgnat_ip(uint32_t ip)
{
    return ip >= TAILSCALE_SERVICE_CGNAT_MIN && ip <= TAILSCALE_SERVICE_CGNAT_MAX;
}

static void sanitize_diagnostics(ts_claw_diagnostics_t *diagnostics)
{
    diagnostics->status.egress[sizeof(diagnostics->status.egress) - 1u] = '\0';
    diagnostics->status.dns_egress[sizeof(diagnostics->status.dns_egress) - 1u] = '\0';
    diagnostics->status.last_error[sizeof(diagnostics->status.last_error) - 1u] = '\0';
    diagnostics->hostname[sizeof(diagnostics->hostname) - 1u] = '\0';
    diagnostics->derp_active_name[sizeof(diagnostics->derp_active_name) - 1u] = '\0';
    diagnostics->derp_default_name[sizeof(diagnostics->derp_default_name) - 1u] = '\0';
    if (diagnostics->derp_rtt_count > TS_CLAW_MAX_DERP_RTTS) {
        diagnostics->derp_rtt_count = TS_CLAW_MAX_DERP_RTTS;
    }
    for (size_t index = 0u; index < diagnostics->derp_rtt_count; ++index) {
        diagnostics->derp_rtts[index].region_name[
            sizeof(diagnostics->derp_rtts[index].region_name) - 1u] = '\0';
    }
}

static void sanitize_runtime_result(ts_claw_runtime_result_t *runtime)
{
    runtime->egress[sizeof(runtime->egress) - 1u] = '\0';
}

static bool normalize_selector(const char *selector, char out[96],
                               bool *is_ip, uint32_t *ip)
{
    const char *begin;
    const char *end;
    size_t length;
    bool numeric_like = true;

    if (selector == NULL || out == NULL || is_ip == NULL || ip == NULL) {
        return false;
    }
    begin = selector;
    while (*begin != '\0' && isspace((unsigned char)*begin)) {
        ++begin;
    }
    end = begin + strlen(begin);
    while (end > begin && isspace((unsigned char)end[-1])) {
        --end;
    }
    length = (size_t)(end - begin);
    if (length == 0u || length >= 96u) {
        return false;
    }
    for (size_t index = 0u; index < length; ++index) {
        unsigned char ch = (unsigned char)begin[index];
        if (!(isalnum(ch) || ch == '-' || ch == '.')) {
            return false;
        }
        if (!(isdigit(ch) || ch == '.')) {
            numeric_like = false;
        }
    }
    if (begin[0] == '.' || begin[0] == '-' || end[-1] == '.' || end[-1] == '-') {
        return false;
    }
    memcpy(out, begin, length);
    out[length] = '\0';
    if (strstr(out, "..") != NULL) {
        return false;
    }
    *is_ip = false;
    *ip = 0u;
    if (numeric_like) {
        if (!parse_canonical_ipv4(out, ip) || !is_cgnat_ip(*ip)) {
            return false;
        }
        *is_ip = true;
    }
    return true;
}

static size_t hostname_short_length(const char *hostname)
{
    const char *dot = strchr(hostname, '.');
    return dot == NULL ? strlen(hostname) : (size_t)(dot - hostname);
}

static void append_candidate(tailscale_service_result_t *out,
                             const ts_claw_peer_t *peer,
                             bool first)
{
    char ip[16];
    size_t used = strlen(out->message);
    size_t short_length = hostname_short_length(peer->hostname);
    format_ip(peer->vpn_ip, ip);
    if (used < sizeof(out->message)) {
        (void)snprintf(out->message + used, sizeof(out->message) - used,
                       "%s%.*s (%s)", first ? "" : ", ",
                       (int)short_length, peer->hostname, ip);
    }
}

static selector_resolution_t resolve_selector(const char *normalized,
                                              bool selector_is_ip,
                                              uint32_t selector_ip,
                                              const ts_claw_peer_t *peers,
                                              size_t peer_count,
                                              size_t *selected_index,
                                              tailscale_service_result_t *out)
{
    size_t match_count = 0u;
    size_t first_match = 0u;

    if (selector_is_ip) {
        for (size_t index = 0u; index < peer_count; ++index) {
            if (peers[index].vpn_ip == selector_ip) {
                *selected_index = index;
                return SELECTOR_RESOLVED;
            }
        }
        return SELECTOR_NOT_FOUND;
    }
    for (size_t index = 0u; index < peer_count; ++index) {
        if (strcasecmp(peers[index].hostname, normalized) == 0) {
            *selected_index = index;
            return SELECTOR_RESOLVED;
        }
    }
    if (strchr(normalized, '.') != NULL) {
        return SELECTOR_NOT_FOUND;
    }
    size_t selector_length = strlen(normalized);
    for (size_t index = 0u; index < peer_count; ++index) {
        size_t short_length = hostname_short_length(peers[index].hostname);
        if (selector_length <= short_length &&
            strncasecmp(peers[index].hostname, normalized, selector_length) == 0) {
            if (match_count == 0u) {
                first_match = index;
            }
            ++match_count;
        }
    }
    if (match_count == 0u) {
        return SELECTOR_NOT_FOUND;
    }
    if (match_count == 1u) {
        *selected_index = first_match;
        return SELECTOR_RESOLVED;
    }
    copy_text(out->message, sizeof(out->message), "Ambiguous node: ");
    size_t candidate_count = 0u;
    for (size_t index = 0u; index < peer_count; ++index) {
        size_t short_length = hostname_short_length(peers[index].hostname);
        if (selector_length <= short_length &&
            strncasecmp(peers[index].hostname, normalized, selector_length) == 0) {
            append_candidate(out, &peers[index], candidate_count == 0u);
            ++candidate_count;
        }
    }
    return SELECTOR_AMBIGUOUS;
}

static void result_from_diagnostics(tailscale_service_result_t *out,
                                    const ts_claw_diagnostics_t *diagnostics)
{
    out->exit_state = diagnostics->status.exit_state;
    copy_text(out->egress, sizeof(out->egress), diagnostics->status.egress);
    format_ip(diagnostics->status.exit_node_ip, out->selected_ip);
}

static void result_from_runtime(tailscale_service_result_t *out,
                                const ts_claw_runtime_result_t *runtime,
                                const ts_claw_peer_t *peers,
                                size_t peer_count)
{
    out->exit_state = runtime->exit_state;
    copy_text(out->egress, sizeof(out->egress), runtime->egress);
    format_ip(runtime->selected_exit_node_ip, out->selected_ip);
    out->selected_hostname[0] = '\0';
    for (size_t index = 0u; index < peer_count; ++index) {
        if (peers[index].vpn_ip == runtime->selected_exit_node_ip) {
            copy_text(out->selected_hostname, sizeof(out->selected_hostname),
                      peers[index].hostname);
            break;
        }
    }
}

static bool runtime_result_is_safe(const ts_claw_runtime_result_t *runtime,
                                   uint32_t requested_ip)
{
    if (runtime->rollback_attempted || runtime->rollback_recovered ||
        runtime->selected_exit_node_ip != requested_ip) {
        return false;
    }
    if (requested_ip == 0u) {
        return runtime->exit_state == TS_EXIT_DISABLED &&
               strcmp(runtime->egress, "sta") == 0;
    }
    return runtime->exit_state == TS_EXIT_ACTIVE &&
           strcmp(runtime->egress, "exit") == 0;
}

static bool rollback_result_is_safe(const ts_claw_runtime_result_t *runtime,
                                    uint32_t requested_ip)
{
    return runtime->selected_exit_node_ip == requested_ip &&
           (requested_ip == 0u
                ? runtime->exit_state == TS_EXIT_DISABLED && strcmp(runtime->egress, "sta") == 0
                : runtime->exit_state == TS_EXIT_ACTIVE && strcmp(runtime->egress, "exit") == 0) &&
           !runtime->rollback_attempted;
}

static bool mutation_begin(tailscale_service_handle_t service,
                           tailscale_service_result_t *out)
{
    if (xSemaphoreTake(service->mutation_mutex, 0) == pdTRUE) {
        return true;
    }
    result_error(out, TAILSCALE_SERVICE_BUSY,
                 "Another Tailscale mutation is already in progress.");
    return false;
}

static esp_err_t require_runtime(tailscale_service_handle_t service,
                                 bool require_connected,
                                 tailscale_service_result_t *out)
{
    tailscale_service_workspace_t *workspace = service->workspace;
    memset(&workspace->diagnostics, 0, sizeof(workspace->diagnostics));
    esp_err_t err = service->ops.get_diagnostics(&workspace->diagnostics,
                                                 service->ops.ctx);
    if (err != ESP_OK) {
        memset(&workspace->diagnostics, 0, sizeof(workspace->diagnostics));
        result_error(out, TAILSCALE_SERVICE_NOT_CONNECTED,
                     "Tailscale runtime diagnostics are unavailable.");
        return err;
    }
    sanitize_diagnostics(&workspace->diagnostics);
    result_from_diagnostics(out, &workspace->diagnostics);
    if (!workspace->diagnostics.status.enabled) {
        result_error(out, TAILSCALE_SERVICE_NOT_ENABLED,
                     "Tailscale is not enabled.");
        return ESP_ERR_INVALID_STATE;
    }
    if (require_connected && !workspace->diagnostics.status.connected) {
        result_error(out, TAILSCALE_SERVICE_NOT_CONNECTED,
                     "Tailscale is not connected.");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static bool load_rollback_target(tailscale_service_handle_t service,
                                 tailscale_service_result_t *out)
{
    tailscale_service_workspace_t *workspace = service->workspace;
    memset(workspace->old_persisted, 0, sizeof(workspace->old_persisted));
    esp_err_t err = service->ops.load_persisted_exit(workspace->old_persisted,
                                                     service->ops.ctx);
    workspace->old_persisted[sizeof(workspace->old_persisted) - 1] = '\0';
    if (err != ESP_OK) {
        result_error(out, TAILSCALE_SERVICE_PERSISTENCE_FAILED,
                     "Unable to load the persisted Exit Node selection.");
        return false;
    }
    if (workspace->old_persisted[0] == '\0') {
        workspace->old_ip = 0u;
        return true;
    }
    if (!parse_canonical_ipv4(workspace->old_persisted, &workspace->old_ip) ||
        !is_cgnat_ip(workspace->old_ip)) {
        result_error(out, TAILSCALE_SERVICE_PERSISTENCE_FAILED,
                     "Persisted Exit Node selection is invalid; runtime was not changed.");
        return false;
    }
    return true;
}

static void persistence_rollback(tailscale_service_handle_t service,
                                 tailscale_service_result_t *out,
                                 bool persistence_unverified)
{
    tailscale_service_workspace_t *workspace = service->workspace;
    memset(&workspace->runtime, 0, sizeof(workspace->runtime));
    out->persisted = false;
    out->rollback_attempted = true;
    esp_err_t rollback_err = service->ops.apply_exit_node(workspace->old_ip,
                                                          &workspace->runtime,
                                                          service->ops.ctx);
    sanitize_runtime_result(&workspace->runtime);
    result_from_runtime(out, &workspace->runtime,
                        workspace->peers, workspace->peer_count);
    out->rollback_attempted = true;
    out->rollback_recovered = rollback_err == ESP_OK &&
                              rollback_result_is_safe(&workspace->runtime,
                                                      workspace->old_ip);
    if (out->rollback_recovered) {
        if (persistence_unverified) {
            result_error(out, TAILSCALE_SERVICE_PERSISTENCE_FAILED,
                         "Persistence unverified; runtime restored, but reboot may use a different Exit Node.");
        } else {
            result_error(out, TAILSCALE_SERVICE_PERSISTENCE_FAILED,
                         "Persistence failed; the previous runtime selection was restored.");
        }
    } else {
        result_error(out, TAILSCALE_SERVICE_ROLLBACK_FAILED,
                     persistence_unverified
                         ? "Persistence unverified and runtime rollback did not recover."
                         : "Persistence failed and runtime rollback did not recover.");
    }
}

esp_err_t tailscale_service_create(const tailscale_service_ops_t *ops,
                                   tailscale_service_handle_t *out)
{
    tailscale_service_handle_t service;
    if (out != NULL) {
        *out = NULL;
    }
    if (ops == NULL || out == NULL || ops->get_diagnostics == NULL ||
        ops->list_exit_nodes == NULL || ops->apply_exit_node == NULL ||
        ops->rebind == NULL || ops->load_persisted_exit == NULL ||
        ops->save_persisted_exit == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    service = calloc(1, sizeof(*service));
    if (service == NULL) {
        return ESP_ERR_NO_MEM;
    }
    service->workspace = calloc(1, sizeof(*service->workspace));
    if (service->workspace == NULL) {
        free(service);
        return ESP_ERR_NO_MEM;
    }
    service->mutation_mutex = xSemaphoreCreateMutex();
    if (service->mutation_mutex == NULL) {
        free(service->workspace);
        free(service);
        return ESP_ERR_NO_MEM;
    }
    service->ops = *ops;
    *out = service;
    return ESP_OK;
}

void tailscale_service_delete(tailscale_service_handle_t service)
{
    if (service == NULL) {
        return;
    }
    vSemaphoreDelete(service->mutation_mutex);
    free(service->workspace);
    free(service);
}

esp_err_t tailscale_service_get_diagnostics(tailscale_service_handle_t service,
                                            ts_claw_diagnostics_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (service == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = service->ops.get_diagnostics(out, service->ops.ctx);
    if (err != ESP_OK) {
        memset(out, 0, sizeof(*out));
    } else {
        sanitize_diagnostics(out);
    }
    return err;
}

int tailscale_service_list_exit_nodes(tailscale_service_handle_t service,
                                      ts_claw_peer_t *out,
                                      size_t capacity)
{
    ts_claw_peer_t *snapshot;
    size_t bounded_capacity = capacity < TAILSCALE_SERVICE_MAX_PEERS
                                  ? capacity
                                  : TAILSCALE_SERVICE_MAX_PEERS;
    if (out != NULL && bounded_capacity > 0u) {
        memset(out, 0, bounded_capacity * sizeof(*out));
    }
    if (service == NULL || (capacity > 0u && out == NULL)) {
        return -ESP_ERR_INVALID_ARG;
    }
    snapshot = calloc(TAILSCALE_SERVICE_MAX_PEERS, sizeof(*snapshot));
    if (snapshot == NULL) {
        return -ESP_ERR_NO_MEM;
    }
    int count = service->ops.list_exit_nodes(snapshot, TAILSCALE_SERVICE_MAX_PEERS,
                                             service->ops.ctx);
    if (count < 0 || count > (int)TAILSCALE_SERVICE_MAX_PEERS) {
        free(snapshot);
        return count < 0 ? count : -ESP_ERR_INVALID_SIZE;
    }
    size_t copied = (size_t)count < bounded_capacity ? (size_t)count
                                                     : bounded_capacity;
    if (copied > 0u) {
        memcpy(out, snapshot, copied * sizeof(*out));
        for (size_t index = 0u; index < copied; ++index) {
            out[index].hostname[sizeof(out[index].hostname) - 1] = '\0';
            out[index].derp_region_name[sizeof(out[index].derp_region_name) - 1] = '\0';
        }
    }
    free(snapshot);
    return (int)copied;
}

esp_err_t tailscale_service_set_exit_node(tailscale_service_handle_t service,
                                          const char *selector,
                                          tailscale_service_result_t *out)
{
    bool selector_is_ip;
    uint32_t selector_ip;
    size_t selected_index = 0u;

    result_init(out);
    if (service == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mutation_begin(service, out)) {
        return ESP_OK;
    }
    tailscale_service_workspace_t *workspace = service->workspace;
    if (!normalize_selector(selector, workspace->selector,
                            &selector_is_ip, &selector_ip)) {
        result_error(out, TAILSCALE_SERVICE_INVALID_NODE,
                     "Node must be a canonical CGNAT IP or a valid hostname selector.");
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    if (require_runtime(service, true, out) != ESP_OK) {
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    memset(workspace->peers, 0, sizeof(workspace->peers));
    int count = service->ops.list_exit_nodes(workspace->peers,
                                             TAILSCALE_SERVICE_MAX_PEERS,
                                             service->ops.ctx);
    if (count < 0 || count > (int)TAILSCALE_SERVICE_MAX_PEERS) {
        result_error(out, TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED,
                     "Exit Node list is unavailable.");
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    workspace->peer_count = (size_t)count;
    for (size_t index = 0u; index < workspace->peer_count; ++index) {
        workspace->peers[index].hostname[sizeof(workspace->peers[index].hostname) - 1] = '\0';
        workspace->peers[index].derp_region_name[
            sizeof(workspace->peers[index].derp_region_name) - 1] = '\0';
    }
    selector_resolution_t resolution = resolve_selector(
        workspace->selector, selector_is_ip, selector_ip,
        workspace->peers, workspace->peer_count, &selected_index, out);
    if (resolution != SELECTOR_RESOLVED) {
        if (resolution == SELECTOR_AMBIGUOUS) {
            out->ok = false;
            out->error = TAILSCALE_SERVICE_AMBIGUOUS_NODE;
        } else {
            result_error(out, TAILSCALE_SERVICE_NODE_NOT_FOUND,
                         "No matching Tailscale node was found.");
        }
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    const ts_claw_peer_t *selected = &workspace->peers[selected_index];
    format_ip(selected->vpn_ip, out->selected_ip);
    copy_text(out->selected_hostname, sizeof(out->selected_hostname), selected->hostname);
    if (!selected->online) {
        result_error(out, TAILSCALE_SERVICE_NODE_OFFLINE,
                     "The selected Tailscale node is offline.");
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    if (!selected->is_exit_node) {
        result_error(out, TAILSCALE_SERVICE_NOT_EXIT_NODE,
                     "The selected node does not advertise Exit Node capability.");
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    if (!load_rollback_target(service, out)) {
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    memset(&workspace->runtime, 0, sizeof(workspace->runtime));
    esp_err_t apply_err = service->ops.apply_exit_node(selected->vpn_ip,
                                                       &workspace->runtime,
                                                       service->ops.ctx);
    sanitize_runtime_result(&workspace->runtime);
    result_from_runtime(out, &workspace->runtime,
                        workspace->peers, workspace->peer_count);
    out->rollback_attempted = workspace->runtime.rollback_attempted;
    out->rollback_recovered = workspace->runtime.rollback_recovered;
    if (apply_err != ESP_OK) {
        result_error(out,
                     apply_err == ESP_ERR_TIMEOUT ? TAILSCALE_SERVICE_SWITCH_TIMEOUT
                                                  : TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED,
                     apply_err == ESP_ERR_TIMEOUT ? "Exit Node switch timed out."
                                                  : "Exit Node runtime apply failed.");
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    if (!runtime_result_is_safe(&workspace->runtime, selected->vpn_ip)) {
        result_error(out, TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED,
                     "Runtime did not confirm the requested active Exit Node.");
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    char selected_ip[16];
    format_ip(selected->vpn_ip, selected_ip);
    esp_err_t save_err = service->ops.save_persisted_exit(selected_ip,
                                                          service->ops.ctx);
    if (save_err != ESP_OK) {
        persistence_rollback(service, out,
                             save_err == ESP_ERR_INVALID_RESPONSE);
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    out->ok = true;
    out->error = TAILSCALE_SERVICE_OK;
    out->persisted = true;
    copy_text(out->message, sizeof(out->message), "Exit Node is active and persisted.");
    xSemaphoreGive(service->mutation_mutex);
    return ESP_OK;
}

esp_err_t tailscale_service_clear_exit_node(tailscale_service_handle_t service,
                                            tailscale_service_result_t *out)
{
    result_init(out);
    if (service == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mutation_begin(service, out)) {
        return ESP_OK;
    }
    tailscale_service_workspace_t *workspace = service->workspace;
    workspace->peer_count = 0u;
    if (require_runtime(service, true, out) != ESP_OK) {
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    if (!load_rollback_target(service, out)) {
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    memset(&workspace->runtime, 0, sizeof(workspace->runtime));
    esp_err_t apply_err = service->ops.apply_exit_node(0u, &workspace->runtime,
                                                       service->ops.ctx);
    sanitize_runtime_result(&workspace->runtime);
    result_from_runtime(out, &workspace->runtime, NULL, 0u);
    out->rollback_attempted = workspace->runtime.rollback_attempted;
    out->rollback_recovered = workspace->runtime.rollback_recovered;
    if (apply_err != ESP_OK) {
        result_error(out,
                     apply_err == ESP_ERR_TIMEOUT ? TAILSCALE_SERVICE_SWITCH_TIMEOUT
                                                  : TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED,
                     apply_err == ESP_ERR_TIMEOUT ? "Exit Node clear timed out."
                                                  : "Exit Node runtime clear failed.");
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    if (!runtime_result_is_safe(&workspace->runtime, 0u)) {
        result_error(out, TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED,
                     "Runtime did not confirm disabled Exit Node routing on STA.");
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    esp_err_t save_err = service->ops.save_persisted_exit("", service->ops.ctx);
    if (save_err != ESP_OK) {
        persistence_rollback(service, out,
                             save_err == ESP_ERR_INVALID_RESPONSE);
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    out->ok = true;
    out->error = TAILSCALE_SERVICE_OK;
    out->persisted = true;
    copy_text(out->message, sizeof(out->message), "Exit Node is disabled and persisted.");
    xSemaphoreGive(service->mutation_mutex);
    return ESP_OK;
}

esp_err_t tailscale_service_reconnect(tailscale_service_handle_t service,
                                      tailscale_service_result_t *out)
{
    result_init(out);
    if (service == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mutation_begin(service, out)) {
        return ESP_OK;
    }
    if (require_runtime(service, false, out) != ESP_OK) {
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    esp_err_t err = service->ops.rebind(service->ops.ctx);
    if (err != ESP_OK) {
        result_error(out, TAILSCALE_SERVICE_RECONNECT_FAILED,
                     "Tailscale reconnect failed.");
        xSemaphoreGive(service->mutation_mutex);
        return ESP_OK;
    }
    out->ok = true;
    out->error = TAILSCALE_SERVICE_OK;
    copy_text(out->message, sizeof(out->message), "Tailscale reconnect requested.");
    xSemaphoreGive(service->mutation_mutex);
    return ESP_OK;
}

const char *tailscale_service_error_name(tailscale_service_error_t error)
{
    switch (error) {
    case TAILSCALE_SERVICE_OK: return "ok";
    case TAILSCALE_SERVICE_NOT_ENABLED: return "not_enabled";
    case TAILSCALE_SERVICE_NOT_CONNECTED: return "not_connected";
    case TAILSCALE_SERVICE_INVALID_NODE: return "invalid_node";
    case TAILSCALE_SERVICE_AMBIGUOUS_NODE: return "ambiguous_node";
    case TAILSCALE_SERVICE_NODE_NOT_FOUND: return "node_not_found";
    case TAILSCALE_SERVICE_NODE_OFFLINE: return "node_offline";
    case TAILSCALE_SERVICE_NOT_EXIT_NODE: return "not_exit_node";
    case TAILSCALE_SERVICE_SWITCH_TIMEOUT: return "switch_timeout";
    case TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED: return "runtime_apply_failed";
    case TAILSCALE_SERVICE_PERSISTENCE_FAILED: return "persistence_failed";
    case TAILSCALE_SERVICE_ROLLBACK_FAILED: return "rollback_failed";
    case TAILSCALE_SERVICE_RECONNECT_FAILED: return "reconnect_failed";
    case TAILSCALE_SERVICE_BUSY: return "busy";
    default: return "unknown";
    }
}
