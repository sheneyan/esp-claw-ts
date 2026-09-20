# Multi-Wi-Fi Profiles Design

## Goal

Make ESP-Claw TS portable across familiar locations without overwriting the
only saved Wi-Fi credential at each new location. The device will retain up to
five Wi-Fi profiles, reconnect automatically to an available profile in a
predictable priority order, and permit an explicit one-time switch from the
existing web configuration page.

## Scope and non-goals

This change applies to the ESP-Claw application configuration, Wi-Fi manager,
and embedded configuration UI. It does not change Tailscale identity,
authentication, hostname, Exit Node behavior, FATFS contents, or the connected
SSD1306 display and keys.

There are no automatic background handoffs while the STA connection is healthy.
The device switches profiles only at startup, after a connection loss, or when
the user explicitly requests a profile. This protects a running chat, HTTP
session, and Tailscale transport from an unnecessary network interruption.

## User-visible behavior

- The device stores up to five named Wi-Fi profiles. A profile consists of an
  SSID, an optional password, and its ordered position in the list. Passwords
  are accepted on write but never sent back to the browser.
- At startup and after a connection loss, the device scans once for visible
  SSIDs, attempts saved profiles in list order, and stops after the first
  successful DHCP connection.
- Each profile attempt has a bounded timeout. A wrong password or unavailable
  AP cannot permanently block attempts of lower-priority profiles.
- If no saved profile connects, the existing provisioning AP remains available
  and all saved profiles stay intact. Adding one profile does not delete or
  replace another profile.
- In the web UI, users can add, edit, delete, and reorder profiles, see which
  profile is currently connected, and use `Connect now` to switch explicitly.
  That action deliberately drops the current STA connection and attempts the
  selected saved profile.
- The existing single Wi-Fi fields are migrated into profile slot 1 on first
  boot after the upgrade. Existing installations continue to work unchanged.

## Data model and persistence

The application config grows a bounded `wifi_profiles` collection with five
fixed slots. Fixed-size slots avoid heap allocation and make NVS updates
atomic at the profile level. Each slot contains `enabled`, `ssid`, and
`password`; priority is its ascending slot order. Empty slots are ignored.

The current `wifi_ssid` and `wifi_password` remain as compatibility fields
only during migration. New writes use profile slots. Once migration has stored
slot 1 successfully, the runtime source of truth is the profile collection.
The server masks every password when serialising configuration.

## Runtime architecture

1. A profile-selection policy receives the saved profile list and scan results.
   It returns the first visible enabled profile in priority order without
   exposing passwords or calling ESP-IDF.
2. `wifi_manager` owns the scan/connect state machine. It applies exactly one
   STA configuration at a time, waits for a bounded connection result, then
   advances to the next selected profile on failure.
3. The existing Wi-Fi state callback fires only when STA connection state
   changes. The existing TS-Claw integration therefore withdraws/recreates
   transport naturally during an actual handoff; it needs no profile-aware
   Tailscale API.
4. The HTTP configuration API validates profile shape and capacity, writes a
   complete replacement profile list, and provides a narrow action endpoint
   for an explicit saved-profile switch. The UI calls that action only after
   the user presses `Connect now`.

## Failure handling and safety

- Profile validation rejects blank enabled SSIDs, SSIDs beyond ESP-IDF limits,
  duplicate enabled SSIDs, and invalid WPA passwords before persistence.
- A scan failure does not erase credentials. The manager falls back to bounded
  direct attempts in priority order, then keeps the provisioning AP reachable.
- A manual switch to a bad profile can temporarily disconnect the device; the
  manager continues through the remaining profiles and restores connectivity
  when another saved profile is available.
- Factory reset keeps its existing semantics: it clears all application
  settings, including all Wi-Fi profiles.
- This feature does not use or expose the OLED buttons. It does not require a
  flash-storage format or a full firmware erase.

## Verification

1. Host tests cover profile validation, migration from the legacy single
   fields, priority ordering, duplicate rejection, and the no-visible-network
   case.
2. Firmware tests/mockable policy tests cover bounded failure progression and
   confirm that a healthy connection does not trigger a background handoff.
3. Frontend tests cover add/edit/delete/reorder and the explicit switch action;
   password values must not appear in a configuration read response.
4. Build the selected N16R8 board with ESP-IDF 5.5.4 and run the affected
   host/frontend tests.
5. Before physical validation, preserve the current safe application-only
   flashing rule: do not use default `idf.py flash`, since its manifest can
   write writable storage. Flash only the rebuilt application image at
   `0x20000` after verifying the target port and MAC.
6. Physical acceptance uses at least two saved networks: boot on either one,
   power-cycle in the other, deliberately make the preferred network absent,
   and verify web-triggered switching and Tailnet recovery after each actual
   network change.

## Deferred decisions

- Dynamic re-ranking based on most-recent or strongest signal.
- Roaming across multiple APs with the same SSID/BSSID preferences.
- OLED profile selection or the K1-K4 keys.
- Wi-Fi provisioning through ESPTouch or other broadcast-based methods.
