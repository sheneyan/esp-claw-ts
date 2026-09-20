# ESP-Claw TS Guide

[简体中文](./ESP_CLAW_TS_CN.md)

## Scope

ESP-Claw TS is an unofficial community fork of
[ESP-Claw](https://github.com/espressif/esp-claw). It adds an optional
Tailscale-compatible network path through the bundled
[MicroLink](https://github.com/Csontikka/microlink) submodule.

The verified use case is deliberately narrow: the ESP32 joins a tailnet so an
authorized peer can reach the ESP-Claw web interface and WebSocket chat. The
device may also send its own outbound traffic through a selected exit node.
This release is not documented or supported as a general-purpose subnet
router.

ESP-Claw TS is not affiliated with or endorsed by Espressif or Tailscale.
Compatibility here does not imply support for every feature of an official
Tailscale client.

## Tested Configuration

The physically tested configuration is:

- Generic ESP32-S3 development board with 16 MB flash and 8 MB octal PSRAM
  (`N16R8`)
- 2.4 GHz Wi-Fi station uplink
- ESP-IDF 5.5.4
- Board profile `esp32_s3_n16r8_ts_claw`
- MicroLink gitlink at the revision committed by this repository
- Tailscale control service using an auth key
- Local web access and tailnet web/WebSocket access

The board profile assumes QIO 16 MB flash and octal 8 MB PSRAM. A board merely
being labelled "ESP32-S3" is not enough; confirm the module and flash/PSRAM
configuration before flashing.

> **ESP-IDF version baseline:** ESP-IDF **5.5.4** is the recommended and
> verified version for this project. The N16R8 firmware was built, flashed, and
> physically validated with it. ESP-IDF 6.x is not part of the verified
> baseline and may require additional compatibility work.

## Prerequisites

Install these tools before building:

- Git with submodule support
- ESP-IDF 5.5.4 and its toolchain
- ESP Board Manager (`idf.py bmgr`, installed through this project's managed
  components)
- A data-capable USB cable
- Access to a Wi-Fi network and a tailnet in which you can authorize a device

The commands below use the common ESP-IDF installation path. Change it if your
ESP-IDF checkout lives elsewhere.

## Clone with MicroLink

Clone recursively so the exact MicroLink revision is checked out:

```bash
git clone --recurse-submodules https://github.com/sheneyan/esp-claw-ts.git
cd esp-claw-ts
```

If the repository was cloned without submodules, repair it with:

```bash
git submodule update --init --recursive
```

Confirm that the submodule is present:

```bash
git submodule status --recursive
```

The MicroLink line should begin with a commit ID, not `-`.

## Build and Flash

From the repository root:

```bash
cd application/edge_agent
source "$HOME/esp/esp-idf/export.sh"
idf.py bmgr -c ./boards -b esp32_s3_n16r8_ts_claw
idf.py build
idf.py flash monitor
```

If more than one serial device is connected, pass the ESP32 port explicitly:

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

On Linux the port commonly looks like `/dev/ttyACM0`. Exit the serial monitor
with `Ctrl-]`.

> [!WARNING]
> A full project flash writes the storage partition and can replace mutable
> files previously stored under `/fatfs`. Back up files you need before
> reflashing. Wi-Fi and ESP-Claw TS settings are stored separately in NVS, but
> that is not a backup guarantee: commands such as `erase-flash` also remove
> NVS.

## First Boot and Wi-Fi Provisioning

Without valid station credentials, the board opens a provisioning access point
named like `esp-claw-84AEE5`. Connect to it and open:

```text
http://192.168.237.1/
```

Open **Basic Settings > Wi-Fi Settings** and save up to five 2.4 GHz networks
in priority order. On the first upgraded boot, the legacy single SSID and
password are migrated into the first profile automatically.

- At boot or after a disconnect, the device scans once and tries visible saved
  networks from top to bottom.
- If every attempt fails, the provisioning AP remains available and the full
  saved list is rescanned every 30 seconds until one profile obtains an IP.
- A healthy connection is not roamed merely because a higher-priority network
  becomes visible; this avoids disrupting the web UI, chat, and Tailscale.
- **Connect now** deliberately switches networks and may drop the current web
  session. If it fails, the remaining saved profiles are tried.
- Stored passwords are write-only. A blank password field preserves the saved
  password for the same SSID; selecting the open-network option explicitly
  clears it.

Saving the Wi-Fi list takes effect immediately without a reboot. AP name,
password, and other base network settings still follow the restart guidance in
the UI. The access point normally closes after the station joins Wi-Fi.

If the SSID scan reports `esp_wifi_scan_start failed`, enter the SSID manually.
This does not by itself mean that station association will fail.

The BOOT button provides recovery on the tested board profile:

- Hold for 3 to 9 seconds, then release: reopen the provisioning access point.
- Hold for at least 10 seconds, then release: factory reset and restart.

A factory reset removes saved application settings, all Wi-Fi profiles, and
ESP-Claw TS settings.

## Join the Tailnet

Create an auth key in the control service you intend to use. Prefer a narrowly
scoped, short-lived or one-time key where your deployment allows it. Treat the
key as a secret.

In the device web interface, open **Tailscale** and configure:

- **Enable Tailscale:** on
- **Auth Key:** the new auth key; this field is write-only and is never shown
  again
- **Device Hostname:** for example `esp-claw`
- **Login Server:** leave blank for the default service; a compatible custom
  control server may be entered when required
- **Exit Node:** leave at **None** for normal Wi-Fi egress during initial setup
- **Maximum Peers:** keep the default `16` unless there is a measured reason to
  change it; the accepted range is 1 to 64 and larger values consume more
  memory

Save the settings and restart the board. Saving means that configuration was
accepted; it does not prove that the tunnel is active. After restart, return to
the Tailscale page and check:

- Connection is **Connected**
- A Tailscale IP in `100.64.0.0/10` is displayed
- Connection path and peer counts are populated
- **Last Error** is empty

The device may still require approval in the tailnet administration console.
Tailnet ACLs or grants must also permit the source peer to reach it.

## Verify Local and Tailnet Access

First verify the ESP-Claw web service over the local Wi-Fi address shown in the
web header or serial log:

```bash
curl --noproxy '*' -fsS http://DEVICE_LAN_IP/api/webim/status
```

From another authorized tailnet peer, verify the node and connection path:

```bash
tailscale status
tailscale ping esp-claw
curl --noproxy '*' -fsS http://DEVICE_TAILNET_IP/api/webim/status
```

A `tailscale ping` result containing `via <LAN-IP>:<port>` indicates a direct
path. A result containing `via DERP(...)` is relayed and can have materially
higher and more variable latency. Relay use is not necessarily a fault.

Open `http://DEVICE_TAILNET_IP/` in a browser and send a chat message to verify
the WebSocket path as well as the HTTP page. A successful ping alone does not
prove that browser proxy rules, ACLs, HTTP, and WebSocket traffic are all
working.

## Agent-Aware Tailscale Controls

The built-in `tailscale_network` Skill activates the `cap_tailscale` Capability
Group and teaches the agent how to use these five tools:

| Tool | Purpose |
| --- | --- |
| `tailscale_status` | Read connection, selected Exit Node, actual egress, DNS compatibility egress/count, errors, peer counts, timing, reconnect counters, and bounded DERP diagnostics. |
| `tailscale_list_exit_nodes` | List currently visible Exit Node candidates, including online/direct state and DERP region when known. |
| `tailscale_set_exit_node` | Select an online node by canonical CGNAT IP or an unambiguous hostname selector. |
| `tailscale_clear_exit_node` | Disable Exit Node routing and return device-originated traffic to Wi-Fi. |
| `tailscale_reconnect` | Reconnect this device's Tailscale runtime without changing registration settings. |

Example user requests are “Show my Tailscale status and DERP diagnostics” and
“List the available Exit Nodes.” The corresponding reads require no
confirmation. `tailscale_status` reports the active/default DERP regions,
bounded per-region RTT samples, heartbeat/control ages, and reconnect counters
when the runtime provides them. An empty Exit Node list means that no candidates
are currently visible to this peer; it does not prove that the tailnet has no
Exit Nodes.

For example, a status read uses an empty object and can return a DERP excerpt
like this (region names and RTTs are runtime measurements):

```text
tool: tailscale_status
input: {}
result excerpt: {"connected":true,"path":"derp","derp":{"active":{"id":4,"name":"<region>"},"rtts":[{"region":{"id":4,"name":"<region>"},"rtt_ms":84,"timed_out":false}]}}
```

DERP data is relay-specific telemetry from the ESP32's Tailscale runtime. It is
not a hop-by-hop Internet traceroute, and it does not identify every network
segment between the device and a destination. A relayed path or a timed-out RTT
sample is diagnostic evidence, not by itself proof of a broken connection.

Every agent-requested mutation requires an explicit request from the current
user in the current interaction, and the tool call must contain
`user_confirmed: true`. The agent must not infer permission from earlier
messages or from a diagnostic result. The web interface currently provides live
Exit Node selection and clearing; choosing either directly confirms that web
action, so its HTTP request does not use the agent-only `user_confirmed` field.
Reconnect is available through the confirmed `tailscale_reconnect` agent tool
and the `/api/tailscale/reconnect` HTTP API, not as a page button. The web,
agent-tool, and direct-API paths call the same serialized live-control service
and therefore share its behavior, but their transport response schemas differ.
For example, HTTP mutation results expose `rollback_attempted` and
`rollback_recovered`, while the Capability result does not expose those fields.

`user_confirmed` and a direct web selection record current action intent only;
they are not authentication or authorization. The direct mutation HTTP routes
do not add an authentication wrapper. Restrict access to the device with a
trusted LAN exposure model and tailnet ACLs or grants; anyone who can reach
those HTTP routes may otherwise invoke them.

The agent cannot change the device hostname, auth key, login server, enabled
state, or maximum peers. Those remain settings-only operations in the web
interface. Erasing/resetting device identity is separate: it requires the
physical factory-reset path described above, and is not an agent or ordinary
settings-page operation. Registration and server validation are not agent
tools.

## Exit Node Behavior

Start with **Exit Node: None**. After the device is connected, the Tailscale
page lists exit nodes visible to this peer. Selecting a node, clearing the
selection, or asking the confirmed agent to do either applies immediately; an
Exit Node-only change does not require a restart. A successful operation is
persisted only after the runtime confirms the requested state.

The status page distinguishes the configured node from actual egress:

- `exit` means the selected exit node is healthy and device-originated outbound
  traffic uses it.
- `wifi` means ordinary Wi-Fi egress is active.
- `fallback` means the selected exit node is configured but unavailable, so
  outbound traffic has fallen back to Wi-Fi.

### DNS compatibility policy

While `egress` is `exit`, ESP-Claw TS classifies the current IPv4 resolvers
from lwIP/DHCP by their actual route. Private/link-local resolvers and captured
public resolvers use Wi-Fi STA; CGNAT resolvers, including `100.100.100.100`,
use WireGuard. Other public traffic continues through the Exit Node. Status
exposes `dns_egress`, `dns_bypass_active`, and bounded `dns_bypass_count`;
resolver addresses are not exposed to the agent or page:

- `sta`: every effective resolver uses STA;
- `exit`: every effective resolver uses WireGuard/Exit Node;
- `mixed`: effective resolvers use both paths;
- `unavailable`: no usable resolver path is currently known.

`dns_bypass_count` counts only public exact-IP STA bypasses, not private,
link-local, or CGNAT resolvers. Sites normally see the Exit Node public IP. In
`sta` mode the local resolver or ISP can observe queried domains; in `mixed`
mode it can observe the locally routed subset. Local DNS answers, CDN
geolocation, or DNS pollution may affect reachability or content selection.
`exit` does not imply a local DNS disclosure, while `unavailable` means name
resolution may fail. This behavior is for compatibility and is not a privacy
VPN. The route hook
operates on destination IP only, so **all traffic to a captured public resolver
IP**—not only UDP/TCP port 53—uses STA. The list is refreshed from lwIP while
the Exit Node is active and cleared on fallback, clear, rollback transition,
Wi-Fi loss, or runtime teardown.

Recovery is intentionally conservative: an exit node must show repeated
successful probes before egress switches back to it. The fallback policy
controls traffic created by the ESP32 itself; it does not advertise the ESP32
as an exit node and does not route another LAN through the board.

An offline candidate is rejected instead of being selected. An empty selector
is also rejected by the set action; use the explicit clear action to disable
Exit Node routing. If runtime application fails or times out, the operation is
reported as failed and the runtime control attempts to recover its previous
safe state. If persistence fails after a live switch, the shared service tries
to roll the runtime back to the previously persisted selection and reports
whether rollback was attempted and recovered. If persistence is unverified,
the current runtime may be restored while a later reboot still uses a different
selection; never interpret fallback, partial application, or rollback failure
as success.

The fallback state machine and egress selection have host-test coverage. The
tested device was observed with normal Wi-Fi egress and a configured exit-node
path, but every network environment should run its own controlled online/offline
test before relying on the behavior operationally.

## Hardware Recommendations

| Hardware | Status | Notes |
| --- | --- | --- |
| ESP32-S3 N16R8 | **Tested and recommended** | Matches the current 16 MB partition layout, octal PSRAM settings, build, and physical runtime verification. |
| ESP32-S3 N8R8 | Adaptation required | The CPU and 8 MB PSRAM are plausible, but the current 16 MB flash layout and board profile do not fit an 8 MB flash device directly. |
| ESP32-C5 | Unverified candidate | Its wireless features are attractive, but this application, its dependencies, board profile, USB behavior, memory budget, and runtime have not been ported and verified. |
| ESP32-P4 | Not recommended for this minimal design | It requires an external networking companion, losing the single-board Wi-Fi property. |
| ESP32-C3, ESP32-C6, ESP32-H2, and ESP32-WROOM-32 boards | Not supported by this release | Typical memory, PSRAM, connectivity, target, or board-profile constraints differ from the verified S3 configuration. |

Generic ESP-IDF support for a chip family is not evidence that ESP-Claw TS fits
or runs reliably on a specific board.

## Known Issues and Troubleshooting

### A browser shows HTTP 502 but a phone works

This usually means the desktop browser sent the private address to an HTTP or
SOCKS proxy. Test without the proxy first:

```bash
curl --noproxy '*' -v http://DEVICE_LAN_IP/api/webim/status
curl --noproxy '*' -v http://DEVICE_TAILNET_IP/api/webim/status
```

Add the exact device LAN and tailnet IPs to the browser extension's bypass
list. Also bypass the device's LAN subnet and `100.64.0.0/10` if the proxy tool
supports CIDR rules. Extension syntax differs; some tools do not interpret CIDR
or wildcard entries as expected, so exact IP entries or a temporary **Direct**
profile are the most reliable diagnostic.

### Configuration is saved but the device is disconnected

Restart after changing registration settings such as enablement, auth key,
hostname, login server, or maximum peers. Live Exit Node set/clear and reconnect
actions do not require a restart. Then check `/api/tailscale/status` or the
Tailscale page. Confirm all of the following:

- Wi-Fi station is connected and has working DNS/time
- auth key was valid when registration occurred
- the device is approved in the control plane
- hostname and maximum-peer values passed validation
- the source peer is allowed by tailnet ACLs or grants

The auth key is write-only. Leaving the field blank on a later save preserves
the stored key; it does not display or export it.

### The path is slow

Run:

```bash
tailscale ping esp-claw
```

DERP relay paths depend on Internet latency to the selected relay. Compare
several samples and check whether the peers eventually negotiate a direct
path. Do not treat ESP32 throughput as suitable for bulk transfer without
measurement.

### Local access works but tailnet access does not

Confirm that the Tailscale page reports **Connected** and a tailnet IP. Check
authorization, expiry, ACL/grant rules, and whether the remote client accepts
tailnet routes. Test the exact IP before diagnosing DNS naming.

### Reflashing removed files

`idf.py flash` for this project includes the generated storage partition.
Restore required `/fatfs` content from your own backup. NVS-held settings and
`/fatfs` files have different persistence boundaries; neither replaces a
backup.

## Security Notes

- Never commit auth keys, exported configuration, NVS dumps, or device identity
  material.
- Prefer scoped and short-lived registration keys.
- Tailnet reachability is not authorization by itself; maintain ACLs or grants.
- The device web interface is HTTP. Tailnet encryption protects the overlay
  path, but local-LAN clients still use plain HTTP unless another trusted layer
  is added.
- Review third-party dependencies and their licenses separately from the
  top-level project license.

## Upstream and License

The upstream project is
[`espressif/esp-claw`](https://github.com/espressif/esp-claw). ESP-Claw TS keeps
the upstream Apache License 2.0 [`LICENSE`](../LICENSE) unchanged and preserves
the original notices.

Third-party dependencies and submodules, including MicroLink, retain their own
copyright and license terms. Their inclusion does not relicense them under the
top-level repository license.
