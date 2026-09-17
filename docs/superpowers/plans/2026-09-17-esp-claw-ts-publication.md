# ESP-Claw TS Publication Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish the tested ESP-Claw TS source and bilingual documentation to `sheneyan/esp-claw-ts` without changing the upstream Apache-2.0 license or overstating hardware and network support.

**Architecture:** Keep the ESP-Claw upstream README content as the project foundation, prepend a concise fork-specific entry section in both languages, and place operational details in two standalone guides. Preserve Espressif as a separate `upstream` remote, make the user's fork `origin`, and publish by fast-forward only after identity, tests, build, license, submodule, and public-page checks pass.

**Tech Stack:** Markdown, Git, GitHub, ESP-IDF 5.5.4, ESP Board Manager, CMake/Ninja/CTest, MicroLink git submodule

---

## File Structure

- Modify `README.md`: English ESP-Claw TS identity, tested target, quick-start link, fork disclaimer, and upstream attribution.
- Modify `README_CN.md`: Chinese equivalent of the fork introduction and support boundary.
- Create `docs/ESP_CLAW_TS.md`: complete English build, provisioning, tailnet, validation, hardware, limitation, security, and troubleshooting guide.
- Create `docs/ESP_CLAW_TS_CN.md`: equivalent Chinese operational guide.
- Preserve `LICENSE`: top-level Apache License 2.0 text must remain byte-identical to `upstream/master`.
- Preserve `.gitmodules`: keep the tested MicroLink repository and gitlink.
- Preserve `docs/superpowers/specs/2026-09-17-esp-claw-ts-publication-design.md`: approved publication specification.

### Task 1: Normalize the Unpublished Commit Identity

**Files:**
- Modify: repository-local Git configuration and local commits after `upstream/master`

- [ ] **Step 1: Preserve the official remote and fetch the fork without changing history**

Run:

```bash
git remote rename origin upstream
git remote add origin https://github.com/sheneyan/esp-claw-ts.git
git fetch --all --prune
git merge-base --is-ancestor origin/master HEAD
git rev-parse upstream/master
git rev-list --count upstream/master..HEAD
```

Expected: `upstream` points to Espressif, `origin` points to the user's fork,
the ancestry check exits `0`, and all unpublished commits are descendants of
`upstream/master`.

- [ ] **Step 2: Configure the requested repository-local identity**

Run:

```bash
git config user.name "Yiyan Shen"
git config user.email "sheneyan@gmail.com"
git config --local --get user.name
git config --local --get user.email
```

Expected: `Yiyan Shen` and `sheneyan@gmail.com`.

- [ ] **Step 3: Create a recoverable backup reference before rewriting local history**

Run:

```bash
git branch backup/pre-publication-local-email HEAD
```

Expected: a local backup branch points to the pre-rewrite history. It is not pushed.

- [ ] **Step 4: Rewrite only unpublished commits with the requested author and committer identity**

Run:

```bash
git rebase --exec 'git commit --amend --no-edit --reset-author' upstream/master
```

Expected: the rebase completes without conflict; upstream commits remain untouched.

- [ ] **Step 5: Verify every unpublished commit identity**

Run:

```bash
git log upstream/master..HEAD --format='%an <%ae>|%cn <%ce>' | sort -u
```

Expected exactly:

```text
Yiyan Shen <sheneyan@gmail.com>|Yiyan Shen <sheneyan@gmail.com>
```

### Task 2: Add the English and Chinese Repository Entrypoints

**Files:**
- Modify: `README.md`
- Modify: `README_CN.md`

- [ ] **Step 1: Add the English fork notice immediately after the centered hero block**

Add this structure before the upstream ESP-Claw introduction:

```markdown
> [!IMPORTANT]
> **ESP-Claw TS** is an unofficial community fork of ESP-Claw with optional
> Tailscale-compatible private networking. The currently tested target is an
> **ESP32-S3 N16R8** board. See the [ESP-Claw TS guide](./docs/ESP_CLAW_TS.md)
> before building or flashing.

### What this fork adds

- Tailnet access to the ESP-Claw web interface and WebSocket chat
- Optional exit-node use for device-originated traffic
- Wi-Fi fallback when the selected exit node is unavailable
- A dedicated `esp32_s3_n16r8_ts_claw` board profile

This project is not affiliated with or endorsed by Espressif or Tailscale.
The original ESP-Claw project is maintained at
[`espressif/esp-claw`](https://github.com/espressif/esp-claw).
```

- [ ] **Step 2: Add the equivalent Chinese fork notice**

Add this structure at the matching location in `README_CN.md`:

```markdown
> [!IMPORTANT]
> **ESP-Claw TS** 是 ESP-Claw 的非官方社区分支，增加了可选的 Tailscale
> 兼容私网连接能力。目前唯一经过实机验证的目标是 **ESP32-S3 N16R8**。
> 编译或烧录前请阅读 [ESP-Claw TS 中文指南](./docs/ESP_CLAW_TS_CN.md)。

### 本分支增加的能力

- 通过 tailnet 访问 ESP-Claw 网页和 WebSocket 聊天
- 设备自身的出站流量可选使用 exit node
- 所选 exit node 不可用时回退普通 Wi-Fi
- 专用的 `esp32_s3_n16r8_ts_claw` 板型配置

本项目与乐鑫、Tailscale 均无隶属或背书关系。原始 ESP-Claw 项目由
[`espressif/esp-claw`](https://github.com/espressif/esp-claw) 维护。
```

- [ ] **Step 3: Check the README diff for scope and rendering hazards**

Run:

```bash
git diff --check -- README.md README_CN.md
rg -n "ESP-Claw TS|ESP32-S3 N16R8|ESP_CLAW_TS" README.md README_CN.md
```

Expected: no whitespace errors; both READMEs contain the name, tested target,
guide link, and disclaimer.

- [ ] **Step 4: Commit the bilingual entrypoints**

Run:

```bash
git add README.md README_CN.md
git commit -m "docs: introduce ESP-Claw TS fork"
```

### Task 3: Write the Complete Bilingual Operation Guides

**Files:**
- Create: `docs/ESP_CLAW_TS.md`
- Create: `docs/ESP_CLAW_TS_CN.md`

- [ ] **Step 1: Write the English guide with this exact section contract**

The guide must contain these headings and facts:

```markdown
# ESP-Claw TS Guide

## Scope
## Tested Configuration
## Prerequisites
## Clone with MicroLink
## Build and Flash
## First Boot and Wi-Fi Provisioning
## Join the Tailnet
## Verify Local and Tailnet Access
## Exit Node Behavior
## Hardware Recommendations
## Known Issues and Troubleshooting
## Security Notes
## Upstream and License
```

Use these tested build commands:

```bash
git clone --recurse-submodules https://github.com/sheneyan/esp-claw-ts.git
cd esp-claw-ts/application/edge_agent
source /Users/sheneyan/esp/esp-idf-v5.5.4/export.sh
idf.py bmgr -c ./boards -b esp32_s3_n16r8_ts_claw
idf.py build
idf.py flash monitor
```

Also document the equivalent recovery for a clone without submodules:

```bash
git submodule update --init --recursive
```

The guide must distinguish local Wi-Fi access from tailnet access, require the
device to be authorized in the tailnet, explain direct versus DERP paths, and
state that tailnet ACL/grant policy still applies.

- [ ] **Step 2: Include the support table in the English guide**

Use the approved statuses:

```markdown
| Hardware | Status |
| --- | --- |
| ESP32-S3 N16R8 | Tested and recommended |
| ESP32-S3 N8R8 | Adaptation required; current 16 MB layout does not fit directly |
| ESP32-C5 | Unverified candidate; port and runtime validation required |
| ESP32-P4 | Not recommended for this single-board Wi-Fi design |
| ESP32-C3/C6/H2 and ESP32-WROOM-32 boards | Not supported by this release |
```

- [ ] **Step 3: Include actionable English troubleshooting**

Cover all of the following with commands or observable checks:

```text
- Proxy bypass: 100.64.0.0/10 and the device LAN subnet
- HTTP 502 in a proxied browser while mobile access works
- Saved Tailscale configuration versus active/connected status
- Direct path versus DERP latency using `tailscale ping <device-name>`
- Tailnet authorization and ACL/grant checks
- Full flashing may replace /fatfs runtime files
- NVS persistence is separate from /fatfs and is not a universal backup guarantee
- Exit-node fallback affects device-originated outbound traffic only
- Auth keys and device identity material must never be committed
```

- [ ] **Step 4: Write the Chinese guide with the same operational contract**

Use these matching headings:

```markdown
# ESP-Claw TS 中文指南

## 能力边界
## 已验证配置
## 前置条件
## 克隆并初始化 MicroLink
## 编译与烧录
## 首次启动与 Wi-Fi 配网
## 加入 Tailnet
## 验证局域网与 Tailnet 访问
## Exit Node 行为
## 硬件建议
## 已知问题与排障
## 安全说明
## 上游项目与许可证
```

Commands, hardware statuses, proxy ranges, persistence warnings, and support
boundaries must match the English guide.

- [ ] **Step 5: Run a bilingual contract check**

Run:

```bash
for file in docs/ESP_CLAW_TS.md docs/ESP_CLAW_TS_CN.md; do
  rg -n "esp32_s3_n16r8_ts_claw|ESP-IDF 5\.5\.4|100\.64\.0\.0/10|/fatfs|N16R8|N8R8|ESP32-C5|exit node|Exit Node|DERP|Apache" "$file"
done
git diff --check -- docs/ESP_CLAW_TS.md docs/ESP_CLAW_TS_CN.md
```

Expected: every required fact appears in both guides and no whitespace errors
are reported.

- [ ] **Step 6: Commit the operation guides**

Run:

```bash
git add docs/ESP_CLAW_TS.md docs/ESP_CLAW_TS_CN.md
git commit -m "docs: add ESP-Claw TS setup guides"
```

### Task 4: Verify Documentation, License, Tests, and Firmware

**Files:**
- Verify: `README.md`
- Verify: `README_CN.md`
- Verify: `docs/ESP_CLAW_TS.md`
- Verify: `docs/ESP_CLAW_TS_CN.md`
- Verify unchanged: `LICENSE`
- Test: `application/edge_agent/components/ts_claw/tests/host`
- Test: `application/edge_agent/components/app_config/tests/host`
- Test: `application/edge_agent/main/tests/host`
- Test: `components/common/wifi_manager/tests/host`

- [ ] **Step 1: Verify links, terminology, and absence of unrelated positioning**

Run:

```bash
test -f docs/ESP_CLAW_TS.md
test -f docs/ESP_CLAW_TS_CN.md
rg -n "ESP-Claw TS" README.md README_CN.md docs/ESP_CLAW_TS.md docs/ESP_CLAW_TS_CN.md
if rg -ni "KVM|HDMI capture|keyboard control" README.md README_CN.md docs/ESP_CLAW_TS.md docs/ESP_CLAW_TS_CN.md; then exit 1; fi
git diff --check upstream/master...HEAD
```

Expected: guide files exist, all entrypoints use the public name, no KVM
positioning appears, and the diff has no whitespace errors.

- [ ] **Step 2: Prove the top-level license is unchanged**

Run:

```bash
test "$(git hash-object LICENSE)" = "$(git show upstream/master:LICENSE | git hash-object --stdin)"
```

Expected: exit `0`.

- [ ] **Step 3: Run all four host-test projects in fresh temporary directories**

Activate ESP-IDF 5.5.4, configure each directory with CMake/Ninja, build it,
and run CTest:

```bash
source /Users/sheneyan/esp/esp-idf-v5.5.4/export.sh
test_root="$(mktemp -d /tmp/esp-claw-ts-publish-tests.XXXXXX)"
cmake -S application/edge_agent/components/ts_claw/tests/host -B "$test_root/ts_claw" -G Ninja
cmake --build "$test_root/ts_claw"
ctest --test-dir "$test_root/ts_claw" --output-on-failure
cmake -S application/edge_agent/components/app_config/tests/host -B "$test_root/app_config" -G Ninja
cmake --build "$test_root/app_config"
ctest --test-dir "$test_root/app_config" --output-on-failure
cmake -S application/edge_agent/main/tests/host -B "$test_root/main" -G Ninja
cmake --build "$test_root/main"
ctest --test-dir "$test_root/main" --output-on-failure
cmake -S components/common/wifi_manager/tests/host -B "$test_root/wifi_manager" -G Ninja
cmake --build "$test_root/wifi_manager"
ctest --test-dir "$test_root/wifi_manager" --output-on-failure
```

Expected: five tests pass with zero failures.

- [ ] **Step 4: Build the tested N16R8 firmware**

Run from `application/edge_agent`:

```bash
source /Users/sheneyan/esp/esp-idf-v5.5.4/export.sh
idf.py bmgr -c ./boards -b esp32_s3_n16r8_ts_claw
idf.py build
```

Expected: `Project build complete` and no tracked-file changes.

- [ ] **Step 5: Verify a clean recursive clone locally**

Run from the repository root:

```bash
clone_root="$(mktemp -d /tmp/esp-claw-ts-clone.XXXXXX)"
git clone --recurse-submodules . "$clone_root/esp-claw-ts"
git -C "$clone_root/esp-claw-ts" submodule status --recursive
test -f "$clone_root/esp-claw-ts/application/edge_agent/third_party/microlink/CMakeLists.txt"
```

Expected: the MicroLink gitlink resolves at the committed revision.

- [ ] **Step 6: Confirm the publication tree is clean**

Run:

```bash
git status --short --branch
git log upstream/master..HEAD --format='%an <%ae>|%cn <%ce>' | sort -u
```

Expected: no uncommitted files and only the requested Gmail identity for local
publication commits.

### Task 5: Verify Remotes and Publish by Fast-Forward

**Files:**
- Modify: repository-local Git remote configuration

- [ ] **Step 1: Verify the remote roles configured before the identity rewrite**

Run:

```bash
git remote -v
```

Expected: `origin` points to `sheneyan/esp-claw-ts` and `upstream` points to
`espressif/esp-claw` for fetch and push.

- [ ] **Step 2: Prove the fork update is still a fast-forward**

Run:

```bash
git merge-base --is-ancestor origin/master HEAD
git rev-list --left-right --count origin/master...HEAD
```

Expected: ancestry exits `0`; the left count is `0` and the right count is the
number of unpublished commits.

- [ ] **Step 3: Push without force and set tracking**

Run:

```bash
git push --set-upstream origin master
```

Expected: Git reports a normal fast-forward update of `master`; no force option
is used.

### Task 6: Verify the Public Repository

**Files:**
- Verify remote: `https://github.com/sheneyan/esp-claw-ts`

- [ ] **Step 1: Verify the public branch and commit identity through Git**

Run:

```bash
git ls-remote --symref origin HEAD
git ls-remote origin refs/heads/master
git rev-parse HEAD
git log origin/master -1 --format='%H%n%an <%ae>%n%s'
```

Expected: remote HEAD targets `refs/heads/master`, remote `master` equals local
`HEAD`, and the latest identity uses `sheneyan@gmail.com`.

- [ ] **Step 2: Inspect GitHub rendering and repository metadata**

Open `https://github.com/sheneyan/esp-claw-ts` and verify:

```text
- ESP-Claw TS notice renders at the top of README.md
- README_CN.md and both detailed guides open successfully
- GitHub detects Apache-2.0
- the MicroLink submodule link resolves
- the default branch is master
- no credential, auth key, or device identity is visible
```

- [ ] **Step 3: Remove the local backup only after public verification succeeds**

Run:

```bash
git branch -D backup/pre-publication-local-email
git status --short --branch
```

Expected: the backup branch is deleted safely and `master` is clean and tracks
`origin/master`.
