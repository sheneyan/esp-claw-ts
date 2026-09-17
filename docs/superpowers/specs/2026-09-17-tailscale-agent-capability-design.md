# ESP-Claw TS Agent-Aware Tailscale Capability Design

Date: 2026-09-17

## Summary

ESP-Claw TS currently runs a Tailscale-compatible MicroLink client and exposes
basic status through the settings web application, but the agent has no native
knowledge of or controlled access to that runtime. This change adds one
authoritative `tailscale_network` skill, a provider-backed `cap_tailscale`
capability group, richer DERP diagnostics, transactional runtime Exit Node
switching, and matching live controls in the Tailscale settings page.

The first release deliberately excludes authentication, login-server, device
hostname, identity-reset, tailnet administration, arbitrary route changes, and
automatic network-path changes initiated by the agent.

## Goals

- Let the agent explain its own Tailscale connectivity and health.
- Expose the device Tailscale IP, connection state, direct/DERP path, peer
  counts, DERP diagnostics, current egress, Exit Node state, and recent errors.
- Let the agent list usable Exit Nodes.
- Let the agent switch, clear, or reconnect Tailscale only after an explicit
  request from the current user.
- Apply Exit Node changes immediately without restarting the ESP32, persist a
  successful choice, and restore it after a later device restart.
- Reject invalid or offline Exit Nodes without changing runtime or persisted
  state.
- Make the settings page expose the same live Exit Node behavior as the agent.
- Preserve the separation between reusable ESP-Claw capability code and the
  ESP-Claw TS/MicroLink implementation.

## Non-goals

- Reading, displaying, modifying, or asking for the Tailscale auth key.
- Modifying the login server, device hostname, enabled state, or maximum peer
  count through the agent.
- Clearing the Tailscale identity or performing a factory reset.
- Administering other tailnet nodes, ACLs, grants, tags, DNS, advertised routes,
  or the Tailscale control plane.
- Automatically selecting an Exit Node or reconnecting because the agent
  observed degraded connectivity.
- Selecting a preferred DERP region in the first release.
- Providing an Internet-hop traceroute. DERP diagnostics describe Tailscale
  relay state, not every physical network hop.

## User-visible behavior

### Read-only diagnosis

The agent may call read-only tools when the user asks about Tailscale state,
remote connectivity, the device Tailscale IP, direct versus relayed paths,
available Exit Nodes, or a possible Tailscale connection problem.

It must distinguish a relay path from a failure. `path=derp` means traffic is
relayed and may have higher latency; it does not by itself mean the connection
is unhealthy.

### Mutating operations

The agent may switch an Exit Node, clear the current Exit Node, or trigger a
lightweight reconnect only when the current user explicitly requests that
specific action. Describing slow or failed networking is not permission to
change the network path.

Every mutating tool requires `user_confirmed: true`. The skill may supply this
field only when the immediately relevant user request explicitly asks for the
mutation. This is a semantic guard rather than an authentication mechanism;
the capability remains limited so even an incorrect call cannot access keys,
identity reset, or arbitrary routes.

### Persistent live Exit Node selection

When the user selects an Exit Node by exact CGNAT IP or hostname:

1. Resolve the input against the current peer snapshot.
2. Require one unique match.
3. Require that the peer advertises Exit Node capability and is online.
4. Apply the new choice by restarting only the MicroLink runtime, not the
   ESP32, Wi-Fi manager, agent, or application.
5. Wait for the Tailscale connection to recover and for the Exit Node policy to
   reach `active`.
6. Persist the selected IP only after successful activation.
7. Return the observed final state, including the selected node and egress.

The Tailscale interface may be unavailable for several seconds while the
MicroLink runtime is rebuilt. A browser connected through the tailnet may need
to reconnect its WebSocket, but the ESP32 and agent task must not restart.

An offline, unknown, ambiguous, or non-Exit-Node peer is rejected before any
runtime or persistent change.

Clearing the Exit Node follows the same transaction and succeeds only after
the runtime reports:

```text
exit_node = ""
exit_state = disabled
egress = sta
```

### Reconnect

`tailscale_reconnect` uses MicroLink's lightweight `microlink_rebind()` path.
It does not clear identity, change configuration, recreate credentials, or
restart the ESP32. It also requires an explicit user request.

## Architecture

```text
LLM
  -> tailscale_network skill
  -> cap_tailscale capability group
  -> provider callbacks registered by edge_agent
  -> ts_claw worker queue
  -> MicroLink
```

### `cap_tailscale`

Add a reusable capability component under
`components/claw_capabilities/cap_tailscale/`. It owns tool schemas, argument
validation, stable JSON rendering, output-size limits, and the user-confirmed
guard. It must not include or depend on `microlink.h`, `ts_claw.h`, application
configuration storage, or the HTTP server.

The component accepts a provider interface with callbacks for:

- status and DERP diagnostics;
- peer/Exit Node listing;
- transactional Exit Node selection;
- transactional Exit Node clearing;
- lightweight reconnect.

Provider result structures contain only fields safe for the model. In
particular, there is no auth-key field and no callback that can retrieve it.

### Application integration

`edge_agent` implements the provider by adapting `ts_claw` and application
configuration storage. It registers `cap_tailscale` as an external capability
group before `app_claw_start()`. This keeps the fork-specific integration out
of the reusable `app_claw` built-in capability table.

The external group is compiled only for builds that include TS-Claw. Its group
id is `cap_tailscale`, its display name is `Tailscale`, and it is LLM-visible by
default on the ESP32-S3 N16R8 TS-Claw board.

### `ts_claw` runtime operations

Extend the existing worker event queue with synchronous requests for:

- a full diagnostic snapshot;
- peer snapshots suitable for capability output;
- set/clear Exit Node;
- rebind.

All MicroLink lifecycle changes occur on the existing worker. Capability,
HTTP-server, and agent tasks must not stop, destroy, or replace MicroLink
directly.

The runtime Exit Node operation snapshots the previous desired IP, retires the
current exit probe, rebuilds MicroLink with the candidate configuration, and
waits for the configured timeout. Success requires connected state, the
selected Exit Node being ready, a working exit probe, and `exit_state=active`.

If activation fails, the worker rebuilds MicroLink with the previous desired
IP and reports both the original failure and whether rollback recovered. The
provider persists the new application configuration only after runtime success.
If persistence then fails, it requests a runtime rollback to the old choice and
returns failure. A rollback failure is reported explicitly and retained in
`last_error`; it must never be presented as success.

## Capability tools

### `tailscale_status`

Input: empty JSON object.

Output includes:

- enabled and connected state;
- device hostname and Tailscale IP, but no auth or identity secrets;
- Tailscale path classification;
- online and total peer counts;
- selected Exit Node, Exit Node state, and actual egress;
- most recent runtime error;
- DERP diagnostics described below.

### `tailscale_list_exit_nodes`

Input: empty JSON object.

Output is a bounded array of peers that advertise Exit Node capability. Each
item contains IP, hostname, online state, direct-path state, DERP region id, and
DERP region name. The list may contain offline nodes for diagnosis; mutating
selection rejects them.

### `tailscale_set_exit_node`

Input:

```json
{
  "node": "racknerd-4fc9d3f.tailnet.example.ts.net",
  "user_confirmed": true
}
```

`node` accepts an exact hostname or an IPv4 address inside
`100.64.0.0/10`. Hostname comparison is case-insensitive. Partial hostname
matching is allowed only if it produces exactly one peer; otherwise the tool
returns candidates and asks the user to choose an exact hostname or IP.

Success output includes the resolved node, final `active` state, actual egress,
and `persisted: true`.

### `tailscale_clear_exit_node`

Input:

```json
{
  "user_confirmed": true
}
```

Success requires ordinary STA egress and an empty persisted Exit Node.

### `tailscale_reconnect`

Input:

```json
{
  "user_confirmed": true
}
```

Success means the rebind request was accepted and the connection returned to a
healthy connected state within the timeout. It does not imply a direct path.

## DERP diagnostics

MicroLink already provides the required raw data through
`microlink_get_diag()`, `microlink_get_derp_rtts()`,
`microlink_get_derp_region_name()`, `microlink_get_last_derp_heartbeat_ms()`,
`microlink_get_ctrl_last_rx_ms()`, and peer snapshots.

Expose a bounded diagnostic object containing:

- active/home DERP region id and name;
- configured default DERP region id and name;
- the most recent bounded, RTT-sorted region measurements;
- DERP heartbeat age in milliseconds;
- control-plane receive age in milliseconds;
- reconnect-cause counters;
- for listed peers, direct/DERP state and the peer relay region.

Convert monotonic timestamps to ages inside `ts_claw`; never expose raw
monotonic timestamps. Unknown regions use a null name rather than an invented
location. A zero RTT is reported as timed out/unavailable, not as zero latency.
Do not expose the STUN-discovered WAN public IP in model-visible output.

The diagnostics describe the Tailscale relay topology visible to MicroLink.
They are not an Internet traceroute and must not be described as one.

## Skill design

Add one authoritative component skill, `tailscale_network`, owned by
`cap_tailscale`. Its frontmatter activates only `cap_tailscale`, uses
`manage_mode: readonly`, and describes user intent in both diagnosis and
explicit control terms.

One skill is preferred over separate diagnosis and control skills so that the
mutation guard, terminology, error interpretation, and success criteria have a
single source of truth.

The skill instructs the model to:

- use status for questions about its own Tailscale address or health;
- use Exit Node listing before a requested switch;
- diagnose and recommend without mutating when the user only reports a problem;
- mutate only after an explicit request in the current conversation;
- never request, reveal, or modify auth keys;
- report hostname but direct hostname changes to the settings page;
- distinguish DERP relay from failure;
- report `pending`, `fallback`, timeout, persistence failure, and rollback
  failure without claiming success;
- state that it controls only this device, not the full tailnet.

## Web settings behavior

The actual device UI is authoritative. Although the current source and bundled
frontend contain Exit Node select code, the user-observed settings surface did
not present a usable control. Treat this as a rendering/product defect and add
an end-to-end acceptance check rather than relying on source inspection.

The Tailscale settings page must:

- always render an Exit Node control in the settings section;
- always render the `None (use regular Wi-Fi)` option;
- list the peers returned by `/api/tailscale/exit-nodes`;
- show offline peers but disable their selection;
- show an explicit empty-state or request error instead of silently hiding the
  control;
- apply Exit Node selection and clearing through a live runtime endpoint;
- show switching, active, rollback, and error states;
- refresh runtime status after completion;
- update the local configuration cache only after confirmed persistence;
- remove the blanket restart implication from Exit Node-only changes.

Auth key, login server, device hostname, enabled state, and maximum peers keep
their existing configuration-page workflow and restart semantics.

The live HTTP endpoint and `cap_tailscale` provider must call the same
application service so browser and agent behavior cannot diverge.

## Error model

Stable error categories are:

- `not_enabled`
- `not_connected`
- `invalid_node`
- `ambiguous_node`
- `node_not_found`
- `node_offline`
- `not_exit_node`
- `switch_timeout`
- `runtime_apply_failed`
- `persistence_failed`
- `rollback_failed`
- `reconnect_failed`
- `busy`

Errors include a short user-safe message and current runtime state. They must
not include credentials, internal key material, full public keys, or raw
control-plane payloads.

Only one mutating Tailscale operation may run at a time. A concurrent mutation
returns `busy`; read-only snapshots remain available where safe.

## Testing and acceptance

### Host tests

- Tool schema and argument validation for all five capabilities.
- Stable JSON output and bounded arrays/strings.
- Absence of auth key, WAN public IP, full public key, and raw control payloads.
- Exact IP, exact hostname, unique partial hostname, ambiguous hostname,
  unknown peer, offline peer, and non-Exit-Node selection.
- DERP region names, unknown names, zero/timed-out RTT, RTT ordering, heartbeat
  age, control receive age, and reconnect counters.
- Worker serialization and `busy` behavior.
- Successful live apply and clear.
- Runtime start failure, activation timeout, exit-probe failure, persistence
  failure, successful rollback, and rollback failure.
- Rebind preserves identity and desired Exit Node configuration.

### Frontend verification

- Frontend build and typecheck pass.
- With an empty Exit Node response, the control and `None` option remain visible.
- With one online node, the node is selectable and live state is shown.
- Offline nodes are visible but disabled.
- API errors render a useful message and do not remove the control.
- A completed live switch updates status without a device restart.
- The actual device page is inspected after flashing; source presence alone is
  not acceptance evidence.

### Firmware verification

- Full ESP-IDF build for `esp32_s3_n16r8_ts_claw` succeeds.
- Existing TS-Claw route, policy, provisioning, and configuration tests remain
  green.
- No new credentials or private tailnet data are committed.

### Physical ESP32-S3 N16R8 smoke test

1. Ask the agent for its Tailscale IP and verify it matches runtime status.
2. Ask whether the path is direct or DERP and verify region/RTT reporting.
3. Ask for available Exit Nodes and verify the known online node appears.
4. Explicitly request a switch and verify `active`, Exit Node egress, and no
   ESP32 restart.
5. Restart the ESP32 and verify the selected node remains configured.
6. Explicitly clear the Exit Node and verify ordinary STA egress.
7. Report a connection problem without requesting a change and verify the
   agent diagnoses but does not mutate.
8. Trigger a reconnect explicitly and verify identity and persisted settings
   remain unchanged.
9. Verify the web page visibly offers the same Exit Node and can switch it live.

## Documentation

Update the English and Chinese ESP-Claw TS guides with:

- the agent-aware Tailscale capability and its limits;
- examples of status, DERP, and Exit Node requests;
- the explicit-user-request mutation rule;
- live/persistent Exit Node behavior and rollback;
- the fact that auth, login server, hostname, enablement, and identity remain
  settings-page operations;
- the distinction between DERP diagnostics and Internet traceroute.
