# TS-Claw Loopback Routing Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore lwIP loopback routing for `127.0.0.0/8` so ESP-IDF HTTP server asynchronous WebSocket work succeeds without changing TS-Claw LAN, tailnet, or Exit Node routing.

**Architecture:** Separate loopback from the STA-bypass policy, classify loopback as `TS_ROUTE_DEFAULT`, and add a defensive loopback delegation at the wrapped lwIP route hook. Cover the pure policy and runtime hook with host tests, then build and validate the exact `esp32_s3_n16r8_ts_claw` image on the connected N16R8.

**Tech Stack:** C11, ESP-IDF v5.5.4, lwIP route hook wrapping, CMake, Ninja, CTest, Node.js WebSocket client, ESP32-S3 N16R8

---

## File Structure

- Modify `application/edge_agent/components/ts_claw/include/ts_claw_policy.h`: expose the loopback predicate.
- Modify `application/edge_agent/components/ts_claw/ts_claw_policy.c`: separate loopback from physical-LAN bypass.
- Modify `application/edge_agent/components/ts_claw/ts_claw_route_hook.c`: delegate loopback to lwIP.
- Modify `application/edge_agent/components/ts_claw/tests/host/test_ts_claw_policy.c`: assert policy boundaries.
- Create `application/edge_agent/components/ts_claw/tests/host/test_ts_claw_route_hook.c`: test runtime routing with fake netifs.
- Create `application/edge_agent/components/ts_claw/tests/host/stubs/freertos/FreeRTOS.h`: host critical-section stubs.
- Create `application/edge_agent/components/ts_claw/tests/host/stubs/lwip/ip4_addr.h`: host IPv4 stubs.
- Create `application/edge_agent/components/ts_claw/tests/host/stubs/lwip/netif.h`: host netif stub.
- Modify `application/edge_agent/components/ts_claw/tests/host/CMakeLists.txt`: register the route-hook test.

### Task 1: Make loopback a first-class policy decision

**Files:**
- Modify: `application/edge_agent/components/ts_claw/include/ts_claw_policy.h`
- Modify: `application/edge_agent/components/ts_claw/ts_claw_policy.c`
- Test: `application/edge_agent/components/ts_claw/tests/host/test_ts_claw_policy.c`

- [ ] **Step 1: Add the predicate declaration and failing loopback expectations**

Add after `ts_route_is_private()` in the header:

```c
bool ts_route_is_loopback(uint32_t host_order_ip);
```

Also replace the stale `TS_ROUTE_DEFAULT` enum comment with:

```c
    /* Delegate special destinations such as loopback to lwIP's native route. */
    TS_ROUTE_DEFAULT,
```

Replace `test_loopback_and_link_local_stay_on_sta()` with:

```c
static void test_loopback_delegates_and_link_local_stays_on_sta(void)
{
    TEST_CHECK(!ts_route_is_loopback(0x7EFFFFFFu));
    TEST_CHECK(ts_route_is_loopback(0x7F000000u));
    TEST_CHECK(ts_route_is_loopback(0x7FFFFFFFu));
    TEST_CHECK(!ts_route_is_loopback(0x80000000u));

    TEST_CHECK(ts_route_classify(0x7EFFFFFFu, true) == TS_ROUTE_WG);
    TEST_CHECK(ts_route_classify(0x7F000000u, true) == TS_ROUTE_DEFAULT);
    TEST_CHECK(ts_route_classify(0x7FFFFFFFu, true) == TS_ROUTE_DEFAULT);
    TEST_CHECK(ts_route_classify(0x80000000u, true) == TS_ROUTE_WG);

    TEST_CHECK(ts_route_classify(0xA9FDFFFFu, true) == TS_ROUTE_WG);
    TEST_CHECK(ts_route_classify(0xA9FE0000u, true) == TS_ROUTE_STA);
    TEST_CHECK(ts_route_classify(0xA9FEFFFFu, true) == TS_ROUTE_STA);
    TEST_CHECK(ts_route_classify(0xA9FF0000u, true) == TS_ROUTE_WG);
}
```

Replace its call in `main()` with:

```c
    test_loopback_delegates_and_link_local_stays_on_sta();
```

- [ ] **Step 2: Verify the policy test is red**

Run:

```bash
cmake -S application/edge_agent/components/ts_claw/tests/host \
  -B /tmp/ts-claw-host-tests -G Ninja
cmake --build /tmp/ts-claw-host-tests
```

Expected: link failure with undefined reference to `ts_route_is_loopback`.

- [ ] **Step 3: Implement the minimal policy**

Add after `ts_route_is_private()`:

```c
bool ts_route_is_loopback(uint32_t host_order_ip)
{
    return address_matches(host_order_ip, 0x7F000000u, 0xFF000000u);
}
```

Replace `ts_route_is_local_bypass()` with:

```c
bool ts_route_is_local_bypass(uint32_t host_order_ip)
{
    return ts_route_is_private(host_order_ip) ||
           address_matches(host_order_ip, 0xA9FE0000u, 0xFFFF0000u);
}
```

Replace the opening exclusion in `ts_route_is_public_unicast()` with:

```c
    if (ts_route_is_cgnat(host_order_ip) ||
        ts_route_is_loopback(host_order_ip) ||
        ts_route_is_local_bypass(host_order_ip)) {
        return false;
    }
```

Add before the CGNAT branch in `ts_route_classify()`:

```c
    if (ts_route_is_loopback(host_order_ip)) {
        return TS_ROUTE_DEFAULT;
    }
```

- [ ] **Step 4: Verify the policy test is green**

Run:

```bash
cmake --build /tmp/ts-claw-host-tests
ctest --test-dir /tmp/ts-claw-host-tests --output-on-failure -R '^ts_claw_policy$'
```

Expected: `ts_claw_policy` passes.

- [ ] **Step 5: Commit**

```bash
git add application/edge_agent/components/ts_claw/include/ts_claw_policy.h \
  application/edge_agent/components/ts_claw/ts_claw_policy.c \
  application/edge_agent/components/ts_claw/tests/host/test_ts_claw_policy.c
git commit -m "fix: delegate TS-Claw loopback routes"
```

### Task 2: Defensively delegate loopback in the runtime hook

**Files:**
- Create: `application/edge_agent/components/ts_claw/tests/host/stubs/freertos/FreeRTOS.h`
- Create: `application/edge_agent/components/ts_claw/tests/host/stubs/lwip/ip4_addr.h`
- Create: `application/edge_agent/components/ts_claw/tests/host/stubs/lwip/netif.h`
- Create: `application/edge_agent/components/ts_claw/tests/host/test_ts_claw_route_hook.c`
- Modify: `application/edge_agent/components/ts_claw/tests/host/CMakeLists.txt`
- Modify: `application/edge_agent/components/ts_claw/ts_claw_route_hook.c`

- [ ] **Step 1: Add exact host stubs**

Create `stubs/freertos/FreeRTOS.h`:

```c
#pragma once
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
```

Create `stubs/lwip/ip4_addr.h`:

```c
#pragma once
#include <stdint.h>
typedef struct { uint32_t addr; } ip4_addr_t;
#define ip4_addr_get_u32(address) ((address)->addr)
static inline uint32_t lwip_ntohl(uint32_t value)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(value);
#else
    return value;
#endif
}
static inline uint32_t lwip_htonl(uint32_t value)
{
    return lwip_ntohl(value);
}
```

Create `stubs/lwip/netif.h`:

```c
#pragma once
struct netif { int marker; };
```

- [ ] **Step 2: Add the failing runtime-hook test**

Create `test_ts_claw_route_hook.c`:

```c
#include "ts_claw_route_hook.h"
#include "ts_claw_policy.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static struct netif s_default_netif = {.marker = 1};
static struct netif s_sta_netif = {.marker = 2};
static struct netif s_wg_netif = {.marker = 3};
static unsigned int s_real_hook_calls;

bool ts_route_is_loopback(uint32_t host_order_ip)
{
    return (host_order_ip & 0xFF000000u) == 0x7F000000u;
}

bool ts_route_is_cgnat(uint32_t host_order_ip)
{
    return (host_order_ip & 0xFFC00000u) == 0x64400000u;
}

bool ts_route_is_local_bypass(uint32_t host_order_ip)
{
    const bool private_lan =
        (host_order_ip & 0xFF000000u) == 0x0A000000u ||
        (host_order_ip & 0xFFF00000u) == 0xAC100000u ||
        (host_order_ip & 0xFFFF0000u) == 0xC0A80000u;
    const bool link_local =
        (host_order_ip & 0xFFFF0000u) == 0xA9FE0000u;

    /* Deliberately simulate future helper drift: the runtime guard must win. */
    return private_lan || link_local || ts_route_is_loopback(host_order_ip);
}

bool ts_route_is_public_unicast(uint32_t host_order_ip)
{
    return host_order_ip == 0x08080808u;
}

struct netif *__real_ip4_route_src_hook(const ip4_addr_t *src,
                                        const ip4_addr_t *dest)
{
    (void)src;
    (void)dest;
    s_real_hook_calls++;
    return &s_default_netif;
}

struct netif *__wrap_ip4_route_src_hook(const ip4_addr_t *src,
                                        const ip4_addr_t *dest);

static ip4_addr_t network_address(uint32_t host_order_ip)
{
    return (ip4_addr_t) {.addr = lwip_htonl(host_order_ip)};
}

static void test_loopback_delegates_to_lwip(void)
{
    const ip4_addr_t loopback = network_address(0x7F000001u);
    ts_claw_route_hook_reset();
    ts_claw_route_hook_set_netifs(&s_sta_netif, &s_wg_netif);
    s_real_hook_calls = 0u;
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &loopback) == &s_default_netif);
    TEST_CHECK(s_real_hook_calls == 1u);
}

static void test_existing_route_targets_are_preserved(void)
{
    const ip4_addr_t private_lan = network_address(0xC0A80132u);
    const ip4_addr_t cgnat_peer = network_address(0x6457967Au);
    const ip4_addr_t public_ip = network_address(0x08080808u);

    ts_claw_route_hook_reset();
    ts_claw_route_hook_set_netifs(&s_sta_netif, &s_wg_netif);
    s_real_hook_calls = 0u;
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &private_lan) == &s_sta_netif);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &cgnat_peer) == &s_wg_netif);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &public_ip) == &s_default_netif);
    TEST_CHECK(s_real_hook_calls == 1u);

    ts_claw_route_hook_set_tunnel_available(true);
    ts_claw_route_hook_set_upstream_pinned(true);
    ts_claw_route_hook_set_exit_active(true);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &public_ip) == &s_wg_netif);
    TEST_CHECK(s_real_hook_calls == 1u);
}

static void test_null_destination_delegates_to_lwip(void)
{
    s_real_hook_calls = 0u;
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, NULL) == &s_default_netif);
    TEST_CHECK(s_real_hook_calls == 1u);
}

int main(void)
{
    test_loopback_delegates_to_lwip();
    test_existing_route_targets_are_preserved();
    test_null_destination_delegates_to_lwip();
    puts("ts_claw_route_hook: all tests passed");
    return 0;
}
```

Add to `CMakeLists.txt` before the existing policy `add_test`:

```cmake
add_executable(test_ts_claw_route_hook
    test_ts_claw_route_hook.c
    ../../ts_claw_route_hook.c
)
target_include_directories(test_ts_claw_route_hook PRIVATE
    stubs
    ../..
    ../../include
)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_ts_claw_route_hook PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME ts_claw_route_hook COMMAND test_ts_claw_route_hook)
```

- [ ] **Step 3: Verify the runtime-hook test is red**

```bash
cmake -S application/edge_agent/components/ts_claw/tests/host \
  -B /tmp/ts-claw-host-tests -G Ninja
cmake --build /tmp/ts-claw-host-tests
ctest --test-dir /tmp/ts-claw-host-tests --output-on-failure -R '^ts_claw_route_hook$'
```

Expected: `ts_claw_route_hook` fails because the deliberately regressed
`ts_route_is_local_bypass()` stub makes the unguarded hook select the fake STA
netif. This test proves that the explicit runtime guard exists independently of
the pure policy implementation.

- [ ] **Step 4: Add the explicit runtime guard**

After calculating `destination` and before reading route state:

```c
    if (ts_route_is_loopback(destination)) {
        return __real_ip4_route_src_hook(src, dest);
    }
```

The opening must become:

```c
struct netif *__wrap_ip4_route_src_hook(const ip4_addr_t *src,
                                        const ip4_addr_t *dest)
{
    if (dest == NULL) {
        return __real_ip4_route_src_hook(src, dest);
    }

    const uint32_t destination = lwip_ntohl(ip4_addr_get_u32(dest));
    if (ts_route_is_loopback(destination)) {
        return __real_ip4_route_src_hook(src, dest);
    }

    const ts_claw_route_state_t state = ts_claw_route_hook_get_state();
```

- [ ] **Step 5: Run all host tests**

```bash
cmake --build /tmp/ts-claw-host-tests
ctest --test-dir /tmp/ts-claw-host-tests --output-on-failure
```

Expected: both `ts_claw_policy` and `ts_claw_route_hook` pass.

- [ ] **Step 6: Commit**

```bash
git add application/edge_agent/components/ts_claw/ts_claw_route_hook.c \
  application/edge_agent/components/ts_claw/tests/host/CMakeLists.txt \
  application/edge_agent/components/ts_claw/tests/host/test_ts_claw_route_hook.c \
  application/edge_agent/components/ts_claw/tests/host/stubs
git commit -m "test: cover TS-Claw runtime route selection"
```

### Task 3: Build the exact N16R8 image

**Files:**
- Verify: `application/edge_agent/build/edge_agent.bin`
- Verify: `application/edge_agent/build/bootloader/bootloader.bin`
- Verify: `application/edge_agent/build/partition_table/partition-table.bin`

- [ ] **Step 1: Activate the pinned toolchain and regenerate the board configuration**

```bash
source /Users/sheneyan/esp/esp-idf-v5.5.4/export.sh
cd application/edge_agent
idf.py --version
idf.py bmgr -c ./boards -b esp32_s3_n16r8_ts_claw
```

Expected: ESP-IDF v5.5.4 and board `esp32_s3_n16r8_ts_claw`.

- [ ] **Step 2: Build**

```bash
idf.py build
```

Expected: successful build and an application image that fits its partition.

- [ ] **Step 3: Re-run host tests and inspect generated drift**

From the repository root:

```bash
ctest --test-dir /tmp/ts-claw-host-tests --output-on-failure
git status --short
```

Expected: both tests pass and no unexpected generated or source changes appear.

### Task 4: Flash once and validate the physical path

**Files:**
- No source changes.
- Target: ESP32-S3 N16R8 currently at `/dev/cu.usbmodem5C930635061`.

- [ ] **Step 1: Confirm the serial target**

```bash
ls -1 /dev/cu.usbmodem*
```

Expected: `/dev/cu.usbmodem5C930635061`. If it changed, use only the single newly enumerated ESP32-S3 port.

- [ ] **Step 2: Flash once and monitor**

From `application/edge_agent` with ESP-IDF v5.5.4 active:

```bash
idf.py -p /dev/cu.usbmodem5C930635061 flash monitor
```

Expected: saved Wi-Fi/Tailscale settings remain, the device returns to `192.168.1.50`, and there is no recurring `httpd_queue_work: failed to queue work` storm.

- [ ] **Step 3: Verify LAN and tailnet HTTP paths without proxying**

```bash
curl --noproxy '*' -fsS http://192.168.1.50/api/webim/status
curl --noproxy '*' -fsS http://100.87.150.122/api/webim/status
```

Expected: successful status JSON from both addresses and no 502 response.

- [ ] **Step 4: Run a controlled WebSocket reply test**

```bash
node <<'NODE'
const ws = new WebSocket('ws://192.168.1.50/ws/webim');
const timeout = setTimeout(() => {
  console.error('Timed out waiting for assistant reply');
  process.exit(1);
}, 120000);

ws.addEventListener('open', async () => {
  const response = await fetch('http://192.168.1.50/api/webim/send', {
    method: 'POST',
    headers: {'content-type': 'application/json'},
    body: JSON.stringify({
      chat_id: 'loopback-fix-test',
      text: '只回复 LOOPBACK_OK',
      files: [],
    }),
  });
  if (!response.ok) throw new Error('send failed: ' + response.status);
});

ws.addEventListener('message', event => {
  const message = JSON.parse(String(event.data));
  console.log(message);
  if (message.role === 'assistant' && message.text.includes('LOOPBACK_OK')) {
    clearTimeout(timeout);
    ws.close();
    process.exit(0);
  }
});

ws.addEventListener('error', error => {
  clearTimeout(timeout);
  console.error(error);
  process.exit(1);
});
NODE
```

Expected: an immediate assistant-status event followed by a reply containing `LOOPBACK_OK`; serial logs show queued broadcasts without queue-work failures.

- [ ] **Step 5: Verify leaf-node and Exit Node behavior**

```bash
tailscale status | rg 'esp-claw|100\.87\.150\.122'
tailscale ping esp-claw
```

Expected: `esp-claw` responds at `100.87.150.122` and still advertises no subnet routes. Repeat the WebSocket test once with the configured Exit Node available and once with only that Exit Node offline; the second run must fall back to ordinary Wi-Fi.

- [ ] **Step 6: Record final evidence**

```bash
git status --short --branch
git log --oneline -4
```

Expected: clean worktree, the design and two implementation commits visible, host tests and firmware build passing, and physical Web IM delivering `LOOPBACK_OK`.
