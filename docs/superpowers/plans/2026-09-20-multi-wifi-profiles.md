# Multi-Wi-Fi Profiles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retain up to five Wi-Fi networks, reconnect in fixed priority order, and allow an explicit web-triggered switch without changing Tailscale settings.

**Architecture:** A dedicated profile store keeps credentials separate from legacy `app_config_t` fields and HTTP config serialization. A pure selector is host-tested; `wifi_manager` owns the ESP-IDF scan/connect lifecycle; the web UI talks to a dedicated, password-safe Wi-Fi profile API.

**Tech Stack:** ESP-IDF 5.5.4, C, NVS through `settings_store`, ESP Wi-Fi, ESP HTTP Server/cJSON, SolidJS/TypeScript/Vitest, CMake/CTest.

---

## File map

| Path | Responsibility |
| --- | --- |
| `components/common/wifi_profiles/` | Bounded profile types, validation, NVS persistence, legacy migration. |
| `components/common/wifi_manager/wifi_manager_profile_policy.c` | Platform-free selection and no-roam logic. |
| `components/common/wifi_manager/wifi_manager.c` | Scan, bounded connection attempt, and fallback lifecycle. |
| `application/edge_agent/main/wifi_profile_runtime.c` | Bridges persisted profiles to Wi-Fi manager and services. |
| `application/edge_agent/components/http_server/http_server_wifi_profiles_api.c` | Password-safe HTTP profile endpoints. |
| `application/edge_agent/components/http_server/frontend_source/src/pages/BasicPage.tsx` | Profile editor and manual-switch control. |

### Task 1: Add a profile store and migration

**Files:**
- Create: `components/common/wifi_profiles/{CMakeLists.txt,idf_component.yml,wifi_profiles.c,include/wifi_profiles.h}`
- Create: `components/common/wifi_profiles/tests/host/{CMakeLists.txt,test_wifi_profiles.c}`
- Modify: `application/edge_agent/main/idf_component.yml`

- [ ] **Step 1: Write a failing host test defining the bounded public shape.**

  ```c
  #define WIFI_PROFILES_MAX_COUNT 5u
  typedef struct { char ssid[33]; char password[65]; } wifi_profile_t;
  typedef struct { wifi_profile_t entries[WIFI_PROFILES_MAX_COUNT]; } wifi_profiles_t;
  assert(wifi_profiles_validate(&profiles, &message) == ESP_OK);
  assert(wifi_profiles_validate(&duplicate_ssids, &message) == ESP_ERR_INVALID_ARG);
  assert(wifi_profiles_validate(&bad_password, &message) == ESP_ERR_INVALID_ARG);
  ```

- [ ] **Step 2: Confirm the test is red.**

  Run: `cmake -S components/common/wifi_profiles/tests/host -B /tmp/esp-claw-wifi-profiles`

  Expected: CMake fails because the profile component does not exist.

- [ ] **Step 3: Implement fixed-key persistence and one-time migration.**

  Use NVS keys `wprof0_ssid` through `wprof4_ssid`, `wprof0_pwd` through
  `wprof4_pwd`, plus marker `wifi_prof_v1`; all are under the 15-character
  `settings_store` key limit. `wifi_profiles_migrate_legacy()` copies nonempty
  legacy `wifi_ssid`/`wifi_password` to slot zero only if the marker is absent,
  then writes the marker. `wifi_profiles_save()` validates first and writes all
  ten values with `settings_store_set_strings_batch()`.

- [ ] **Step 4: Run tests.**

  ```bash
  cmake -S components/common/wifi_profiles/tests/host -B /tmp/esp-claw-wifi-profiles
  cmake --build /tmp/esp-claw-wifi-profiles
  ctest --test-dir /tmp/esp-claw-wifi-profiles --output-on-failure
  ```

  Expected: validation, save/load, migration-once, duplicate, and empty-slot tests pass.

- [ ] **Step 5: Commit.**

  ```bash
  git add components/common/wifi_profiles application/edge_agent/main/idf_component.yml
  git commit -m "feat: persist wifi profiles"
  ```

### Task 2: Add deterministic selection policy

**Files:**
- Create: `components/common/wifi_manager/include/wifi_manager_profile_policy.h`
- Create: `components/common/wifi_manager/wifi_manager_profile_policy.c`
- Create: `components/common/wifi_manager/tests/host/test_wifi_manager_profile_policy.c`
- Modify: `components/common/wifi_manager/{CMakeLists.txt,tests/host/CMakeLists.txt}`

- [ ] **Step 1: Write failing policy tests.**

  ```c
  assert(wifi_manager_profile_pick_next(&profiles, visible, 2, 0) == 1);
  assert(wifi_manager_profile_pick_next(&profiles, visible, 2, 2) == -1);
  assert(!wifi_manager_profile_should_roam(true, 0, 1));
  ```

  Cover visible profile ordering, no visible profiles, duplicate SSID handling,
  and the rule that a healthy STA never triggers background roaming.

- [ ] **Step 2: Confirm red, then implement platform-free policy.**

  ```c
  int wifi_manager_profile_pick_next(const wifi_profiles_t *profiles,
                                     const wifi_manager_scan_record_t *visible,
                                     size_t visible_count, size_t start_after);
  bool wifi_manager_profile_should_roam(bool sta_connected,
                                        int active_profile, int candidate_profile);
  ```

  `pick_next` compares SSIDs byte-for-byte and returns the lowest matching
  profile index at/after `start_after`; `should_roam` returns false while
  `sta_connected` is true.

- [ ] **Step 3: Run the Wi-Fi manager host suite.**

  ```bash
  cmake -S components/common/wifi_manager/tests/host -B /tmp/esp-claw-wifi-manager
  cmake --build /tmp/esp-claw-wifi-manager
  ctest --test-dir /tmp/esp-claw-wifi-manager --output-on-failure
  git add components/common/wifi_manager
  git commit -m "feat: select saved wifi profiles by priority"
  ```

  Expected: existing AP-policy and new profile-policy tests pass.

### Task 3: Integrate bounded runtime attempts

**Files:**
- Create: `application/edge_agent/main/{wifi_profile_runtime.c,wifi_profile_runtime.h}`
- Create: `application/edge_agent/main/tests/host/test_wifi_profile_runtime.c`
- Modify: `components/common/wifi_manager/{wifi_manager.c,include/wifi_manager.h}`
- Modify: `application/edge_agent/main/{CMakeLists.txt,main.c,tests/host/CMakeLists.txt}`

- [ ] **Step 1: Write failing adapter tests.**

  Stub the profile store and Wi-Fi manager. Prove migration precedes the first
  start, five slots are supplied to `wifi_manager_start`, `connect_now(3)`
  rejects an empty slot, and an explicit selection never mutates Tailscale:

  ```c
  CHECK(wifi_profile_runtime_start(&runtime) == ESP_OK);
  CHECK(s_start_profile_count == WIFI_PROFILES_MAX_COUNT);
  CHECK(wifi_profile_runtime_connect_now(&runtime, 3) == ESP_ERR_NOT_FOUND);
  ```

- [ ] **Step 2: Confirm red, then implement the runtime bridge.**

  `wifi_profile_runtime_start()` loads/migrates profiles and builds
  `wifi_manager_config_t` using `profiles`, `profile_count`, and all existing
  AP fields unchanged. Replace the single `wifi_ssid` construction in `main.c`.
  Keep `on_wifi_state_changed` as the sole TS-Claw notification path.

- [ ] **Step 3: Extend `wifi_manager` with one-at-a-time profile attempts.**

  On startup and `WIFI_EVENT_STA_DISCONNECTED`, scan once, choose the next
  visible profile, apply only that profile to `wifi_config_t`, call
  `esp_wifi_connect()`, and arm `WIFI_PROFILE_ATTEMPT_TIMEOUT_MS`. On timeout
  or disconnect, move to the next profile exactly once. After all profiles are
  exhausted, leave/reopen the provisioning AP and delay before another scan
  round. Stop the attempt timer on `IP_EVENT_STA_GOT_IP`; never scan or
  disconnect while an IP lease is healthy. Expose only `active_profile_index`
  and `active_profile_ssid` in status.

- [ ] **Step 4: Run focused tests and the target build.**

  ```bash
  cmake -S application/edge_agent/main/tests/host -B /tmp/esp-claw-main-host
  cmake --build /tmp/esp-claw-main-host
  ctest --test-dir /tmp/esp-claw-main-host --output-on-failure
  . /Users/sheneyan/esp/esp-idf-v5.5.4/export.sh
  idf.py gen-bmgr-config -b esp32_s3_n16r8_ts_claw -c boards/local
  idf.py build
  ```

  Expected: tests pass and ESP-IDF 5.5.4 builds the N16R8 application. Do not use `idf.py flash`.

- [ ] **Step 5: Commit.**

  ```bash
  git add components/common/wifi_manager application/edge_agent/main
  git commit -m "feat: reconnect through saved wifi profiles"
  ```

### Task 4: Add password-safe HTTP endpoints and manual switch

**Files:**
- Create: `application/edge_agent/components/http_server/http_server_wifi_profiles_api.c`
- Create: `application/edge_agent/components/http_server/tests/host/test_http_server_wifi_profiles_handlers.c`
- Modify: `application/edge_agent/components/http_server/{CMakeLists.txt,http_server_core.c,http_server_priv.h,include/http_server.h,tests/host/CMakeLists.txt}`
- Modify: `application/edge_agent/main/main.c`

- [ ] **Step 1: Write failing handler tests.**

  Test `GET /api/wifi/profiles` returns five entries with `ssid`, `configured`,
  `active`, and `password_set`, never actual password bytes. Test `PUT` rejects
  duplicates and a sixth profile. Test `POST /api/wifi/profiles/connect` needs
  `{ "index": 0..4 }` and invokes only the runtime adapter.

- [ ] **Step 2: Confirm red, then add services and routes.**

  Add callbacks `get_wifi_profiles`, `save_wifi_profiles`, and
  `connect_wifi_profile` to `http_server_services_t`. Register:

  ```c
  { .uri = "/api/wifi/profiles", .method = HTTP_GET, .handler = wifi_profiles_get_handler },
  { .uri = "/api/wifi/profiles", .method = HTTP_PUT, .handler = wifi_profiles_put_handler },
  { .uri = "/api/wifi/profiles/connect", .method = HTTP_POST, .handler = wifi_profiles_connect_handler },
  ```

  PUT accepts `{ "profiles": [{ "ssid": "...", "password": "..." }] }`.
  Empty/omitted password preserves an existing same-SSID password; explicit
  `{ "clear_password": true }` makes an open network. Connect returns accepted,
  not a false claim that DHCP or Tailnet already succeeded.

- [ ] **Step 3: Run HTTP host tests and commit.**

  ```bash
  cmake -S application/edge_agent/components/http_server/tests/host -B /tmp/esp-claw-http-host
  cmake --build /tmp/esp-claw-http-host
  ctest --test-dir /tmp/esp-claw-http-host --output-on-failure
  git add application/edge_agent/components/http_server application/edge_agent/main/main.c
  git commit -m "feat: manage wifi profiles over http"
  ```

### Task 5: Replace single-STA web form with profile editor

**Files:**
- Create: `application/edge_agent/components/http_server/frontend_source/src/pages/BasicPage.test.tsx`
- Modify: `application/edge_agent/components/http_server/frontend_source/src/{api/client.ts,pages/BasicPage.tsx,i18n/en.ts,i18n/zh-cn.ts}`

- [ ] **Step 1: Write failing UI tests.**

  Mock `fetchWifiProfiles`, `saveWifiProfiles`, and `connectWifiProfile`.
  Assert at most five rows, Up/Down changes request order, a returned
  `password_set` state never fills a password field, and `Connect now` asks for
  confirmation before posting one index.

- [ ] **Step 2: Confirm red, then add typed client functions.**

  ```ts
  export function fetchWifiProfiles(): Promise<WifiProfileSummary[]>;
  export function saveWifiProfiles(profiles: WifiProfileInput[]): Promise<void>;
  export function connectWifiProfile(index: number): Promise<{ accepted: boolean }>;
  ```

  Keep these out of `AppConfig` and `GROUP_FIELDS`; they are a separate
  credential-safe resource.

- [ ] **Step 3: Implement the UI.**

  Keep AP and timezone controls unchanged. Add SSID, blank-password-retains,
  Remove, Up, Down, and Connect-now controls for each profile. Confirm that a
  manual connection drops the current browser session. Use bilingual labels;
  after acceptance, reload status instead of claiming connection success.

- [ ] **Step 4: Verify and commit.**

  ```bash
  pnpm --dir application/edge_agent/components/http_server/frontend_source test -- --run src/pages/BasicPage.test.tsx src/api/client.test.ts
  pnpm --dir application/edge_agent/components/http_server/frontend_source typecheck
  pnpm --dir application/edge_agent/components/http_server/frontend_source build
  git add application/edge_agent/components/http_server/frontend_source
  git commit -m "feat: edit saved wifi profiles in web ui"
  ```

### Task 6: Document and physically verify safely

**Files:**
- Modify: `README.md`
- Modify: `README_zh.md`

- [ ] **Step 1: Add bilingual user guidance.**

  State the five-profile limit, fixed priority order, no healthy-session roam,
  manual-switch interruption, fallback provisioning AP, password redaction, and
  that unchanged Tailscale identity reconnects after any profile gains internet.

- [ ] **Step 2: Run final source verification once.**

  Re-run Task 1–5 test commands, then:

  ```bash
  . /Users/sheneyan/esp/esp-idf-v5.5.4/export.sh
  idf.py gen-bmgr-config -b esp32_s3_n16r8_ts_claw -c boards/local
  idf.py build
  git diff --check
  ```

- [ ] **Step 3: Flash only with a verified target and explicit approval.**

  Verify port `/dev/cu.usbmodem5C930635061` and MAC `a0:85:e3:e0:07:a8`.
  Then flash only the application partition:

  ```bash
  esptool.py --chip esp32s3 --port /dev/cu.usbmodem5C930635061 --baud 460800 \
    write_flash 0x20000 build/edge_agent.bin
  ```

  Never run `idf.py flash`, `erase_flash`, or write `system.bin`, `storage.bin`, NVS, partition table, or OTA data.

- [ ] **Step 4: Execute two-network acceptance.**

  Save A then B; boot with A unavailable/B visible; verify B DHCP and Tailnet
  recovery. Restore A during a healthy B session and verify no auto-handoff.
  Use Connect now for A and verify expected web disconnection, A DHCP, and the
  same Tailnet identity. Make both unavailable and verify AP `192.168.237.1`
  remains reachable with saved profiles intact.

- [ ] **Step 5: Commit documentation.**

  ```bash
  git add README.md README_zh.md
  git commit -m "docs: explain saved wifi profiles"
  ```
