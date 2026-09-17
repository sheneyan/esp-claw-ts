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

Choose the Wi-Fi settings, enter the 2.4 GHz SSID and password, save, and
restart the device. The access point normally closes after the station joins
Wi-Fi.

If the SSID scan reports `esp_wifi_scan_start failed`, enter the SSID manually.
This does not by itself mean that station association will fail.

The BOOT button provides recovery on the tested board profile:

- Hold for 3 to 9 seconds, then release: reopen the provisioning access point.
- Hold for at least 10 seconds, then release: factory reset and restart.

A factory reset removes saved application, Wi-Fi, and ESP-Claw TS settings.

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

## Exit Node Behavior

Start with **Exit Node: None**. After the device is connected, the Tailscale
page lists exit nodes visible to this peer. Select one, save, and restart.

The status page distinguishes the configured node from actual egress:

- `exit` means the selected exit node is healthy and device-originated outbound
  traffic uses it.
- `wifi` means ordinary Wi-Fi egress is active.
- `fallback` means the selected exit node is configured but unavailable, so
  outbound traffic has fallen back to Wi-Fi.

Recovery is intentionally conservative: an exit node must show repeated
successful probes before egress switches back to it. The fallback policy
controls traffic created by the ESP32 itself; it does not advertise the ESP32
as an exit node and does not route another LAN through the board.

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

Restart after changing Tailscale settings. Then check `/api/tailscale/status`
or the Tailscale page. Confirm all of the following:

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
