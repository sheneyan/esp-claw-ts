# Exit Node HTTPS MTU Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Permanently apply the hardware-proven WireGuard MTU 1280 and TCP MSS 1240 combination on the ESP32-S3 N16R8 TS-Claw board so HTTPS and LLM traffic work through an Exit Node.

**Architecture:** A project-owned TS-Claw helper applies MTU 1280 to the borrowed WireGuard lwIP netif through `tcpip_callback_with_block`; the Exit Node probe cannot start until this succeeds. The N16R8 board defaults set TCP MSS 1240. MicroLink remains unchanged.

**Tech Stack:** ESP-IDF 5.5.4, C11, lwIP, CMake/CTest, ESP Board Manager, ESP32-S3 N16R8.

---

### Task 1: Add the project-owned WireGuard MTU policy test-first

**Files:**
- Create: `application/edge_agent/components/ts_claw/ts_claw_mtu_policy.h`
- Create: `application/edge_agent/components/ts_claw/ts_claw_mtu_policy.c`
- Create: `application/edge_agent/components/ts_claw/tests/host/test_ts_claw_mtu_policy.c`
- Create: `application/edge_agent/components/ts_claw/tests/host/stubs/lwip/err.h`
- Create: `application/edge_agent/components/ts_claw/tests/host/stubs/lwip/tcpip.h`
- Modify: `application/edge_agent/components/ts_claw/tests/host/stubs/lwip/netif.h`
- Modify: `application/edge_agent/components/ts_claw/tests/host/CMakeLists.txt`

- [ ] **Step 1: Write the failing host test**

Add `uint16_t mtu` to the host `struct netif` stub. Add these lwIP host stubs:

```c
/* stubs/lwip/err.h */
#pragma once
typedef int err_t;
#define ERR_OK 0
#define ERR_MEM (-1)
```

```c
/* stubs/lwip/tcpip.h */
#pragma once
#include "lwip/err.h"
typedef unsigned char u8_t;
typedef void (*tcpip_callback_fn)(void *ctx);
err_t tcpip_callback_with_block(tcpip_callback_fn fn, void *ctx, u8_t block);
```

Create the complete policy test:

```c
#include <stdbool.h>
#include <stdio.h>

#include "lwip/err.h"
#include "lwip/tcpip.h"
#include "ts_claw_mtu_policy.h"

static int s_failures;
#define TEST_CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
        s_failures++; \
    } \
} while (0)

static err_t s_callback_result = ERR_OK;
static bool s_run_callback = true;

err_t tcpip_callback_with_block(tcpip_callback_fn fn, void *ctx, u8_t block)
{
    TEST_CHECK(block == 1u);
    if (s_callback_result == ERR_OK && s_run_callback) {
        fn(ctx);
    }
    return s_callback_result;
}

static void test_applies_verified_mtu(void)
{
    struct netif first = {.mtu = 1420u};
    struct netif second = {.mtu = 1420u};

    TEST_CHECK(ts_claw_apply_wg_mtu(&first) == ESP_OK);
    TEST_CHECK(first.mtu == TS_CLAW_WG_MTU);
    TEST_CHECK(ts_claw_apply_wg_mtu(&first) == ESP_OK);
    TEST_CHECK(ts_claw_apply_wg_mtu(&second) == ESP_OK);
    TEST_CHECK(second.mtu == TS_CLAW_WG_MTU);
}

static void test_rejects_invalid_or_failed_apply(void)
{
    s_callback_result = ERR_OK;
    s_run_callback = true;
    TEST_CHECK(ts_claw_apply_wg_mtu(NULL) == ESP_ERR_INVALID_ARG);
    struct netif netif = {.mtu = 1420u};
    s_callback_result = ERR_MEM;
    TEST_CHECK(ts_claw_apply_wg_mtu(&netif) == ESP_FAIL);
    TEST_CHECK(netif.mtu == 1420u);
    s_callback_result = ERR_OK;
    s_run_callback = false;
    TEST_CHECK(ts_claw_apply_wg_mtu(&netif) == ESP_FAIL);
}

int main(void)
{
    test_applies_verified_mtu();
    test_rejects_invalid_or_failed_apply();
    return s_failures == 0 ? 0 : 1;
}
```

Register the test with the production source:

```cmake
add_executable(test_ts_claw_mtu_policy
    test_ts_claw_mtu_policy.c
    ../../ts_claw_mtu_policy.c)
target_include_directories(test_ts_claw_mtu_policy PRIVATE
    stubs
    ../..
    ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_ts_claw_mtu_policy PRIVATE
        -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME ts_claw_mtu_policy COMMAND test_ts_claw_mtu_policy)
```

- [ ] **Step 2: Run the focused test and verify RED**

```bash
cmake -S application/edge_agent/components/ts_claw/tests/host \
      -B /tmp/esp-claw-mtu-red
cmake --build /tmp/esp-claw-mtu-red --target test_ts_claw_mtu_policy -j 4
```

Expected: compilation fails because `ts_claw_mtu_policy.h` and `ts_claw_apply_wg_mtu` do not exist.

- [ ] **Step 3: Implement the minimal synchronous policy helper**

`ts_claw_mtu_policy.h`:

```c
#pragma once

#include "esp_err.h"
#include "lwip/netif.h"

#define TS_CLAW_WG_MTU 1280u

esp_err_t ts_claw_apply_wg_mtu(struct netif *wg_netif);
```

`ts_claw_mtu_policy.c`:

```c
#include "ts_claw_mtu_policy.h"

#include "lwip/err.h"
#include "lwip/tcpip.h"

typedef struct {
    struct netif *netif;
} ts_claw_mtu_apply_t;

static void apply_mtu_on_tcpip(void *ctx)
{
    ts_claw_mtu_apply_t *apply = ctx;
    apply->netif->mtu = TS_CLAW_WG_MTU;
}

esp_err_t ts_claw_apply_wg_mtu(struct netif *wg_netif)
{
    if (wg_netif == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ts_claw_mtu_apply_t apply = {.netif = wg_netif};
    if (tcpip_callback_with_block(apply_mtu_on_tcpip, &apply, 1u) != ERR_OK) {
        return ESP_FAIL;
    }
    return wg_netif->mtu == TS_CLAW_WG_MTU ? ESP_OK : ESP_FAIL;
}
```

The stack object contains one pointer and remains below the repository's 128-byte task-stack limit. The callback is blocking, so the stack context remains valid until completion.

- [ ] **Step 4: Run the focused test and verify GREEN**

```bash
cmake --build /tmp/esp-claw-mtu-red --target test_ts_claw_mtu_policy -j 4
ctest --test-dir /tmp/esp-claw-mtu-red -R ts_claw_mtu_policy --output-on-failure
```

Expected: one MTU policy test passes.

### Task 2: Gate Exit Node probing on MTU application and set board MSS

**Files:**
- Modify: `application/edge_agent/components/ts_claw/CMakeLists.txt`
- Modify: `application/edge_agent/components/ts_claw/ts_claw.c`
- Modify: `application/edge_agent/boards/local/esp32_s3_n16r8_ts_claw/sdkconfig.defaults.board`
- Create: `application/edge_agent/components/ts_claw/tests/host/verify_n16r8_mss.cmake`
- Modify: `application/edge_agent/components/ts_claw/tests/host/CMakeLists.txt`

- [ ] **Step 1: Write the failing board-default check**

`verify_n16r8_mss.cmake`:

```cmake
file(READ "${BOARD_DEFAULTS}" board_defaults)
string(FIND "${board_defaults}" "CONFIG_LWIP_TCP_MSS=1240" mss_index)
if(mss_index EQUAL -1)
    message(FATAL_ERROR "N16R8 board defaults must set CONFIG_LWIP_TCP_MSS=1240")
endif()
```

Register it as:

```cmake
add_test(NAME ts_claw_n16r8_mss
    COMMAND ${CMAKE_COMMAND}
        -DBOARD_DEFAULTS=${CMAKE_CURRENT_LIST_DIR}/../../../../boards/local/esp32_s3_n16r8_ts_claw/sdkconfig.defaults.board
        -P ${CMAKE_CURRENT_LIST_DIR}/verify_n16r8_mss.cmake)
```

- [ ] **Step 2: Run the board check and verify RED**

```bash
ctest --test-dir /tmp/esp-claw-mtu-red -R ts_claw_n16r8_mss --output-on-failure
```

Expected: failure stating that `CONFIG_LWIP_TCP_MSS=1240` is absent.

- [ ] **Step 3: Add the board-specific MSS value**

Append to `sdkconfig.defaults.board`:

```text
# Exit Node HTTPS compatibility validated with WireGuard MTU 1280.
CONFIG_LWIP_TCP_MSS=1240
```

- [ ] **Step 4: Gate probe startup on the MTU policy**

Add `ts_claw_mtu_policy.c` to the TS-Claw component sources. At the beginning of `worker_start_exit_probe(struct netif *wg_netif)`, before allocating or starting an ESP ping session, apply and verify the policy:

```c
esp_err_t mtu_err = ts_claw_apply_wg_mtu(wg_netif);
if (mtu_err != ESP_OK) {
    set_last_error("wireguard MTU apply failed");
    return mtu_err;
}
```

Include `ts_claw_mtu_policy.h`. This location reapplies the value for every newly started or recreated Exit Node probe, including when lwIP reuses the same netif address. No runtime setting or cached pointer is introduced.

- [ ] **Step 5: Run focused verification**

```bash
rm -rf /tmp/esp-claw-mtu-green
cmake -S application/edge_agent/components/ts_claw/tests/host \
      -B /tmp/esp-claw-mtu-green
cmake --build /tmp/esp-claw-mtu-green -j 4
ctest --test-dir /tmp/esp-claw-mtu-green --output-on-failure
git diff --check
```

Expected: all eight TS-Claw host tests pass and the diff is clean.

- [ ] **Step 6: Commit the implementation**

```bash
git add \
  application/edge_agent/components/ts_claw/CMakeLists.txt \
  application/edge_agent/components/ts_claw/ts_claw.c \
  application/edge_agent/components/ts_claw/ts_claw_mtu_policy.c \
  application/edge_agent/components/ts_claw/ts_claw_mtu_policy.h \
  application/edge_agent/components/ts_claw/tests/host \
  application/edge_agent/boards/local/esp32_s3_n16r8_ts_claw/sdkconfig.defaults.board
git commit -m "fix: set Exit Node MTU for HTTPS"
```

### Task 3: Build once, flash once, and run the fixed acceptance matrix

**Files:**
- Modify only if the fixed acceptance matrix discovers a defect in Task 1 or Task 2 files.

- [ ] **Step 1: Build the exact board once**

```bash
source /Users/sheneyan/esp/esp-idf/export.sh
cd application/edge_agent
idf.py bmgr -c ./boards -b esp32_s3_n16r8_ts_claw
idf.py build
```

Expected: build completes, generated sdkconfig reports `CONFIG_LWIP_TCP_MSS=1240`, and the application fits its partition.

- [ ] **Step 2: Verify the exact physical target and flash once**

Confirm `/dev/cu.usbmodem5C930635061` still reports ESP32-S3 rev 0.2, 16 MB flash, 8 MB PSRAM, and the known MAC before flashing. Do not touch `/dev/cu.usbmodem1401`.

```bash
idf.py -p /dev/cu.usbmodem5C930635061 flash
```

- [ ] **Step 3: Run the bounded acceptance matrix**

Perform each once, with at most one retry only for clear transient transport noise:

1. Confirm STA boot and Tailscale connection.
2. Activate RackNerd Exit Node `100.104.62.56`.
3. Request `https://connectivitycheck.gstatic.com/generate_204`; expect HTTP 204 within 20 seconds.
4. Start a fresh MiMo chat; expect a final model response rather than `ESP_ERR_HTTP_CONNECT`.
5. Clear Exit Node; expect `exit_state=disabled`, `egress=sta`, and no reboot.

Any new failure ends the run and is reported without another code change loop.

- [ ] **Step 4: Run final hygiene checks**

```bash
git diff --check
git status --short --branch
rg -n 'WIREGUARDIF_MTU \(1280\)' application/edge_agent/third_party/microlink && exit 1 || true
```

Expected: worktree is clean and the MicroLink submodule contains no persistent MTU edit.
