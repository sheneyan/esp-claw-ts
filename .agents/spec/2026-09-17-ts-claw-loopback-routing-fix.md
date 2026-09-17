# TS-Claw IPv4 Loopback Routing Fix

## Status

Approved in conversation on 2026-09-17. This specification covers only the
route-hook defect that prevents ESP-IDF HTTP server asynchronous work and Web
IM replies after MicroLink starts.

## Problem

TS-Claw wraps lwIP's IPv4 source-route hook to select the WireGuard netif for
tailnet CGNAT destinations, the STA netif for local destinations, and the
WireGuard netif for public traffic while an Exit Node is active.

The current local-bypass classification combines three address classes:

- RFC1918 private destinations;
- IPv4 link-local destinations (`169.254.0.0/16`);
- IPv4 loopback destinations (`127.0.0.0/8`).

The route hook forces every local-bypass destination onto the STA netif. That
is valid for RFC1918 and link-local traffic, but invalid for loopback traffic.
ESP-IDF's HTTP server uses a UDP socket on `127.0.0.1` to wake its server task
for asynchronous work. Once the TS-Claw route hook is active, those datagrams
are sent toward STA instead of lwIP's loopback path. `httpd_queue_work()` then
fails or its queued WebSocket broadcast never runs.

Physical reproduction established that the configured MiMo backend completes
successfully and returns the expected text, while Web IM logs two connected
clients and attempts a broadcast that neither client receives. Repeated
`httpd_queue_work: failed to queue work` warnings follow. This isolates the
fault to local asynchronous HTTP dispatch rather than LLM configuration,
Wi-Fi, browser proxying, or MiMo response parsing.

## Required Behavior

IPv4 routing decisions must obey these rules:

- `100.64.0.0/10` selects the WireGuard netif when it is available.
- RFC1918 destinations select the physical STA netif.
- `169.254.0.0/16` selects the physical STA netif.
- `127.0.0.0/8` is never forced to STA or WireGuard; it delegates to lwIP's
  native route hook so the loopback netif remains authoritative.
- Public unicast destinations select WireGuard only while the configured Exit
  Node is active; otherwise they retain the existing STA/default behavior.
- Other non-global special-purpose ranges continue to delegate to lwIP.

The fix must not change peer routing, Exit Node activation/fallback thresholds,
MicroLink socket pinning, Wi-Fi provisioning, LLM configuration, or Web IM's
message protocol.

## Design

Add an explicit loopback predicate to the pure TS-Claw route policy. Loopback
is separate from the set of destinations that must be forced onto the physical
STA netif.

`ts_route_classify()` will return `TS_ROUTE_DEFAULT` for all of
`127.0.0.0/8`. The wrapped lwIP route hook will explicitly delegate loopback
destinations to `__real_ip4_route_src_hook()` before considering the STA bypass
or WireGuard branches. The explicit route-hook guard protects the runtime even
if policy helpers are changed later.

RFC1918 and IPv4 link-local addresses remain in the STA-bypass predicate. No
new task, queue, socket, retry, or configuration switch is introduced.

## Testing

Implementation follows test-first development:

1. Change the host policy test to require `TS_ROUTE_DEFAULT` at both loopback
   boundaries and verify addresses immediately outside the range keep their
   existing classifications. Confirm this test fails against the current
   implementation.
2. Add boundary assertions for the new `ts_route_is_loopback()` predicate and
   retain the existing RFC1918, link-local, CGNAT, and active Exit Node
   assertions. The runtime hook must call the real lwIP hook when that predicate
   is true.
3. Implement the minimal policy and hook changes and run all TS-Claw host tests.
4. Build the complete `esp32_s3_n16r8_ts_claw` image with ESP-IDF v5.5.4.
5. Flash the connected N16R8 once and verify on physical hardware:
   - no recurring `httpd_queue_work` failure storm;
   - Web IM receives the immediate working message and final MiMo reply;
   - LAN Web Console remains reachable at `192.168.1.50`;
   - tailnet Web Console remains reachable at `100.87.150.122`;
   - Tailscale remains a leaf node with no subnet routes;
   - configured Exit Node behavior and Wi-Fi fallback are unchanged.

## Failure Handling and Rollback

If the host tests or physical WebSocket test fail, do not add HTTP-server
workarounds. Revert the candidate firmware to the last validated image and
continue tracing the route-hook boundary. No stored Wi-Fi, Tailscale, or LLM
credentials are modified by this change.
