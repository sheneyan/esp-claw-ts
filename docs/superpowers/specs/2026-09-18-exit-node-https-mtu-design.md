# Exit Node HTTPS MTU Compatibility Design

## Goal

Make HTTPS and LLM traffic work through the ESP32-S3 N16R8 TS-Claw Exit Node by permanently applying the experimentally verified combination of WireGuard MTU 1280 and TCP MSS 1240.

## Evidence

With the existing WireGuard MTU 1420 and TCP MSS 1440, a generic HTTPS request through the RackNerd Exit Node timed out after about 20 seconds while HTTP requests succeeded. A temporary firmware using MTU 1280 and MSS 1240 returned HTTP 204 from the same HTTPS endpoint in about 3.3 seconds. The experiment changed both values together, so this design intentionally keeps the verified pair rather than spending more hardware cycles isolating either value.

## Scope

- Apply MTU 1280 only to the TS-Claw WireGuard netif.
- Set TCP MSS 1240 only in the `esp32_s3_n16r8_ts_claw` board defaults.
- Keep the MicroLink submodule unchanged.
- Preserve the existing DNS-over-STA compatibility policy and Exit Node routing behavior.
- Do not add a runtime setting, web control, model tool, or automatic MTU probing.

## Implementation

TS-Claw will own a small MTU policy helper. When the borrowed MicroLink WireGuard netif becomes available, the worker applies MTU 1280 through the lwIP TCP/IP thread, then verifies the effective value before treating the interface as ready for Exit Node use. Repeated application must be idempotent. A changed or recreated WireGuard netif receives the policy again.

The N16R8 board file `sdkconfig.defaults.board` will set `CONFIG_LWIP_TCP_MSS=1240`. Generated `sdkconfig` files are not source artifacts and will not be committed.

If the TCP/IP callback fails or the effective MTU is not 1280, TS-Claw must fail closed for Exit Node activation and expose an operational error; ordinary STA operation remains available.

## Tests

Test-first host coverage will verify:

- the desired MTU is 1280;
- applying the policy changes a WireGuard netif from 1420 to 1280;
- repeated application is idempotent;
- null netif and callback/application failure are rejected;
- a recreated netif receives the policy;
- the N16R8 board defaults contain TCP MSS 1240.

Only focused TS-Claw tests will run during implementation. One exact-board build is allowed after the focused tests pass.

## Physical Acceptance

Use the previously identified N16R8 only. Build and flash once, then verify:

1. STA boot and Tailscale connection remain healthy.
2. RackNerd Exit Node becomes active.
3. `https://connectivitycheck.gstatic.com/generate_204` returns HTTP 204.
4. A fresh MiMo chat receives a final response.
5. Clearing the Exit Node returns immediately to STA.

At most one retry is allowed for clear transient transport noise. Any new failure is recorded and ends the run; it does not trigger another implementation loop.

## Non-goals

- Finding the maximum possible MTU.
- Separating whether MTU 1280 or MSS 1240 alone is sufficient.
- Generalizing the setting to every ESP-Claw board.
- Modifying or upstreaming MicroLink.
