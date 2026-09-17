#include "ts_claw.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "ping/ping_sock.h"
#include "ts_claw_route_hook.h"

#define TS_CLAW_AUTH_KEY_LEN 320
#define TS_CLAW_HOSTNAME_LEN 64
#define TS_CLAW_LOGIN_SERVER_LEN 320

enum {
    TS_CLAW_QUEUE_DEPTH = 8,
    TS_CLAW_WORKER_STACK = 6144,
    TS_CLAW_WORKER_PRIORITY = 5,
    TS_CLAW_POLL_MS = 250,
    TS_CLAW_PIN_TIMEOUT_MS = 10000,
    TS_CLAW_ERROR_RESTART_MS = 30000,
    TS_CLAW_RESOURCE_SAMPLE_MS = 10000,
    TS_CLAW_EXIT_PROBE_INTERVAL_MS = 5000,
    TS_CLAW_EXIT_PROBE_TIMEOUT_MS = 5000,
    TS_CLAW_EXIT_PROBE_DATA_SIZE = 16,
    TS_CLAW_EXIT_PROBE_QUIESCE_MS = 7000,
    TS_CLAW_EXIT_PROBE_CLEANUP_MS = 1000,
    TS_CLAW_DESTROY_RETRY_MS = 1000,
};

typedef enum {
    TS_DESTROY_RETRY_NONE,
    TS_DESTROY_RETRY_STOP,
    TS_DESTROY_RETRY_RESTART,
} ts_destroy_retry_mode_t;

typedef enum {
    TS_EVENT_WIFI_CHANGED,
    TS_EVENT_GET_EXIT_NODES,
    TS_EVENT_FACTORY_RESET,
} ts_event_type_t;

typedef struct {
    ts_event_type_t type;
    SemaphoreHandle_t reply_signal;
    esp_err_t *reply_result;
    microlink_peer_info_t *nodes;
    size_t node_capacity;
    size_t *node_count;
} ts_event_t;

typedef struct {
    bool initialized;
    bool wifi_has_ip;
    bool wifi_event_pending;
    esp_netif_t *sta_netif;
    QueueHandle_t queue;
    SemaphoreHandle_t lock;
    TaskHandle_t worker;
    microlink_t *ml;
    esp_ping_handle_t exit_ping;
    SemaphoreHandle_t exit_ping_end;
    struct netif *exit_ping_wg_netif;
    uint32_t exit_ping_target;
    uint64_t exit_ping_recreate_after_ms;
    bool exit_ping_accept_results;
    bool exit_ping_stop_requested;
    bool exit_ping_quiesced;
    ts_destroy_retry_mode_t destroy_retry_mode;
    uint64_t destroy_retry_ms;
    ts_resource_guard_t resource_guard;
    ts_exit_policy_t exit_policy;
    uint64_t next_resource_sample_ms;
    uint64_t pin_next_attempt_ms;
    uint64_t pin_deadline_ms;
    uint64_t error_since_ms;
    uint64_t next_start_retry_ms;
    bool upstream_pinned;
    bool factory_reset_stopped;
    char auth_key[TS_CLAW_AUTH_KEY_LEN];
    char hostname[TS_CLAW_HOSTNAME_LEN];
    char login_server[TS_CLAW_LOGIN_SERVER_LEN];
    ts_claw_config_t config;
    ts_claw_status_t status;
} ts_claw_context_t;

static const char *TAG = "ts_claw";
static ts_claw_context_t s_ts;

static esp_err_t send_sync_event(ts_event_t *event);
static void worker_schedule_destroy_retry(ts_destroy_retry_mode_t mode,
                                          uint64_t current_ms);

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void copy_string(char *dest, size_t capacity, const char *source)
{
    if (capacity == 0u) {
        return;
    }
    snprintf(dest, capacity, "%s", source != NULL ? source : "");
}

static void set_last_error(const char *message)
{
    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    copy_string(s_ts.status.last_error, sizeof(s_ts.status.last_error), message);
    xSemaphoreGive(s_ts.lock);
}

static void update_exit_status_locked(void)
{
    s_ts.status.exit_state = s_ts.exit_policy.state;
    copy_string(s_ts.status.egress, sizeof(s_ts.status.egress),
                !s_ts.wifi_has_ip ? "unavailable" :
                ts_exit_policy_routes_public(&s_ts.exit_policy) ? "exit" : "sta");
}

static void set_exit_usable(bool usable)
{
    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    ts_exit_policy_set_tunnel(&s_ts.exit_policy, usable);
    update_exit_status_locked();
    ts_claw_route_hook_set_exit_active(
        ts_exit_policy_routes_public(&s_ts.exit_policy));
    xSemaphoreGive(s_ts.lock);
}

static void set_exit_probe_accept_results(bool accept)
{
    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    s_ts.exit_ping_accept_results = accept;
    xSemaphoreGive(s_ts.lock);
}

static void record_exit_probe(esp_ping_handle_t handle, bool success)
{
    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    if (handle != s_ts.exit_ping || !s_ts.exit_ping_accept_results) {
        xSemaphoreGive(s_ts.lock);
        return;
    }
    ts_exit_policy_on_probe(&s_ts.exit_policy, success);
    update_exit_status_locked();
    ts_claw_route_hook_set_exit_active(
        ts_exit_policy_routes_public(&s_ts.exit_policy));
    xSemaphoreGive(s_ts.lock);
}

static void exit_probe_on_success(esp_ping_handle_t handle, void *args)
{
    (void)args;
    record_exit_probe(handle, true);
}

static void exit_probe_on_timeout(esp_ping_handle_t handle, void *args)
{
    (void)args;
    record_exit_probe(handle, false);
}

static void exit_probe_on_end(esp_ping_handle_t handle, void *args)
{
    (void)args;

    bool notify = false;
    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    if (handle == s_ts.exit_ping && s_ts.exit_ping_stop_requested) {
        s_ts.exit_ping_quiesced = true;
        notify = true;
    }
    xSemaphoreGive(s_ts.lock);
    if (notify) {
        xSemaphoreGive(s_ts.exit_ping_end);
    }
}

static esp_err_t worker_retire_exit_probe(void)
{
    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    esp_ping_handle_t ping = s_ts.exit_ping;
    s_ts.exit_ping_accept_results = false;
    bool stop_requested = s_ts.exit_ping_stop_requested;
    bool quiesced = s_ts.exit_ping_quiesced;
    if (ping != NULL && !stop_requested) {
        s_ts.exit_ping_stop_requested = true;
    }
    xSemaphoreGive(s_ts.lock);

    if (ping == NULL) {
        return ESP_OK;
    }

    if (!stop_requested) {
        (void)xSemaphoreTake(s_ts.exit_ping_end, 0);
        esp_err_t err = esp_ping_stop(ping);
        if (err != ESP_OK) {
            xSemaphoreTake(s_ts.lock, portMAX_DELAY);
            if (s_ts.exit_ping == ping) {
                s_ts.exit_ping_stop_requested = false;
            }
            xSemaphoreGive(s_ts.lock);
            set_last_error("exit probe stop failed");
            return err;
        }
    }

    if (!quiesced &&
        xSemaphoreTake(s_ts.exit_ping_end,
                       pdMS_TO_TICKS(TS_CLAW_EXIT_PROBE_QUIESCE_MS)) != pdTRUE) {
        xSemaphoreTake(s_ts.lock, portMAX_DELAY);
        quiesced = s_ts.exit_ping == ping && s_ts.exit_ping_quiesced;
        xSemaphoreGive(s_ts.lock);
        if (!quiesced) {
            set_last_error("exit probe quiesce timed out");
            return ESP_ERR_TIMEOUT;
        }
    }

    esp_err_t err = esp_ping_delete_session(ping);
    if (err != ESP_OK) {
        set_last_error("exit probe delete failed");
        return err;
    }

    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    if (s_ts.exit_ping == ping) {
        s_ts.exit_ping = NULL;
        s_ts.exit_ping_wg_netif = NULL;
        s_ts.exit_ping_target = 0u;
        s_ts.exit_ping_accept_results = false;
        s_ts.exit_ping_stop_requested = false;
        s_ts.exit_ping_quiesced = false;
        s_ts.exit_ping_recreate_after_ms =
            now_ms() + TS_CLAW_EXIT_PROBE_CLEANUP_MS;
    }
    xSemaphoreGive(s_ts.lock);
    return ESP_OK;
}

static void set_disconnected_status(void)
{
    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    s_ts.status.connected = false;
    s_ts.status.direct_path_available = false;
    s_ts.status.vpn_ip = 0u;
    s_ts.status.peer_count = 0;
    s_ts.status.peer_online = 0;
    copy_string(s_ts.status.egress, sizeof(s_ts.status.egress),
                s_ts.wifi_has_ip ? "sta" : "unavailable");
    xSemaphoreGive(s_ts.lock);
}

static bool worker_wifi_snapshot(esp_netif_t **sta_netif)
{
    bool has_ip;

    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    has_ip = s_ts.wifi_has_ip;
    if (sta_netif != NULL) {
        *sta_netif = s_ts.sta_netif;
    }
    xSemaphoreGive(s_ts.lock);
    return has_ip;
}

static struct netif *worker_lwip_netif_snapshot(void)
{
    esp_netif_t *sta_netif = NULL;
    if (!worker_wifi_snapshot(&sta_netif) || sta_netif == NULL) {
        return NULL;
    }
    return (struct netif *)esp_netif_get_netif_impl(sta_netif);
}

static void microlink_state_changed(microlink_t *ml,
                                    microlink_state_t state,
                                    void *user_data)
{
    (void)ml;
    (void)user_data;

    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    const bool connected = state == ML_STATE_CONNECTED && s_ts.wifi_has_ip;
    s_ts.status.connected = connected;
    if (connected) {
        s_ts.status.vpn_ip = microlink_get_vpn_ip(s_ts.ml);
        s_ts.status.last_error[0] = '\0';
    } else {
        s_ts.status.direct_path_available = false;
    }
    xSemaphoreGive(s_ts.lock);

    ts_claw_route_hook_set_tunnel_available(connected);
    if (!connected) {
        set_exit_usable(false);
    }
}

static esp_err_t worker_destroy_microlink(void)
{
    if (s_ts.ml == NULL) {
        return ESP_OK;
    }

    microlink_t *ml = s_ts.ml;
    ts_claw_route_hook_set_netifs(worker_lwip_netif_snapshot(), NULL);
    set_exit_probe_accept_results(false);
    set_exit_usable(false);
    ts_claw_route_hook_set_tunnel_available(false);
    esp_err_t retire_err = worker_retire_exit_probe();
    if (retire_err != ESP_OK) {
        return retire_err;
    }
    (void)microlink_pin_wg_output_netif(ml, NULL);
    esp_err_t err = microlink_stop(ml);
    if (err != ESP_OK) {
        set_last_error("microlink stop failed");
        ESP_LOGE(TAG, "microlink_stop failed: %s", esp_err_to_name(err));
        return err;
    }
    microlink_set_state_callback(ml, NULL, NULL);
    microlink_destroy(ml);
    s_ts.ml = NULL;
    s_ts.upstream_pinned = false;
    s_ts.error_since_ms = 0u;
    ts_claw_route_hook_reset();
    set_disconnected_status();
    return ESP_OK;
}

static esp_err_t worker_start_microlink(void)
{
    struct netif *upstream = worker_lwip_netif_snapshot();
    if (!s_ts.config.enabled || upstream == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ts_claw_route_hook_set_netifs(upstream, NULL);

    const microlink_config_t config = {
        .auth_key = s_ts.config.auth_key,
        .device_name = s_ts.config.hostname,
        .enable_derp = true,
        .enable_stun = true,
        .enable_disco = true,
        .max_peers = s_ts.config.max_peers,
        .priority_peer_ip = s_ts.config.exit_node_ip,
        .exit_node_ip = s_ts.config.exit_node_ip,
        .ctrl_host = s_ts.config.login_server,
        .advertise_routes = NULL,
        .netcheck_override_enabled = false,
    };

    s_ts.ml = microlink_init(&config);
    if (s_ts.ml == NULL) {
        set_last_error("microlink init failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = microlink_pin_wg_output_netif(s_ts.ml, upstream);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        set_last_error("wireguard upstream preset failed");
        ESP_LOGE(TAG, "microlink upstream preset failed: %s", esp_err_to_name(err));
        if (worker_destroy_microlink() != ESP_OK) {
            worker_schedule_destroy_retry(TS_DESTROY_RETRY_RESTART, now_ms());
        }
        return err;
    }

    microlink_set_state_callback(s_ts.ml, microlink_state_changed, NULL);
    err = microlink_start(s_ts.ml);
    if (err != ESP_OK) {
        set_last_error("microlink start failed");
        ESP_LOGE(TAG, "microlink_start failed: %s", esp_err_to_name(err));
        if (worker_destroy_microlink() != ESP_OK) {
            worker_schedule_destroy_retry(TS_DESTROY_RETRY_RESTART, now_ms());
        }
        return err;
    }

    const uint64_t current_ms = now_ms();
    s_ts.pin_next_attempt_ms = current_ms;
    s_ts.pin_deadline_ms = current_ms + TS_CLAW_PIN_TIMEOUT_MS;
    s_ts.upstream_pinned = false;
    s_ts.error_since_ms = 0u;
    s_ts.next_start_retry_ms = 0u;
    ts_claw_route_hook_set_upstream_pinned(false);
    set_last_error("");
    return ESP_OK;
}

static bool worker_start_is_allowed(void)
{
    return s_ts.config.enabled && worker_wifi_snapshot(NULL) &&
           !s_ts.resource_guard.stopped && !s_ts.factory_reset_stopped;
}

static void worker_schedule_start_retry(uint64_t current_ms)
{
    s_ts.next_start_retry_ms = worker_start_is_allowed() ?
        ts_start_retry_schedule(current_ms) : 0u;
}

static esp_err_t worker_start_with_retry(uint64_t current_ms)
{
    esp_err_t err = worker_start_microlink();
    if (err != ESP_OK) {
        worker_schedule_start_retry(current_ms);
    }
    return err;
}

static void worker_schedule_destroy_retry(ts_destroy_retry_mode_t mode,
                                          uint64_t current_ms)
{
    if (mode == TS_DESTROY_RETRY_STOP ||
        s_ts.destroy_retry_mode == TS_DESTROY_RETRY_NONE) {
        s_ts.destroy_retry_mode = mode;
    }
    s_ts.destroy_retry_ms = current_ms + TS_CLAW_DESTROY_RETRY_MS;
}

static esp_err_t worker_restart_microlink(void)
{
    esp_err_t err = worker_destroy_microlink();
    if (err != ESP_OK) {
        worker_schedule_destroy_retry(TS_DESTROY_RETRY_RESTART, now_ms());
        return err;
    }
    s_ts.destroy_retry_mode = TS_DESTROY_RETRY_NONE;
    s_ts.destroy_retry_ms = 0u;
    if (worker_start_is_allowed()) {
        return worker_start_with_retry(now_ms());
    } else {
        s_ts.next_start_retry_ms = 0u;
    }
    return ESP_OK;
}

static void worker_handle_wifi_changed(bool has_ip, esp_netif_t *sta_netif)
{
    if (!has_ip) {
        ts_claw_route_hook_set_netifs(NULL, NULL);
        set_exit_probe_accept_results(false);
        set_exit_usable(false);
        if (s_ts.ml != NULL) {
            (void)microlink_pin_wg_output_netif(s_ts.ml, NULL);
        }
        s_ts.upstream_pinned = false;
        s_ts.pin_next_attempt_ms = 0u;
        s_ts.pin_deadline_ms = 0u;
        s_ts.next_start_retry_ms = 0u;
        ts_claw_route_hook_set_upstream_pinned(false);
        ts_claw_route_hook_set_tunnel_available(false);
        set_disconnected_status();
        return;
    }

    if (s_ts.resource_guard.stopped) {
        return;
    }
    if (s_ts.ml == NULL) {
        if (worker_start_is_allowed()) {
            (void)worker_start_with_retry(now_ms());
        }
        return;
    }
    struct netif *upstream = sta_netif != NULL ?
        (struct netif *)esp_netif_get_netif_impl(sta_netif) : NULL;
    if (upstream == NULL) {
        set_last_error("missing lwIP STA netif");
        return;
    }
    ts_claw_route_hook_set_netifs(upstream, microlink_get_wg_netif(s_ts.ml));

    const uint64_t current_ms = now_ms();
    esp_err_t err = microlink_pin_wg_output_netif(s_ts.ml, upstream);
    if (err == ESP_OK) {
        s_ts.upstream_pinned = true;
        s_ts.pin_next_attempt_ms = 0u;
        s_ts.pin_deadline_ms = 0u;
        ts_claw_route_hook_set_upstream_pinned(true);
    } else if (err == ESP_ERR_INVALID_STATE) {
        s_ts.upstream_pinned = false;
        s_ts.pin_next_attempt_ms = current_ms;
        s_ts.pin_deadline_ms = current_ms + TS_CLAW_PIN_TIMEOUT_MS;
        ts_claw_route_hook_set_upstream_pinned(false);
    } else {
        set_last_error("wireguard upstream pin failed");
        worker_restart_microlink();
        return;
    }

    err = microlink_rebind(s_ts.ml);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "microlink_rebind failed: %s; restarting", esp_err_to_name(err));
        worker_restart_microlink();
    }
}

static void worker_consume_pending_wifi(void)
{
    bool handle_wifi = false;
    bool has_ip = false;
    esp_netif_t *sta_netif = NULL;

    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    if (s_ts.wifi_event_pending) {
        s_ts.wifi_event_pending = false;
        handle_wifi = true;
        has_ip = s_ts.wifi_has_ip;
        sta_netif = s_ts.sta_netif;
    }
    xSemaphoreGive(s_ts.lock);

    if (handle_wifi) {
        worker_handle_wifi_changed(has_ip, sta_netif);
    }
}

static void worker_try_start_retry(uint64_t current_ms)
{
    if (!ts_start_retry_due(s_ts.next_start_retry_ms, current_ms)) {
        return;
    }
    if (s_ts.ml != NULL || !worker_start_is_allowed()) {
        s_ts.next_start_retry_ms = 0u;
        return;
    }

    s_ts.next_start_retry_ms = 0u;
    (void)worker_start_with_retry(current_ms);
}

static void worker_try_destroy_retry(uint64_t current_ms)
{
    if (s_ts.destroy_retry_mode == TS_DESTROY_RETRY_NONE ||
        current_ms < s_ts.destroy_retry_ms) {
        return;
    }

    const ts_destroy_retry_mode_t mode = s_ts.destroy_retry_mode;
    esp_err_t err = worker_destroy_microlink();
    if (err != ESP_OK) {
        s_ts.destroy_retry_ms = now_ms() + TS_CLAW_DESTROY_RETRY_MS;
        return;
    }

    s_ts.destroy_retry_mode = TS_DESTROY_RETRY_NONE;
    s_ts.destroy_retry_ms = 0u;
    if (mode == TS_DESTROY_RETRY_RESTART && worker_start_is_allowed()) {
        (void)worker_start_with_retry(now_ms());
    }
}

static void worker_try_pin_upstream(uint64_t current_ms)
{
    if (s_ts.ml == NULL || !worker_wifi_snapshot(NULL) || s_ts.upstream_pinned ||
        s_ts.pin_deadline_ms == 0u || current_ms < s_ts.pin_next_attempt_ms) {
        return;
    }

    if (current_ms >= s_ts.pin_deadline_ms) {
        set_last_error("wireguard upstream pin timed out");
        s_ts.pin_deadline_ms = 0u;
        ts_claw_route_hook_set_upstream_pinned(false);
        return;
    }

    struct netif *upstream = worker_lwip_netif_snapshot();
    if (upstream == NULL) {
        set_last_error("missing lwIP STA netif");
        s_ts.pin_deadline_ms = 0u;
        return;
    }

    esp_err_t err = microlink_pin_wg_output_netif(s_ts.ml, upstream);
    if (err == ESP_OK) {
        s_ts.upstream_pinned = true;
        ts_claw_route_hook_set_upstream_pinned(true);
        return;
    }
    if (err == ESP_ERR_INVALID_STATE && current_ms < s_ts.pin_deadline_ms) {
        s_ts.pin_next_attempt_ms = current_ms + TS_CLAW_POLL_MS;
        return;
    }

    set_last_error(err == ESP_ERR_INVALID_STATE ?
                   "wireguard upstream pin timed out" :
                   "wireguard upstream pin failed");
    s_ts.pin_deadline_ms = 0u;
}

static void worker_refresh_status(void)
{
    if (s_ts.ml == NULL) {
        return;
    }

    microlink_diag_t diag;
    if (microlink_get_diag(s_ts.ml, &diag) != ESP_OK) {
        return;
    }

    bool direct = false;
    for (int i = 0; i < diag.peer_count; ++i) {
        microlink_peer_info_t peer = {0};
        if (microlink_get_peer_info(s_ts.ml, i, &peer) == ESP_OK &&
            peer.online && peer.direct_path) {
            direct = true;
            break;
        }
    }

    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    s_ts.status.connected = diag.connected && s_ts.wifi_has_ip;
    s_ts.status.direct_path_available = direct && s_ts.status.connected;
    s_ts.status.vpn_ip = diag.vpn_ip;
    s_ts.status.peer_count = diag.peer_count > 0 ? diag.peer_count : 0;
    s_ts.status.peer_online = diag.peer_online > 0 ? diag.peer_online : 0;
    update_exit_status_locked();
    xSemaphoreGive(s_ts.lock);
}

static esp_err_t worker_start_exit_probe(struct netif *wg_netif)
{
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = ESP_PING_COUNT_INFINITE;
    config.interval_ms = TS_CLAW_EXIT_PROBE_INTERVAL_MS;
    config.timeout_ms = TS_CLAW_EXIT_PROBE_TIMEOUT_MS;
    config.data_size = TS_CLAW_EXIT_PROBE_DATA_SIZE;
    config.interface = netif_get_index(wg_netif);
    IP_SET_TYPE_VAL(config.target_addr, IPADDR_TYPE_V4);
    ip4_addr_set_u32(ip_2_ip4(&config.target_addr),
                     lwip_htonl(s_ts.config.exit_node_ip));

    const esp_ping_callbacks_t callbacks = {
        .on_ping_success = exit_probe_on_success,
        .on_ping_timeout = exit_probe_on_timeout,
        .on_ping_end = exit_probe_on_end,
        .cb_args = NULL,
    };
    esp_ping_handle_t ping = NULL;
    esp_err_t err = esp_ping_new_session(&config, &callbacks, &ping);
    if (err == ESP_OK) {
        xSemaphoreTake(s_ts.lock, portMAX_DELAY);
        s_ts.exit_ping = ping;
        s_ts.exit_ping_wg_netif = wg_netif;
        s_ts.exit_ping_target = s_ts.config.exit_node_ip;
        s_ts.exit_ping_accept_results = false;
        s_ts.exit_ping_stop_requested = false;
        s_ts.exit_ping_quiesced = false;
        xSemaphoreGive(s_ts.lock);
        err = esp_ping_start(ping);
    }
    if (err != ESP_OK) {
        if (ping != NULL) {
            xSemaphoreTake(s_ts.lock, portMAX_DELAY);
            if (s_ts.exit_ping == ping) {
                s_ts.exit_ping = NULL;
                s_ts.exit_ping_wg_netif = NULL;
                s_ts.exit_ping_target = 0u;
                s_ts.exit_ping_accept_results = false;
                s_ts.exit_ping_stop_requested = false;
                s_ts.exit_ping_quiesced = false;
            }
            xSemaphoreGive(s_ts.lock);
            (void)esp_ping_delete_session(ping);
        }
        s_ts.exit_ping_recreate_after_ms =
            now_ms() + TS_CLAW_EXIT_PROBE_CLEANUP_MS;
        set_last_error("exit probe start failed");
        return err;
    }
    return ESP_OK;
}

static void worker_manage_exit_probe(uint64_t current_ms)
{
    struct netif *sta_netif = worker_lwip_netif_snapshot();
    struct netif *wg_netif = s_ts.ml != NULL ? microlink_get_wg_netif(s_ts.ml) : NULL;
    const bool tunnel_available = s_ts.ml != NULL && sta_netif != NULL &&
                                  microlink_is_connected(s_ts.ml) && wg_netif != NULL;

    ts_claw_route_hook_set_netifs(sta_netif, wg_netif);
    ts_claw_route_hook_set_tunnel_available(tunnel_available);

    const bool ready = s_ts.destroy_retry_mode == TS_DESTROY_RETRY_NONE &&
                       s_ts.config.exit_node_ip != 0u && tunnel_available &&
                       s_ts.upstream_pinned &&
                       microlink_selected_exit_ready(s_ts.ml);
    if (!ready) {
        set_exit_probe_accept_results(false);
        set_exit_usable(false);
        return;
    }

    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    esp_ping_handle_t ping = s_ts.exit_ping;
    const uint32_t ping_target = s_ts.exit_ping_target;
    struct netif *ping_wg_netif = s_ts.exit_ping_wg_netif;
    const bool stop_requested = s_ts.exit_ping_stop_requested;
    xSemaphoreGive(s_ts.lock);

    if (ping != NULL && (ping_target != s_ts.config.exit_node_ip ||
                         ping_wg_netif != wg_netif || stop_requested)) {
        set_exit_probe_accept_results(false);
        set_exit_usable(false);
        if (worker_retire_exit_probe() != ESP_OK) {
            return;
        }
        ping = NULL;
    }
    if (ping == NULL && current_ms < s_ts.exit_ping_recreate_after_ms) {
        set_exit_usable(false);
        return;
    }
    if (ping == NULL && worker_start_exit_probe(wg_netif) != ESP_OK) {
        set_exit_usable(false);
        return;
    }

    set_exit_usable(true);
    set_exit_probe_accept_results(true);
}

static void worker_check_error(uint64_t current_ms)
{
    if (s_ts.ml == NULL || !worker_wifi_snapshot(NULL)) {
        s_ts.error_since_ms = 0u;
        return;
    }

    if (microlink_get_state(s_ts.ml) != ML_STATE_ERROR) {
        s_ts.error_since_ms = 0u;
        return;
    }

    if (s_ts.error_since_ms == 0u) {
        s_ts.error_since_ms = current_ms;
        set_last_error("microlink error");
        return;
    }
    if (current_ms - s_ts.error_since_ms >= TS_CLAW_ERROR_RESTART_MS) {
        worker_restart_microlink();
    }
}

static void worker_sample_resources(uint64_t current_ms)
{
    if (current_ms < s_ts.next_resource_sample_ms) {
        return;
    }
    s_ts.next_resource_sample_ms = current_ms + TS_CLAW_RESOURCE_SAMPLE_MS;

    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    s_ts.status.internal_free = internal_free;
    s_ts.status.internal_largest = internal_largest;
    s_ts.status.psram_free = psram_free;
    xSemaphoreGive(s_ts.lock);

    if (s_ts.ml == NULL && !s_ts.resource_guard.stopped) {
        return;
    }
    ts_resource_guard_action_t action = ts_resource_guard_sample(
        &s_ts.resource_guard, current_ms, internal_free, internal_largest);
    if (action == TS_RESOURCE_GUARD_STOP) {
        s_ts.next_start_retry_ms = 0u;
        esp_err_t err = worker_destroy_microlink();
        if (err != ESP_OK) {
            worker_schedule_destroy_retry(TS_DESTROY_RETRY_STOP, now_ms());
        } else {
            s_ts.destroy_retry_mode = TS_DESTROY_RETRY_NONE;
            s_ts.destroy_retry_ms = 0u;
            set_last_error("resource guard stopped tailscale");
        }
    } else if (action == TS_RESOURCE_GUARD_RETRY) {
        const bool started = s_ts.destroy_retry_mode == TS_DESTROY_RETRY_NONE &&
                             s_ts.ml == NULL && worker_start_is_allowed() &&
                             worker_start_microlink() == ESP_OK;
        ts_resource_guard_retry_completed(&s_ts.resource_guard, current_ms, started);
    }
}

static esp_err_t worker_get_exit_nodes(microlink_peer_info_t *nodes,
                                       size_t capacity,
                                       size_t *count)
{
    *count = 0u;
    if (s_ts.ml == NULL) {
        return ESP_OK;
    }

    const int peers = microlink_get_peer_count(s_ts.ml);
    for (int i = 0; i < peers; ++i) {
        microlink_peer_info_t peer = {0};
        esp_err_t err = microlink_get_peer_info(s_ts.ml, i, &peer);
        if (err != ESP_OK) {
            return err;
        }
        if (peer.is_exit_node && *count < capacity) {
            nodes[*count] = peer;
            (*count)++;
        }
    }
    return ESP_OK;
}

static void worker_handle_event(const ts_event_t *event)
{
    esp_err_t result = ESP_OK;

    switch (event->type) {
    case TS_EVENT_WIFI_CHANGED:
        worker_consume_pending_wifi();
        break;
    case TS_EVENT_GET_EXIT_NODES:
        result = worker_get_exit_nodes(event->nodes, event->node_capacity,
                                       event->node_count);
        break;
    case TS_EVENT_FACTORY_RESET:
        s_ts.factory_reset_stopped = true;
        s_ts.next_start_retry_ms = 0u;
        result = worker_destroy_microlink();
        if (result != ESP_OK) {
            worker_schedule_destroy_retry(TS_DESTROY_RETRY_STOP, now_ms());
            break;
        }
        s_ts.destroy_retry_mode = TS_DESTROY_RETRY_NONE;
        s_ts.destroy_retry_ms = 0u;
        result = microlink_factory_reset();
        break;
    default:
        result = ESP_ERR_INVALID_ARG;
        break;
    }

    if (event->reply_signal != NULL) {
        *event->reply_result = result;
        xSemaphoreGive(event->reply_signal);
    }
}

static void ts_claw_worker(void *arg)
{
    (void)arg;
    s_ts.next_resource_sample_ms = now_ms() + TS_CLAW_RESOURCE_SAMPLE_MS;

    for (;;) {
        ts_event_t event;
        if (xQueueReceive(s_ts.queue, &event, pdMS_TO_TICKS(TS_CLAW_POLL_MS)) == pdTRUE) {
            worker_handle_event(&event);
        }

        worker_consume_pending_wifi();

        const uint64_t current_ms = now_ms();
        worker_try_destroy_retry(current_ms);
        worker_try_start_retry(current_ms);
        worker_try_pin_upstream(current_ms);
        worker_manage_exit_probe(current_ms);
        worker_refresh_status();
        worker_check_error(current_ms);
        worker_sample_resources(current_ms);
    }
}

esp_err_t ts_claw_init(const ts_claw_config_t *config)
{
    if (config == NULL || s_ts.initialized ||
        (config->enabled && (config->auth_key == NULL || config->auth_key[0] == '\0' ||
                            config->hostname == NULL || config->hostname[0] == '\0' ||
                            config->max_peers == 0u))) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_ts, 0, sizeof(s_ts));
    copy_string(s_ts.auth_key, sizeof(s_ts.auth_key), config->auth_key);
    copy_string(s_ts.hostname, sizeof(s_ts.hostname), config->hostname);
    copy_string(s_ts.login_server, sizeof(s_ts.login_server), config->login_server);
    s_ts.config = *config;
    s_ts.config.auth_key = s_ts.auth_key;
    s_ts.config.hostname = s_ts.hostname;
    s_ts.config.login_server = s_ts.login_server;

    s_ts.lock = xSemaphoreCreateMutex();
    if (s_ts.lock == NULL) {
        memset(&s_ts, 0, sizeof(s_ts));
        return ESP_ERR_NO_MEM;
    }
    s_ts.exit_ping_end = xSemaphoreCreateBinary();
    if (s_ts.exit_ping_end == NULL) {
        vSemaphoreDelete(s_ts.lock);
        memset(&s_ts, 0, sizeof(s_ts));
        return ESP_ERR_NO_MEM;
    }
    s_ts.queue = xQueueCreate(TS_CLAW_QUEUE_DEPTH, sizeof(ts_event_t));
    if (s_ts.queue == NULL) {
        vSemaphoreDelete(s_ts.exit_ping_end);
        vSemaphoreDelete(s_ts.lock);
        memset(&s_ts, 0, sizeof(s_ts));
        return ESP_ERR_NO_MEM;
    }

    s_ts.status.enabled = config->enabled;
    s_ts.status.auth_key_set = s_ts.auth_key[0] != '\0';
    s_ts.status.exit_state = config->exit_node_ip != 0u ? TS_EXIT_PENDING : TS_EXIT_DISABLED;
    copy_string(s_ts.status.egress, sizeof(s_ts.status.egress), "unavailable");
    ts_resource_guard_init(&s_ts.resource_guard);
    ts_exit_policy_init(&s_ts.exit_policy, config->exit_node_ip != 0u);
    ts_claw_route_hook_reset();

    BaseType_t created = xTaskCreate(ts_claw_worker, "ts_claw", TS_CLAW_WORKER_STACK,
                                     NULL, TS_CLAW_WORKER_PRIORITY, &s_ts.worker);
    if (created != pdPASS) {
        vQueueDelete(s_ts.queue);
        vSemaphoreDelete(s_ts.exit_ping_end);
        vSemaphoreDelete(s_ts.lock);
        memset(&s_ts, 0, sizeof(s_ts));
        return ESP_ERR_NO_MEM;
    }

    s_ts.initialized = true;
    return ESP_OK;
}

esp_err_t ts_claw_notify_wifi(bool sta_has_ip, esp_netif_t *sta_netif)
{
    if (!s_ts.initialized || (sta_has_ip && sta_netif == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    bool send_event = false;
    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    s_ts.wifi_has_ip = sta_has_ip;
    s_ts.sta_netif = sta_has_ip ? sta_netif : NULL;
    if (!s_ts.wifi_event_pending) {
        s_ts.wifi_event_pending = true;
        send_event = true;
    }
    xSemaphoreGive(s_ts.lock);

    const ts_event_t event = {.type = TS_EVENT_WIFI_CHANGED};
    if (!sta_has_ip) {
        if (xTaskGetCurrentTaskHandle() == s_ts.worker) {
            worker_consume_pending_wifi();
            return ESP_OK;
        }
        ts_event_t sync_event = event;
        return send_sync_event(&sync_event);
    }
    if (!send_event) {
        return ESP_OK;
    }
    if (xQueueSend(s_ts.queue, &event, 0) == pdTRUE) {
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t ts_claw_get_status(ts_claw_status_t *out_status)
{
    if (!s_ts.initialized || out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    *out_status = s_ts.status;
    xSemaphoreGive(s_ts.lock);
    return ESP_OK;
}

static esp_err_t send_sync_event(ts_event_t *event)
{
    if (xTaskGetCurrentTaskHandle() == s_ts.worker) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_FAIL;
    SemaphoreHandle_t reply_signal = xSemaphoreCreateBinary();
    if (reply_signal == NULL) {
        return ESP_ERR_NO_MEM;
    }
    event->reply_signal = reply_signal;
    event->reply_result = &result;
    if (xQueueSend(s_ts.queue, event, portMAX_DELAY) != pdTRUE) {
        vSemaphoreDelete(reply_signal);
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(reply_signal, portMAX_DELAY);
    vSemaphoreDelete(reply_signal);
    return result;
}

int ts_claw_get_exit_nodes(microlink_peer_info_t *out_nodes, int capacity)
{
    if (!s_ts.initialized || capacity < 0 || (capacity > 0 && out_nodes == NULL)) {
        return -ESP_ERR_INVALID_ARG;
    }

    size_t count = 0u;

    ts_event_t event = {
        .type = TS_EVENT_GET_EXIT_NODES,
        .nodes = out_nodes,
        .node_capacity = (size_t)capacity,
        .node_count = &count,
    };
    esp_err_t err = send_sync_event(&event);
    if (err != ESP_OK) {
        return err < 0 ? (int)err : -(int)err;
    }
    return (int)count;
}

esp_err_t ts_claw_factory_reset(void)
{
    if (!s_ts.initialized) {
        return microlink_factory_reset();
    }

    ts_event_t event = {.type = TS_EVENT_FACTORY_RESET};
    return send_sync_event(&event);
}
