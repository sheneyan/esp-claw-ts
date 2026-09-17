# ESP-Claw TS Publication Design

## Objective

Publish the current TS-enabled ESP-Claw work to
`https://github.com/sheneyan/esp-claw-ts` as **ESP-Claw TS**, a bilingual,
unofficial community fork of ESP-Claw with optional Tailscale-compatible
private networking.

The first release is source-first. It documents only capabilities implemented
and verified on the current ESP32-S3 N16R8 prototype; it does not add a binary
release, hosted service, or new runtime feature.

## Name and Positioning

The public name is **ESP-Claw TS** and the repository name remains
`esp-claw-ts`. Internal identifiers such as `ts_claw` and
`esp32_s3_n16r8_ts_claw` remain unchanged.

English positioning:

> ESP-Claw TS — ESP-Claw with optional Tailscale-compatible private networking.

Chinese positioning:

> ESP-Claw TS——集成可选 Tailscale 兼容私网连接能力的 ESP-Claw 社区分支。

The documentation must state that this is an unofficial community fork and is
not affiliated with or endorsed by Espressif or Tailscale. It must not imply
that the project is an official Tailscale client or that every upstream
Tailscale feature is supported.

## Publication Approach

Preserve the upstream ESP-Claw documentation and add a prominent ESP-Claw TS
introduction rather than replacing the project history. Provide equivalent
English and Chinese entry points:

- `README.md`: English overview, tested-hardware summary, quick-start link, and
  upstream attribution.
- `README_CN.md`: matching Chinese overview, tested-hardware summary,
  quick-start link, and upstream attribution.
- `docs/ESP_CLAW_TS.md`: complete English setup, operation, limitations, and
  troubleshooting guide.
- `docs/ESP_CLAW_TS_CN.md`: equivalent Chinese guide.

The two guides must communicate the same requirements and support boundaries.
They do not need to be sentence-for-sentence translations.

## Documented Capability Boundary

The first public documentation may claim only the following:

- ESP-Claw runs on the ESP32-S3 N16R8 target.
- The device can join a compatible tailnet through the bundled MicroLink
  integration.
- The ESP-Claw web interface and local WebSocket chat can be reached using the
  device's tailnet address when policy and client routing allow it.
- The device can optionally use a tailnet exit node for its own outbound
  traffic.
- When the selected exit node is unavailable, device-originated traffic falls
  back to the normal Wi-Fi uplink under the implemented policy.

The documentation must not market the first release as a general-purpose
subnet router. Unverified MicroLink or upstream Tailscale features are described
as unsupported or experimental rather than inferred from the dependency.

## Quick Start

The guides will cover this reproducible path:

1. Clone the repository with submodules, or initialize submodules after a
   normal clone.
2. Activate ESP-IDF 5.5.4.
3. Select `esp32_s3_n16r8_ts_claw` with ESP Board Manager.
4. Build and flash the `application/edge_agent` project.
5. Configure Wi-Fi through the provisioning interface.
6. Configure and authorize the Tailscale-compatible connection.
7. Verify local access, tailnet status, tailnet reachability, and WebSocket chat.

Commands must use repository-relative paths and must be checked in a clean
clone before publication.

## Hardware Recommendations

Hardware guidance uses explicit support states rather than family-level
claims:

| Hardware | Publication status | Rationale |
| --- | --- | --- |
| ESP32-S3 N16R8 | Tested and recommended | The current board profile, flash layout, PSRAM configuration, build, and physical runtime path were verified on this target. |
| ESP32-S3 N8R8 | Adaptation required | Compute and PSRAM are plausible, but the current 16 MB flash layout and board profile do not directly fit an 8 MB device. |
| ESP32-C5 | Unverified candidate | Wireless features are attractive, but the application, dependencies, board profile, USB behavior, memory budget, and runtime have not been ported and verified. |
| ESP32-P4 | Not recommended for the minimal design | It requires an external networking companion and defeats the single-board Wi-Fi objective. |
| Common ESP32-C3, ESP32-C6, ESP32-H2, and ESP32-WROOM-32 boards | Not supported by this release | Typical memory, PSRAM, connectivity, target, or board-profile constraints differ from the verified S3 configuration. |

The documentation must not describe untested chips as compatible merely
because ESP-IDF or a dependency supports the chip family.

## Known Issues and Troubleshooting

The bilingual guides will include:

- Browser or system proxies must bypass the tailnet range (`100.64.0.0/10`)
  and the device's local network; otherwise the proxy may return errors such as
  HTTP 502 even while the device is healthy.
- DERP-relayed connections can have materially higher and more variable latency
  than direct peer-to-peer paths.
- Authentication and selected network-setting changes may require a reboot;
  the guide will distinguish saved configuration from an active connection.
- Full partition flashing may replace files stored in `/fatfs`; users should
  back up mutable runtime files before reflashing. Wi-Fi and service settings
  stored in NVS are a separate persistence boundary and must not be presented
  as a guarantee for every flashing command.
- Exit-node fallback describes device-originated outbound routing only. The
  guide will state the limits of the physical verification performed.
- Web access over a tailnet remains subject to tailnet ACL/grant policy and
  authorization of the device.
- Users must protect auth keys and must not commit credentials, exported
  configurations, or device identity material.

## Licensing and Attribution

Keep the repository's Apache License 2.0 `LICENSE` unchanged. Preserve existing
copyright notices and upstream attribution. Add fork attribution and links in
the READMEs without claiming ownership of upstream ESP-Claw work.

Third-party components retain their own licenses. In particular, inclusion as
a Git submodule or dependency does not relicense MicroLink or any other
third-party code under the repository's top-level license.

## Git Remote and Branch Model

- Change `origin` to `https://github.com/sheneyan/esp-claw-ts.git`.
- Add `upstream` for `https://github.com/espressif/esp-claw.git`.
- Publish the current local `master` to the fork's `master` branch.
- Do not force-push. The fork is currently at the upstream baseline, so the
  publication should be a fast-forward update.
- Keep the MicroLink gitlink at the exact tested commit and verify that a fresh
  recursive clone resolves it.

## Verification and Acceptance

Before pushing:

1. Confirm both READMEs identify ESP-Claw TS and link to their corresponding
   detailed guide.
2. Compare the English and Chinese guides for equivalent commands, support
   status, limitations, and safety notes.
3. Confirm `LICENSE` is unchanged from the upstream baseline.
4. Run all existing host test suites for TS-Claw, application configuration,
   provisioning-button policy, and Wi-Fi manager policy.
5. Build `application/edge_agent` for `esp32_s3_n16r8_ts_claw` with ESP-IDF
   5.5.4.
6. Validate submodule initialization from a clean clone or equivalent isolated
   checkout.
7. Confirm the push is a fast-forward and inspect the public GitHub repository
   after publication for the default branch, README rendering, license, and
   submodule target.

Publication is complete only when the local verification succeeds and the
public repository reflects the intended commit. No GitHub Release or firmware
binary is part of this first publication.
