# Agent-Aware Tailscale Capability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give ESP-Claw TS a native, safety-bounded Tailscale skill with DERP diagnostics, explicitly requested live/persistent Exit Node control, lightweight reconnect, and an equivalent web control.

**Architecture:** A reusable `cap_tailscale` component exposes five model-callable tools through provider callbacks. An application-owned `tailscale_service` adapts those callbacks to `ts_claw`, owns selector validation and the runtime/persistence rollback transaction, and is also called by the HTTP API so the agent and web UI cannot diverge. All MicroLink access and lifecycle mutations remain serialized on the existing TS-Claw worker.

**Tech Stack:** ESP-IDF C, FreeRTOS queues/semaphores, MicroLink, cJSON, ESP-Claw capability/skill framework, SolidJS/TypeScript, Vitest, CMake/CTest, pnpm.

---

## File map

### New reusable capability files

- `components/claw_capabilities/cap_tailscale/CMakeLists.txt`: ESP-IDF component registration.
- `components/claw_capabilities/cap_tailscale/include/cap_tailscale.h`: safe model-visible types and provider contract.
- `components/claw_capabilities/cap_tailscale/src/cap_tailscale.c`: schemas, JSON parsing/rendering, confirmation guard, and capability registration.
- `components/claw_capabilities/cap_tailscale/src/cap_tailscale_contract.c`: host-testable selector and mutation-input validation helpers.
- `components/claw_capabilities/cap_tailscale/private_include/cap_tailscale_contract.h`: private helper declarations.
- `components/claw_capabilities/cap_tailscale/skills/tailscale_network/SKILL.md`: the single agent policy and usage guide.
- `components/claw_capabilities/cap_tailscale/tests/host/CMakeLists.txt`: host test target.
- `components/claw_capabilities/cap_tailscale/tests/host/test_cap_tailscale_contract.c`: validation and output-boundary tests.
- `components/claw_capabilities/cap_tailscale/tests/host/stubs/esp_err.h`: minimal ESP error constants for the pure host test.

### New application service files

- `application/edge_agent/components/tailscale_service/CMakeLists.txt`: TS-Claw-board-only component registration.
- `application/edge_agent/components/tailscale_service/include/tailscale_service.h`: shared service contract used by the capability and HTTP adapters.
- `application/edge_agent/components/tailscale_service/tailscale_service.c`: selector resolution, mutation serialization, persistence, and rollback.
- `application/edge_agent/components/tailscale_service/tests/host/CMakeLists.txt`: fake-provider transaction tests.
- `application/edge_agent/components/tailscale_service/tests/host/test_tailscale_service.c`: success/rejection/rollback/busy tests.
- `application/edge_agent/components/tailscale_service/tests/host/stubs/esp_err.h`: host error constants.

### Existing firmware files to modify

- `application/edge_agent/components/ts_claw/include/ts_claw.h`: diagnostic, Exit Node, set/clear, and rebind APIs.
- `application/edge_agent/components/ts_claw/ts_claw.c`: worker events and safe snapshots/lifecycle operations.
- `application/edge_agent/components/ts_claw/tests/host/CMakeLists.txt`: add pure diagnostic tests.
- `application/edge_agent/components/ts_claw/tests/host/test_ts_claw_diagnostics.c`: timestamp-to-age and RTT conversion tests.
- `application/edge_agent/main/CMakeLists.txt`: link `cap_tailscale` and `tailscale_service` on the TS-Claw board.
- `application/edge_agent/main/main.c`: provider registration, service adapters, and shared HTTP callbacks.
- `application/edge_agent/components/http_server/include/http_server.h`: live-operation service callbacks and response types.
- `application/edge_agent/components/http_server/http_server_tailscale_api.c`: POST live switch/clear/reconnect endpoints.
- `application/edge_agent/components/http_server/http_server_tailscale_request.c`: cJSON request parser shared by handlers and host tests.
- `application/edge_agent/components/http_server/http_server_tailscale_request.h`: private request-parser contract.
- `application/edge_agent/components/http_server/tests/host/CMakeLists.txt`: host parser test target using ESP-IDF cJSON.
- `application/edge_agent/components/http_server/tests/host/test_http_server_tailscale_request.c`: malformed/unknown/missing-field request tests.

### Existing frontend files to modify or add

- `application/edge_agent/components/http_server/frontend_source/package.json`: add Vitest test command/dependencies.
- `application/edge_agent/components/http_server/frontend_source/pnpm-lock.yaml`: lock new test dependencies.
- `application/edge_agent/components/http_server/frontend_source/src/api/client.ts`: live mutation APIs and richer diagnostics types.
- `application/edge_agent/components/http_server/frontend_source/src/pages/TailscalePage.tsx`: visible, live Exit Node control with disabled offline entries and operation state.
- `application/edge_agent/components/http_server/frontend_source/src/pages/TailscalePage.test.tsx`: rendered empty, online, offline, error, and switching states.
- `application/edge_agent/components/http_server/frontend_source/src/test/setup.ts`: DOM test setup.
- `application/edge_agent/components/http_server/frontend_source/vite.config.ts`: Vitest configuration.
- `application/edge_agent/components/http_server/frontend_source/src/i18n/en.ts`: operation labels/messages.
- `application/edge_agent/components/http_server/frontend_source/src/i18n/zh-cn.ts`: matching Chinese strings.

### Documentation files to modify

- `README.md`
- `README_CN.md`
- `docs/ESP_CLAW_TS.md`
- `docs/ESP_CLAW_TS_CN.md`
- `docs/src/content/docs/en/reference-project/skills-and-capability.mdx`
- `docs/src/content/docs/zh-cn/reference-project/skills-and-capability.mdx`

## Task 1: Add the `cap_tailscale` public contract and pure validation tests

**Files:**
- Create: `components/claw_capabilities/cap_tailscale/include/cap_tailscale.h`
- Create: `components/claw_capabilities/cap_tailscale/private_include/cap_tailscale_contract.h`
- Create: `components/claw_capabilities/cap_tailscale/src/cap_tailscale_contract.c`
- Create: `components/claw_capabilities/cap_tailscale/tests/host/CMakeLists.txt`
- Create: `components/claw_capabilities/cap_tailscale/tests/host/test_cap_tailscale_contract.c`
- Create: `components/claw_capabilities/cap_tailscale/tests/host/stubs/esp_err.h`

- [ ] **Step 1: Write the failing contract tests**

Define tests that assert:

```c
TEST_CHECK(cap_tailscale_validate_read_args(0) == ESP_OK);
TEST_CHECK(cap_tailscale_validate_read_args(1) == ESP_ERR_INVALID_ARG);
TEST_CHECK(cap_tailscale_validate_mutation_args(true, true, 0) == ESP_OK);
TEST_CHECK(cap_tailscale_validate_mutation_args(true, false, 0) ==
           ESP_ERR_INVALID_STATE);
TEST_CHECK(cap_tailscale_validate_mutation_args(true, true, 1) ==
           ESP_ERR_INVALID_ARG);
TEST_CHECK(cap_tailscale_selector_is_cgnat("100.64.0.1"));
TEST_CHECK(cap_tailscale_selector_is_cgnat("100.127.255.254"));
TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.128.0.1"));
TEST_CHECK(!cap_tailscale_selector_is_cgnat("192.168.1.1"));
```

Also verify selector length, empty selector, missing confirmation, wrong
confirmation type, unexpected parsed fields, and bounded output helpers.
Malformed JSON and JSON type errors are covered by the firmware-level handler
smoke in Task 9 because cJSON remains an ESP-IDF dependency rather than being
duplicated in the host-test target.

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
cmake -S components/claw_capabilities/cap_tailscale/tests/host \
      -B /tmp/esp-claw-cap-tailscale-contract
cmake --build /tmp/esp-claw-cap-tailscale-contract -j 4
ctest --test-dir /tmp/esp-claw-cap-tailscale-contract --output-on-failure
```

Expected: configure or compile failure because the header and implementation do not yet exist.

- [ ] **Step 3: Define safe public types and the provider interface**

Use fixed-size, bounded structures; do not include auth keys, WAN public IPs,
public keys, or raw control-plane payloads:

```c
#define CAP_TAILSCALE_HOSTNAME_LEN 96
#define CAP_TAILSCALE_IP_LEN 16
#define CAP_TAILSCALE_REGION_NAME_LEN 48
#define CAP_TAILSCALE_ERROR_LEN 96
#define CAP_TAILSCALE_MAX_EXIT_NODES 16
#define CAP_TAILSCALE_MAX_DERP_RTTS 8

typedef struct {
    uint16_t id;
    char name[CAP_TAILSCALE_REGION_NAME_LEN];
} cap_tailscale_region_t;

typedef struct {
    cap_tailscale_region_t region;
    uint16_t rtt_ms;
    bool timed_out;
} cap_tailscale_derp_rtt_t;

typedef struct {
    bool enabled;
    bool connected;
    char hostname[CAP_TAILSCALE_HOSTNAME_LEN];
    char vpn_ip[CAP_TAILSCALE_IP_LEN];
    char path[16];
    int peer_count;
    int peer_online;
    char exit_node[CAP_TAILSCALE_IP_LEN];
    char exit_state[16];
    char egress[16];
    char last_error[CAP_TAILSCALE_ERROR_LEN];
    cap_tailscale_region_t derp_active;
    cap_tailscale_region_t derp_default;
    cap_tailscale_derp_rtt_t derp_rtts[CAP_TAILSCALE_MAX_DERP_RTTS];
    size_t derp_rtt_count;
    uint64_t derp_heartbeat_age_ms;
    uint64_t control_rx_age_ms;
    uint32_t reconnect_coord_watchdog;
    uint32_t reconnect_coord_transport;
    uint32_t reconnect_derp_watchdog;
    uint32_t reconnect_derp_retry;
} cap_tailscale_status_t;

typedef struct {
    char ip[CAP_TAILSCALE_IP_LEN];
    char hostname[CAP_TAILSCALE_HOSTNAME_LEN];
    bool online;
    bool direct;
    cap_tailscale_region_t derp_region;
} cap_tailscale_exit_node_t;

typedef struct {
    bool ok;
    char error[32];
    char message[CAP_TAILSCALE_ERROR_LEN];
    char selected_ip[CAP_TAILSCALE_IP_LEN];
    char selected_hostname[CAP_TAILSCALE_HOSTNAME_LEN];
    char exit_state[16];
    char egress[16];
    bool persisted;
} cap_tailscale_mutation_result_t;

typedef struct {
    esp_err_t (*get_status)(cap_tailscale_status_t *out, void *ctx);
    int (*list_exit_nodes)(cap_tailscale_exit_node_t *out, size_t capacity, void *ctx);
    esp_err_t (*set_exit_node)(const char *selector,
                               cap_tailscale_mutation_result_t *out,
                               void *ctx);
    esp_err_t (*clear_exit_node)(cap_tailscale_mutation_result_t *out, void *ctx);
    esp_err_t (*reconnect)(cap_tailscale_status_t *out, void *ctx);
    void *ctx;
} cap_tailscale_provider_t;

esp_err_t cap_tailscale_set_provider(const cap_tailscale_provider_t *provider);
esp_err_t cap_tailscale_register_group(void);
```

Provider mutation callbacks return `ESP_OK` whenever they produced a complete
structured result, including a semantic rejection such as `node_offline`.
They return a non-OK `esp_err_t` only when no safe structured result could be
produced. This guarantees that the capability can preserve stable error names
instead of collapsing every rejection into a generic ESP error.

- [ ] **Step 4: Implement strict host-testable validators**

Keep policy validation in `cap_tailscale_contract.c`. Its inputs are already
parsed field counts, presence flags, boolean values, and selector strings.
Reject unknown fields in mutation objects, require confirmation to be present
and true, trim surrounding selector whitespace, cap selectors at
`CAP_TAILSCALE_HOSTNAME_LEN - 1`, and validate IP selectors against
`100.64.0.0/10`. The production cJSON handler parses the object and calls these
pure helpers for policy decisions.

- [ ] **Step 5: Run the tests and verify they pass**

Run the Task 1 CMake/CTest commands again.

Expected: one test executable passes with zero failures.

- [ ] **Step 6: Commit the contract**

```bash
git add components/claw_capabilities/cap_tailscale
git commit -m "feat: define Tailscale capability contract"
```

## Task 2: Register the five model tools and package the skill

**Files:**
- Create: `components/claw_capabilities/cap_tailscale/CMakeLists.txt`
- Create: `components/claw_capabilities/cap_tailscale/src/cap_tailscale.c`
- Create: `components/claw_capabilities/cap_tailscale/skills/tailscale_network/SKILL.md`
- Modify: `components/claw_capabilities/cap_tailscale/tests/host/test_cap_tailscale_contract.c`

- [ ] **Step 1: Extend the tests with descriptor and redaction expectations**

Expose descriptor ids and safe JSON render helpers from the private contract
module, then assert the ids are exactly:

```text
tailscale_status
tailscale_list_exit_nodes
tailscale_set_exit_node
tailscale_clear_exit_node
tailscale_reconnect
```

Feed the pure render helper a status structure containing sentinel text in every
safe field. Assert rendered output contains the safe fields but does not contain
any of:

```text
auth_key
wan_public_ip
public_key
private_key
control_payload
```

- [ ] **Step 2: Run the host test and verify the new assertions fail**

Run the Task 1 CMake/CTest commands.

Expected: failure because descriptors and render helpers are not implemented.

- [ ] **Step 3: Implement provider-backed capability handlers**

Register one `claw_cap_group_t` named `cap_tailscale` with the five descriptors.
Use these input schemas:

```json
{"type":"object","properties":{},"additionalProperties":false}
```

```json
{"type":"object","properties":{"node":{"type":"string","minLength":1,"maxLength":95},"user_confirmed":{"type":"boolean","const":true}},"required":["node","user_confirmed"],"additionalProperties":false}
```

```json
{"type":"object","properties":{"user_confirmed":{"type":"boolean","const":true}},"required":["user_confirmed"],"additionalProperties":false}
```

The two read-only handlers call the provider directly. The three mutating
handlers must return `ESP_ERR_INVALID_STATE` and a user-safe error if
`user_confirmed` is absent or not true. Map service errors to stable JSON with
`ok`, `error`, `message`, and `state`; successful mutations return `ok`, node,
state, egress, and persisted.

- [ ] **Step 4: Add the component skill**

Use valid JSON frontmatter:

```markdown
---
{
  "name": "tailscale_network",
  "description": "Inspect this device's Tailscale connection, IP, DERP path and Exit Nodes; switch, clear, or reconnect only when the current user explicitly requests it.",
  "metadata": {
    "cap_groups": ["cap_tailscale"],
    "manage_mode": "readonly"
  }
}
---

# Tailscale Network
```

The body must encode the confirmed rules: diagnosis is read-only by default;
mutation requires an explicit current-user request; DERP is not automatically
a failure; hostname/auth/login-server/identity changes stay in the settings
page; fallback/pending/rollback is never called success; the device does not
administer the whole tailnet.

- [ ] **Step 5: Build the component through ESP-IDF**

```bash
git submodule update --init --recursive
source /Users/sheneyan/esp/esp-idf/export.sh
cd application/edge_agent
idf.py bmgr -c ./boards -b esp32_s3_n16r8_ts_claw
idf.py reconfigure
```

Expected: configuration succeeds and skill synchronization reports no duplicate
skill id or invalid frontmatter.

- [ ] **Step 6: Run the contract tests and commit**

Run Task 1 CTest, then:

```bash
git add components/claw_capabilities/cap_tailscale
git commit -m "feat: add Tailscale tools and agent skill"
```

## Task 3: Expose bounded DERP and peer diagnostics from TS-Claw

**Files:**
- Modify: `application/edge_agent/components/ts_claw/include/ts_claw.h`
- Modify: `application/edge_agent/components/ts_claw/ts_claw.c`
- Modify: `application/edge_agent/components/ts_claw/tests/host/CMakeLists.txt`
- Create: `application/edge_agent/components/ts_claw/tests/host/test_ts_claw_diagnostics.c`
- Create: `application/edge_agent/components/ts_claw/ts_claw_diagnostics.c`
- Create: `application/edge_agent/components/ts_claw/ts_claw_diagnostics.h`

- [ ] **Step 1: Write failing diagnostic conversion tests**

Cover:

```c
TEST_CHECK(ts_claw_timestamp_age_ms(1000, 0) == 0);
TEST_CHECK(ts_claw_timestamp_age_ms(1000, 750) == 250);
TEST_CHECK(ts_claw_timestamp_age_ms(750, 1000) == 0);
```

Feed RTT entries `{9, 224}`, `{2, 168}`, and `{7, 0}` and assert the public
snapshot preserves bounded order, marks region 7 timed out, and never interprets
zero as zero latency. Assert an unknown region has an empty name.

- [ ] **Step 2: Run TS-Claw host tests and verify failure**

```bash
cmake -S application/edge_agent/components/ts_claw/tests/host \
      -B /tmp/esp-claw-ts-claw-host
cmake --build /tmp/esp-claw-ts-claw-host -j 4
ctest --test-dir /tmp/esp-claw-ts-claw-host --output-on-failure
```

Expected: compile failure because diagnostic conversion helpers are absent.

- [ ] **Step 3: Add public diagnostic types**

Extend `ts_claw.h` with fixed bounds and these APIs:

```c
#define TS_CLAW_MAX_DERP_RTTS 8
#define TS_CLAW_REGION_NAME_LEN 48
#define TS_CLAW_PEER_HOSTNAME_LEN 96

typedef struct {
    uint16_t region_id;
    char region_name[TS_CLAW_REGION_NAME_LEN];
    uint16_t rtt_ms;
    bool timed_out;
} ts_claw_derp_rtt_t;

typedef struct {
    ts_claw_status_t status;
    char hostname[TS_CLAW_PEER_HOSTNAME_LEN];
    uint16_t derp_active_region;
    char derp_active_name[TS_CLAW_REGION_NAME_LEN];
    uint16_t derp_default_region;
    char derp_default_name[TS_CLAW_REGION_NAME_LEN];
    ts_claw_derp_rtt_t derp_rtts[TS_CLAW_MAX_DERP_RTTS];
    size_t derp_rtt_count;
    uint64_t derp_heartbeat_age_ms;
    uint64_t control_rx_age_ms;
    uint32_t rc_coord_stream_wd;
    uint32_t rc_coord_transport;
    uint32_t rc_derp_rx_wd;
    uint32_t rc_derp_retry;
} ts_claw_diagnostics_t;

typedef struct {
    uint32_t vpn_ip;
    char hostname[TS_CLAW_PEER_HOSTNAME_LEN];
    bool online;
    bool direct;
    bool is_exit_node;
    uint16_t derp_region;
    char derp_region_name[TS_CLAW_REGION_NAME_LEN];
} ts_claw_peer_t;

esp_err_t ts_claw_get_diagnostics(ts_claw_diagnostics_t *out);
int ts_claw_list_exit_nodes(ts_claw_peer_t *out, size_t capacity);
```

- [ ] **Step 4: Snapshot MicroLink diagnostics on the TS-Claw worker**

Add `TS_EVENT_GET_DIAGNOSTICS` and replace the old raw
`microlink_peer_info_t` Exit Node handoff with `ts_claw_peer_t`. On the worker,
call `microlink_get_diag()`, `microlink_get_derp_rtts()`, region-name lookup,
heartbeat timestamp, control timestamp, and peer snapshots. Convert timestamps
to ages using the event-handling `now_ms()` value. Copy strings before releasing
the synchronous event. Do not return borrowed MicroLink pointers.

- [ ] **Step 5: Run TS-Claw host tests and commit**

Run the Task 3 CMake/CTest commands. Expected: three TS-Claw tests pass.

```bash
git add application/edge_agent/components/ts_claw
git commit -m "feat: expose TS-Claw DERP diagnostics"
```

## Task 4: Add worker-serialized live Exit Node and reconnect operations

**Files:**
- Modify: `application/edge_agent/components/ts_claw/include/ts_claw.h`
- Modify: `application/edge_agent/components/ts_claw/ts_claw.c`
- Create: `application/edge_agent/components/ts_claw/ts_claw_runtime_control.c`
- Create: `application/edge_agent/components/ts_claw/ts_claw_runtime_control.h`
- Create: `application/edge_agent/components/ts_claw/tests/host/test_ts_claw_runtime_control.c`
- Modify: `application/edge_agent/components/ts_claw/tests/host/CMakeLists.txt`

- [ ] **Step 1: Write failing runtime-control state tests**

Use fake operations for retire-probe, destroy, start, wait-connected, and
wait-exit-active. Verify the exact call sequence for setting an Exit Node:

```text
retire_probe -> destroy -> set_desired_ip -> start -> wait_connected -> wait_exit_active
```

Verify clearing stops after `wait_connected` and requires STA egress. Verify a
start, connection, or exit activation failure restores the previous desired IP
and performs one recovery rebuild. Verify a second mutation while one is active
returns `ESP_ERR_INVALID_STATE`.

- [ ] **Step 2: Run TS-Claw host tests and verify failure**

Run the Task 3 CMake/CTest commands.

Expected: compile failure because runtime-control helpers are absent.

- [ ] **Step 3: Add the public runtime APIs**

```c
typedef struct {
    uint32_t selected_exit_node_ip;
    ts_exit_state_t exit_state;
    char egress[16];
    bool rollback_attempted;
    bool rollback_recovered;
} ts_claw_runtime_result_t;

esp_err_t ts_claw_set_exit_node(uint32_t exit_node_ip,
                                uint32_t timeout_ms,
                                ts_claw_runtime_result_t *out);
esp_err_t ts_claw_reconnect(uint32_t timeout_ms);
```

An IP of zero means clear. Add `TS_EVENT_SET_EXIT_NODE` and
`TS_EVENT_RECONNECT`. The queue event carries the requested IP, timeout, and
caller-owned result pointer protected by the existing synchronous reply signal.

- [ ] **Step 4: Implement worker-only lifecycle changes**

Refactor existing destroy/start helpers only enough to let the event handler:

1. snapshot the old desired IP;
2. retire the exit probe;
3. destroy the current MicroLink instance;
4. update the in-memory desired IP;
5. start MicroLink;
6. wait by continuing the worker loop until the success predicate or deadline;
7. recover the old desired IP on failure.

Do not block the worker in a sleep loop. Store a single pending operation in
`s_ts`, advance it from the existing 250 ms worker tick, and signal the caller
when complete. Reject another mutation while pending. Keep read-only snapshots
available from the cached status during the transition.

For reconnect, call `microlink_rebind()` on the worker and wait for connected
state within the supplied timeout. Do not change `s_ts.config`, identity, or
Exit Node choice.

- [ ] **Step 5: Run TS-Claw tests and full firmware compile**

Run the Task 3 CTest commands, then:

```bash
source /Users/sheneyan/esp/esp-idf/export.sh
cd application/edge_agent
idf.py build
```

Expected: TS-Claw host tests pass and the N16R8 firmware links successfully.

- [ ] **Step 6: Commit runtime control**

```bash
git add application/edge_agent/components/ts_claw
git commit -m "feat: switch TS-Claw exit nodes at runtime"
```

## Task 5: Implement the shared application service and rollback transaction

**Files:**
- Create: `application/edge_agent/components/tailscale_service/CMakeLists.txt`
- Create: `application/edge_agent/components/tailscale_service/include/tailscale_service.h`
- Create: `application/edge_agent/components/tailscale_service/tailscale_service.c`
- Create: `application/edge_agent/components/tailscale_service/tests/host/CMakeLists.txt`
- Create: `application/edge_agent/components/tailscale_service/tests/host/test_tailscale_service.c`
- Create: `application/edge_agent/components/tailscale_service/tests/host/stubs/esp_err.h`
- Modify: `application/edge_agent/main/CMakeLists.txt`
- Modify: `application/edge_agent/main/main.c`

- [ ] **Step 1: Write failing service transaction tests**

Build a fake ops table with counters and scripted return values. Test:

- exact IP match;
- case-insensitive exact hostname match;
- unique short-host prefix match;
- ambiguous prefix returns candidates and makes no runtime call;
- unknown, offline, and non-Exit-Node rejection;
- runtime success followed by persistence success;
- runtime failure leaves persistence untouched;
- persistence failure triggers runtime rollback;
- persistence failure plus rollback failure returns the rollback-failed category;
- clear persists an empty string only after STA egress is observed;
- a concurrent mutation returns busy;
- reconnect never calls persistence.

- [ ] **Step 2: Run the service test and verify failure**

```bash
cmake -S application/edge_agent/components/tailscale_service/tests/host \
      -B /tmp/esp-claw-tailscale-service
cmake --build /tmp/esp-claw-tailscale-service -j 4
ctest --test-dir /tmp/esp-claw-tailscale-service --output-on-failure
```

Expected: configure or compile failure because the service does not exist.

- [ ] **Step 3: Define the service interface and error categories**

```c
typedef enum {
    TAILSCALE_SERVICE_OK = 0,
    TAILSCALE_SERVICE_NOT_ENABLED,
    TAILSCALE_SERVICE_NOT_CONNECTED,
    TAILSCALE_SERVICE_INVALID_NODE,
    TAILSCALE_SERVICE_AMBIGUOUS_NODE,
    TAILSCALE_SERVICE_NODE_NOT_FOUND,
    TAILSCALE_SERVICE_NODE_OFFLINE,
    TAILSCALE_SERVICE_NOT_EXIT_NODE,
    TAILSCALE_SERVICE_SWITCH_TIMEOUT,
    TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED,
    TAILSCALE_SERVICE_PERSISTENCE_FAILED,
    TAILSCALE_SERVICE_ROLLBACK_FAILED,
    TAILSCALE_SERVICE_RECONNECT_FAILED,
    TAILSCALE_SERVICE_BUSY,
} tailscale_service_error_t;

typedef struct {
    esp_err_t (*get_diagnostics)(ts_claw_diagnostics_t *out, void *ctx);
    int (*list_exit_nodes)(ts_claw_peer_t *out, size_t capacity, void *ctx);
    esp_err_t (*apply_exit_node)(uint32_t ip, ts_claw_runtime_result_t *out, void *ctx);
    esp_err_t (*rebind)(void *ctx);
    esp_err_t (*load_persisted_exit)(char out[16], void *ctx);
    esp_err_t (*save_persisted_exit)(const char *ip, void *ctx);
    void *ctx;
} tailscale_service_ops_t;

typedef struct tailscale_service *tailscale_service_handle_t;

typedef struct {
    bool ok;
    tailscale_service_error_t error;
    char message[96];
    char selected_ip[16];
    char selected_hostname[96];
    ts_exit_state_t exit_state;
    char egress[16];
    bool persisted;
    bool rollback_attempted;
    bool rollback_recovered;
} tailscale_service_result_t;

esp_err_t tailscale_service_create(const tailscale_service_ops_t *ops,
                                   tailscale_service_handle_t *out);
void tailscale_service_delete(tailscale_service_handle_t service);
esp_err_t tailscale_service_get_diagnostics(tailscale_service_handle_t service,
                                            ts_claw_diagnostics_t *out);
int tailscale_service_list_exit_nodes(tailscale_service_handle_t service,
                                      ts_claw_peer_t *out,
                                      size_t capacity);
esp_err_t tailscale_service_set_exit_node(tailscale_service_handle_t service,
                                          const char *selector,
                                          tailscale_service_result_t *out);
esp_err_t tailscale_service_clear_exit_node(tailscale_service_handle_t service,
                                            tailscale_service_result_t *out);
esp_err_t tailscale_service_reconnect(tailscale_service_handle_t service,
                                      tailscale_service_result_t *out);
const char *tailscale_service_error_name(tailscale_service_error_t error);
```

Give `tailscale_service` its own mutex so only one mutation can run at once.

- [ ] **Step 4: Implement selector and transaction logic**

Resolution order is exact CGNAT IP, case-insensitive full hostname, then unique
case-insensitive short-host prefix. Require online and `is_exit_node`. Snapshot
the persisted old IP, apply runtime, verify result, save the new string, and on
save failure apply the old IP. Record both primary and rollback outcomes.

- [ ] **Step 5: Register the capability provider before `app_claw_start()`**

In `main.c`, create adapters that convert `tailscale_service` structures into
`cap_tailscale` structures. Register this external group after TS-Claw init and
before `app_claw_start()`:

```c
ESP_ERROR_CHECK(cap_tailscale_set_provider(&provider));
ESP_ERROR_CHECK(app_capabilities_register_external_group(
    &(app_capability_external_group_t) {
        .group_id = "cap_tailscale",
        .display_name = "Tailscale",
        .llm_visible_by_default = true,
        .reg = main_register_tailscale_capability,
    }));
```

Persistence adapters load a fresh `app_config_t`, modify only
`tailscale_exit_node`, validate Tailscale config, and call `main_save_config()`.
Never copy or log `tailscale_auth_key` outside the existing config object.

Keep the capability board-scoped by registering the external group only inside
`CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW`. The external group's
`llm_visible_by_default = true` makes it part of the default LLM-visible set
when the user's capability configuration is empty; no global Kconfig option or
board runtime-config rewrite is needed.

- [ ] **Step 6: Run service and baseline tests**

Run service CTest plus:

```bash
for suite in \
  application/edge_agent/components/ts_claw/tests/host \
  application/edge_agent/components/app_config/tests/host \
  application/edge_agent/main/tests/host; do
  name=$(printf '%s' "$suite" | tr '/' '_')
  cmake -S "$suite" -B "/tmp/$name"
  cmake --build "/tmp/$name" -j 4
  ctest --test-dir "/tmp/$name" --output-on-failure
done
```

Expected: all host tests pass.

- [ ] **Step 7: Commit the shared service and provider**

```bash
git add application/edge_agent/components/tailscale_service \
        application/edge_agent/main
git commit -m "feat: connect Tailscale tools to TS-Claw"
```

## Task 6: Add shared live-control HTTP endpoints

**Files:**
- Modify: `application/edge_agent/components/http_server/include/http_server.h`
- Modify: `application/edge_agent/components/http_server/http_server_tailscale_api.c`
- Create: `application/edge_agent/components/http_server/http_server_tailscale_request.c`
- Create: `application/edge_agent/components/http_server/http_server_tailscale_request.h`
- Create: `application/edge_agent/components/http_server/tests/host/CMakeLists.txt`
- Create: `application/edge_agent/components/http_server/tests/host/test_http_server_tailscale_request.c`
- Modify: `application/edge_agent/main/main.c`

- [ ] **Step 1: Add failing handler-level validation cases**

Create a small parser test target using
`$IDF_PATH/components/json/cJSON/cJSON.c`. Cover missing `node`, non-string
`node`, unknown fields, oversized selector, malformed JSON, a valid set body,
an empty clear body, and an empty reconnect body. Keep JSON-body parsing in
`http_server_tailscale_request.c` so the test does not need HTTP socket stubs.

- [ ] **Step 2: Run service tests and verify failure**

Run:

```bash
source /Users/sheneyan/esp/esp-idf/export.sh
cmake -S application/edge_agent/components/http_server/tests/host \
      -B /tmp/esp-claw-http-tailscale-request
cmake --build /tmp/esp-claw-http-tailscale-request -j 4
ctest --test-dir /tmp/esp-claw-http-tailscale-request --output-on-failure
```

Expected: configure or compile failure because the request parser is absent.

- [ ] **Step 3: Extend `http_server_services_t`**

Add callbacks that invoke the same shared service used by `cap_tailscale`:

```c
esp_err_t (*set_tailscale_exit_node)(const char *selector,
                                     http_server_tailscale_operation_t *out);
esp_err_t (*clear_tailscale_exit_node)(http_server_tailscale_operation_t *out);
esp_err_t (*reconnect_tailscale)(http_server_tailscale_operation_t *out);
```

The operation response contains `ok`, error category, message, selected IP and
hostname, exit state, egress, and persisted. It contains no credential fields.

- [ ] **Step 4: Register live endpoints**

Add:

```text
POST /api/tailscale/exit-node   body {"node":"100.104.62.56"}
DELETE /api/tailscale/exit-node
POST /api/tailscale/reconnect   body {}
```

Map validation errors to 400, conflict/busy/offline to 409, unavailable runtime
to 503, and successful operations to 200 JSON. Browser clicks themselves are
the explicit user action, so the HTTP body does not use the model-only
`user_confirmed` field.

- [ ] **Step 5: Wire callbacks and verify build**

Wire the callbacks in `main.c` to `tailscale_service`. Run the HTTP parser and
service tests, then:

```bash
source /Users/sheneyan/esp/esp-idf/export.sh
cd application/edge_agent
idf.py build
```

- [ ] **Step 6: Commit the HTTP integration**

```bash
git add application/edge_agent/components/http_server application/edge_agent/main/main.c
git commit -m "feat: add live Tailscale control API"
```

## Task 7: Render and operate the Exit Node control in the web UI

**Files:**
- Modify: `application/edge_agent/components/http_server/frontend_source/package.json`
- Modify: `application/edge_agent/components/http_server/frontend_source/pnpm-lock.yaml`
- Modify: `application/edge_agent/components/http_server/frontend_source/vite.config.ts`
- Create: `application/edge_agent/components/http_server/frontend_source/src/test/setup.ts`
- Create: `application/edge_agent/components/http_server/frontend_source/src/pages/TailscalePage.test.tsx`
- Modify: `application/edge_agent/components/http_server/frontend_source/src/api/client.ts`
- Modify: `application/edge_agent/components/http_server/frontend_source/src/pages/TailscalePage.tsx`
- Modify: `application/edge_agent/components/http_server/frontend_source/src/i18n/en.ts`
- Modify: `application/edge_agent/components/http_server/frontend_source/src/i18n/zh-cn.ts`

- [ ] **Step 1: Install the focused rendering test stack**

```bash
cd application/edge_agent/components/http_server/frontend_source
pnpm add -D vitest jsdom @solidjs/testing-library @testing-library/jest-dom
```

Add `"test": "vitest run"` to scripts and configure `environment: 'jsdom'`
with `src/test/setup.ts` importing `@testing-library/jest-dom/vitest`.

- [ ] **Step 2: Write failing page tests**

Mock config/status/Exit Node API calls and assert:

```ts
expect(screen.getByLabelText('Exit Node')).toBeInTheDocument();
expect(screen.getByRole('option', { name: /regular Wi-Fi/i })).toBeInTheDocument();
```

Then cover one online node, one disabled offline node, Exit Node API failure with
the select still visible, mutation progress, successful status refresh, and
rollback error text.

- [ ] **Step 3: Run tests and verify failure**

```bash
pnpm test
```

Expected: tests fail because the page is not isolated for deterministic
rendering and live mutation APIs do not exist.

- [ ] **Step 4: Add typed client operations**

```ts
export function setTailscaleExitNode(node: string) {
  return request<TailscaleOperation>(
    '/api/tailscale/exit-node',
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ node }),
    },
    'Failed to switch Exit Node',
  );
}

export function clearTailscaleExitNode() {
  return request<TailscaleOperation>(
    '/api/tailscale/exit-node',
    { method: 'DELETE' },
    'Failed to clear Exit Node',
  );
}
```

Add the reconnect function and richer DERP status types in the same module.

- [ ] **Step 5: Make the control always visible and live**

Keep the `None` option regardless of list state. Render online nodes normally,
offline nodes with `disabled`, and an inline list error without hiding the
select. Disable the control only during a mutation. On selection, call set or
clear immediately, show progress, then reload runtime status and the Tailscale
config group. Do not show the global restart note for an Exit Node-only live
change; preserve it for hostname/auth/login-server/enable/max-peer edits.

- [ ] **Step 6: Run frontend verification**

```bash
pnpm test
pnpm typecheck
pnpm build
```

Expected: all tests pass, TypeScript reports no errors, and Vite produces the
single-file compressed frontend artifact.

- [ ] **Step 7: Commit the web control**

```bash
git add application/edge_agent/components/http_server/frontend_source
git commit -m "feat: switch Tailscale exit nodes from the web UI"
```

## Task 8: Document the agent-aware behavior in English and Chinese

**Files:**
- Modify: `README.md`
- Modify: `README_CN.md`
- Modify: `docs/ESP_CLAW_TS.md`
- Modify: `docs/ESP_CLAW_TS_CN.md`
- Modify: `docs/src/content/docs/en/reference-project/skills-and-capability.mdx`
- Modify: `docs/src/content/docs/zh-cn/reference-project/skills-and-capability.mdx`

- [ ] **Step 1: Add matching English and Chinese sections**

Document:

- the five tool names;
- status and DERP diagnosis examples;
- Tailscale relay diagnostics are not an Internet traceroute;
- mutation requires an explicit current-user request;
- Exit Node switch/clear is immediate and persistent, with rollback on failure;
- hostname, auth, login server, enablement, and identity remain settings-only;
- the web and agent share one live control service.

Add one aligned note to the existing English and Chinese
`skills-and-capability.mdx` pages showing `tailscale_network` as the concrete
Skill-plus-Capability example. Keep headings, tables, code blocks, blank lines,
and line count aligned between languages.

- [ ] **Step 2: Verify documentation contracts**

```bash
for doc in README.md README_CN.md docs/ESP_CLAW_TS.md docs/ESP_CLAW_TS_CN.md; do
  rg -n 'cap_tailscale|tailscale_network|DERP|Exit Node' "$doc"
done
rg -n -i 'auth key|login server|hostname' docs/ESP_CLAW_TS.md docs/ESP_CLAW_TS_CN.md
git diff --check
cd docs
pnpm run check:doc-lines
pnpm run build
cd ..
```

Expected: all four documents contain the capability/skill concepts, both guides
state the settings-only boundary, and diff check is clean.

- [ ] **Step 3: Commit documentation**

```bash
git add README.md README_CN.md docs/ESP_CLAW_TS.md docs/ESP_CLAW_TS_CN.md \
        docs/src/content/docs/en/reference-project/skills-and-capability.mdx \
        docs/src/content/docs/zh-cn/reference-project/skills-and-capability.mdx
git commit -m "docs: explain agent-aware Tailscale controls"
```

## Task 9: Run complete regression, build, flash, and physical acceptance

**Files:**
- Modify only if verification finds a defect in files already listed above.

- [ ] **Step 1: Run every host suite from fresh build directories**

```bash
rm -rf /tmp/esp-claw-cap-tailscale-contract \
       /tmp/esp-claw-tailscale-service \
       /tmp/esp-claw-http-tailscale-request \
       /tmp/esp-claw-ts-claw-host \
       /tmp/application_edge_agent_components_app_config_tests_host \
       /tmp/application_edge_agent_main_tests_host

cmake -S components/claw_capabilities/cap_tailscale/tests/host \
      -B /tmp/esp-claw-cap-tailscale-contract
cmake --build /tmp/esp-claw-cap-tailscale-contract -j 4
ctest --test-dir /tmp/esp-claw-cap-tailscale-contract --output-on-failure

cmake -S application/edge_agent/components/tailscale_service/tests/host \
      -B /tmp/esp-claw-tailscale-service
cmake --build /tmp/esp-claw-tailscale-service -j 4
ctest --test-dir /tmp/esp-claw-tailscale-service --output-on-failure

source /Users/sheneyan/esp/esp-idf/export.sh
cmake -S application/edge_agent/components/http_server/tests/host \
      -B /tmp/esp-claw-http-tailscale-request
cmake --build /tmp/esp-claw-http-tailscale-request -j 4
ctest --test-dir /tmp/esp-claw-http-tailscale-request --output-on-failure

for suite in \
  application/edge_agent/components/ts_claw/tests/host \
  application/edge_agent/components/app_config/tests/host \
  application/edge_agent/main/tests/host; do
  name=$(printf '%s' "$suite" | tr '/' '_')
  cmake -S "$suite" -B "/tmp/$name"
  cmake --build "/tmp/$name" -j 4
  ctest --test-dir "/tmp/$name" --output-on-failure
done
```

Expected: every CTest target passes with zero failures.

- [ ] **Step 2: Run fresh frontend checks**

```bash
cd application/edge_agent/components/http_server/frontend_source
pnpm install --frozen-lockfile
pnpm test
pnpm typecheck
pnpm build
cd ../../../../../..
```

Expected: tests, typecheck, and production build pass.

- [ ] **Step 3: Run the final N16R8 firmware build**

```bash
git submodule update --init --recursive
source /Users/sheneyan/esp/esp-idf/export.sh
cd application/edge_agent
idf.py fullclean
idf.py bmgr -c ./boards -b esp32_s3_n16r8_ts_claw
idf.py build
```

Expected: `Project build complete` and the generated application fits the
configured application partition.

- [ ] **Step 4: Flash the physically connected N16R8**

```bash
port=$(find /dev -maxdepth 1 \( -name 'cu.usbmodem*' -o -name 'cu.wchusbserial*' \) \
       -print | sort | head -1)
test -n "$port"
idf.py -p "$port" flash monitor
```

Exit the monitor after boot, Wi-Fi connection, Tailscale connection, skill
loading, and capability registration are visible without a crash or reset loop.

- [ ] **Step 5: Verify the actual web surface**

Open the device Tailscale page and visibly verify:

- Exit Node select is rendered below login server;
- `None` is present;
- the known online node is present;
- an offline test item, when available, is disabled;
- selection shows progress and reaches active without reboot;
- clearing returns egress to STA;
- status and config survive a normal device restart.

Do not substitute source inspection, bundle string search, or API success for
this browser verification.

- [ ] **Step 6: Verify the agent conversation**

Use the device chat to ask, in order:

```text
你的 Tailscale IP、连接路径和当前 DERP 区域是什么？
列出现在可用的 Exit Node。
切换到 racknerd-4fc9d3f 作为 Exit Node。
取消 Exit Node，恢复普通 Wi-Fi 出口。
Tailscale 好像有点慢，帮我检查一下。
```

Verify the first two calls are read-only, the explicit switch/clear calls work
and persist correctly, and the last problem report diagnoses without mutation.
Check serial logs and status after each mutation.

- [ ] **Step 7: Run repository hygiene checks**

```bash
git diff --check
git status --short
rg -n -i 'tskey-|auth[_ -]?key["=: ]+[A-Za-z0-9]' \
  README.md README_CN.md docs components application \
  -g '!**/build/**' -g '!**/dist/**'
```

Expected: diff check is clean, only intended files are modified, and the secret
scan finds no credential values.

- [ ] **Step 8: Commit any verification-only corrections**

If verification required changes, stage only the affected files and commit:

```bash
git add README.md README_CN.md docs components application
git commit -m "fix: complete Tailscale capability verification"
```

If no correction was required, do not create an empty commit.

- [ ] **Step 9: Review the branch before integration**

```bash
git status --short --branch
git log --oneline master..HEAD
git diff --stat master...HEAD
```

Expected: clean feature branch containing only the planned capability, service,
runtime, web, tests, and documentation changes.
