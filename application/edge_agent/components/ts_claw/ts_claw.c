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
#include "lwip/netif.h"
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
};

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
    ts_resource_guard_t resource_guard;
    ts_exit_policy_t exit_policy;
    uint64_t next_resource_sample_ms;
    uint64_t pin_next_attempt_ms;
    uint64_t pin_deadline_ms;
    uint64_t error_since_ms;
    bool upstream_pinned;
    char auth_key[TS_CLAW_AUTH_KEY_LEN];
    char hostname[TS_CLAW_HOSTNAME_LEN];
    char login_server[TS_CLAW_LOGIN_SERVER_LEN];
    ts_claw_config_t config;
    ts_claw_status_t status;
} ts_claw_context_t;

static const char *TAG = "ts_claw";
static ts_claw_context_t s_ts;

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
    ts_exit_policy_set_tunnel(&s_ts.exit_policy, connected);
    s_ts.status.exit_state = s_ts.exit_policy.state;
    xSemaphoreGive(s_ts.lock);

    ts_claw_route_hook_set_tunnel_available(connected);
}

static void worker_destroy_microlink(void)
{
    if (s_ts.ml == NULL) {
        return;
    }

    microlink_t *ml = s_ts.ml;
    (void)microlink_pin_wg_output_netif(ml, NULL);
    esp_err_t err = microlink_stop(ml);
    if (err != ESP_OK) {
        set_last_error("microlink stop failed");
        ESP_LOGE(TAG, "microlink_stop failed: %s", esp_err_to_name(err));
    }
    microlink_set_state_callback(ml, NULL, NULL);
    microlink_destroy(ml);
    s_ts.ml = NULL;
    s_ts.upstream_pinned = false;
    s_ts.error_since_ms = 0u;
    ts_claw_route_hook_reset();
    xSemaphoreTake(s_ts.lock, portMAX_DELAY);
    ts_exit_policy_set_tunnel(&s_ts.exit_policy, false);
    s_ts.status.exit_state = s_ts.exit_policy.state;
    xSemaphoreGive(s_ts.lock);
    set_disconnected_status();
}

static esp_err_t worker_start_microlink(void)
{
    struct netif *upstream = worker_lwip_netif_snapshot();
    if (!s_ts.config.enabled || upstream == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

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
        worker_destroy_microlink();
        return err;
    }

    microlink_set_state_callback(s_ts.ml, microlink_state_changed, NULL);
    err = microlink_start(s_ts.ml);
    if (err != ESP_OK) {
        set_last_error("microlink start failed");
        ESP_LOGE(TAG, "microlink_start failed: %s", esp_err_to_name(err));
        worker_destroy_microlink();
        return err;
    }

    const uint64_t current_ms = now_ms();
    s_ts.pin_next_attempt_ms = current_ms;
    s_ts.pin_deadline_ms = current_ms + TS_CLAW_PIN_TIMEOUT_MS;
    s_ts.upstream_pinned = false;
    s_ts.error_since_ms = 0u;
    ts_claw_route_hook_set_upstream_pinned(false);
    set_last_error("");
    return ESP_OK;
}

static void worker_restart_microlink(void)
{
    worker_destroy_microlink();
    if (worker_wifi_snapshot(NULL) && !s_ts.resource_guard.stopped) {
        (void)worker_start_microlink();
    }
}

static void worker_handle_wifi_changed(void)
{
    esp_netif_t *sta_netif = NULL;
    const bool has_ip = worker_wifi_snapshot(&sta_netif);

    if (!has_ip) {
        if (s_ts.ml != NULL) {
            (void)microlink_pin_wg_output_netif(s_ts.ml, NULL);
        }
        s_ts.upstream_pinned = false;
        s_ts.pin_next_attempt_ms = 0u;
        s_ts.pin_deadline_ms = 0u;
        xSemaphoreTake(s_ts.lock, portMAX_DELAY);
        ts_exit_policy_set_tunnel(&s_ts.exit_policy, false);
        s_ts.status.exit_state = s_ts.exit_policy.state;
        xSemaphoreGive(s_ts.lock);
        ts_claw_route_hook_set_upstream_pinned(false);
        ts_claw_route_hook_set_tunnel_available(false);
        set_disconnected_status();
        return;
    }

    if (s_ts.resource_guard.stopped) {
        return;
    }
    if (s_ts.ml == NULL) {
        (void)worker_start_microlink();
        return;
    }
    struct netif *upstream = sta_netif != NULL ?
        (struct netif *)esp_netif_get_netif_impl(sta_netif) : NULL;
    if (upstream == NULL) {
        set_last_error("missing lwIP STA netif");
        return;
    }

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
    s_ts.status.exit_state = s_ts.exit_policy.state;
    copy_string(s_ts.status.egress, sizeof(s_ts.status.egress),
                s_ts.wifi_has_ip ? "sta" : "unavailable");
    xSemaphoreGive(s_ts.lock);
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
        worker_destroy_microlink();
        set_last_error("resource guard stopped tailscale");
    } else if (action == TS_RESOURCE_GUARD_RETRY) {
        const bool started = worker_wifi_snapshot(NULL) &&
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
        break;
    case TS_EVENT_GET_EXIT_NODES:
        result = worker_get_exit_nodes(event->nodes, event->node_capacity,
                                       event->node_count);
        break;
    case TS_EVENT_FACTORY_RESET:
        worker_destroy_microlink();
        result = microlink_factory_reset();
        if (result == ESP_OK && s_ts.config.enabled && worker_wifi_snapshot(NULL) &&
            !s_ts.resource_guard.stopped) {
            result = worker_start_microlink();
        }
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

        bool handle_wifi = false;
        xSemaphoreTake(s_ts.lock, portMAX_DELAY);
        if (s_ts.wifi_event_pending) {
            s_ts.wifi_event_pending = false;
            handle_wifi = true;
        }
        xSemaphoreGive(s_ts.lock);
        if (handle_wifi) {
            worker_handle_wifi_changed();
        }

        const uint64_t current_ms = now_ms();
        worker_try_pin_upstream(current_ms);
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
    s_ts.queue = xQueueCreate(TS_CLAW_QUEUE_DEPTH, sizeof(ts_event_t));
    if (s_ts.queue == NULL) {
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

    if (!send_event) {
        return ESP_OK;
    }

    const ts_event_t event = {.type = TS_EVENT_WIFI_CHANGED};
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
