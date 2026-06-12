# Keysmith status-report channel (TB-7) — wire spec + flash addendum

The device→host half of the Glance raw-HID link. Design-08 calls it **TB-7**:
tier-1 (ground-truth) layer attribution for M6/M9 + the battery read path that
arms the **M11 guardrail** (the host's RIGHT-battery `—` and every RIGHT-half
power proposal wait on it). Firmware: `src/keysmith_status_report.c`, gated on
`CONFIG_KEYSMITH_STATUS_REPORTS` (only the `GLANCE_BLE_STATUS*` images carry it).

## Transport

Same `0xFF60/0x61` HID-over-GATT collection `keysmithd glance` already opens
(hidapi, shared mode). These are **input reports** (GATT notify) — the locked
host→device Glance schema (tags `0xB0–0xB5`, host writes) is untouched and the
tag ranges are disjoint by construction (`0xAA–0xAF` zzeneg samples and
`0xCC–0xCD` relay also avoided). A host that doesn't know these tags can drop
them; a host that wants them reads `data[0]`.

Nothing is sent unless (a) the active profile is connected and (b) the host
has enabled notifications on the input report — with hidapi/IOKit on macOS the
HID system typically subscribes at device setup, so frames flow as soon as the
collection is open. Silence is the idle state, not an error.

## Frames (32 bytes, little-endian)

### `0xB8` — layer state

| byte | meaning |
|---|---|
| 0 | tag `0xB8` |
| 1 | schema version, `0x01` |
| 2 | sequence (u8, wraps; per-tag counter — gaps = dropped frames) |
| 3 | highest active layer **index** (u8) |
| 4..7 | full layer-state bitmask, LE u32 (`zmk_keymap_layer_state()`; bit 0 = base — the #699 workaround per doc-05 T3-A) |
| 8 | reason: `1` = change, `0` = heartbeat |
| 9..31 | zero |

Emission: every `zmk_layer_state_changed`, coalesced through one work item
(bursts collapse to latest state), **plus a 15 s heartbeat**. Host rule of
thumb: state is current within 15 s of subscribing, and exact from the first
change onward. This lifts `layer_tier` 2/3 inference to tier 1 wherever frames
cover the window; momentary-snipe (M6) undercount dies with it.

### `0xB9` — battery

| byte | meaning |
|---|---|
| 0 | tag `0xB9` |
| 1 | schema version, `0x01` |
| 2 | sequence (u8, per-tag) |
| 3 | central (LEFT) state-of-charge %, `0xFF` = unknown |
| 4 | central on USB power, 0/1 |
| 5 | peripheral count (this build: `1`) |
| 6 | peripheral (RIGHT) state-of-charge %, `0xFF` = never reported; **ZMK relays `0` when the peripheral disconnects** — treat `0` as "link down / stale", not "empty" |
| 7 | reason: `1` = change, `0` = heartbeat |
| 8..31 | zero |

Emission: on `zmk_battery_state_changed` + (with
`ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING`, enabled in the shield conf
since `1a6d891`) `zmk_peripheral_battery_state_changed`, plus a **60 s
heartbeat**. `batt_drain_pph` falls out of consecutive frames; M11 stops being
"unreadable" the first frame after a flash.

Independent of this channel, `..._BATTERY_LEVEL_PROXY=y` (same commit) exposes
the RIGHT level as a second standard Battery Service instance — that one is
for macOS itself; keysmith should prefer `0xB9` (no CoreBluetooth-vs-HID
ownership question, same transport it already reads).

## Host-side notes (interface only — host changes are the keysmith track's)

- `GlanceTransport` already opens the right collection; a read loop gains
  `match data[0] { 0xB8 => …, 0xB9 => …, _ => ignore }`. hidapi `read()`
  returns the 32 payload bytes (no report-id prefix on this collection).
- Frame loss is visible (per-tag seq); heartbeats bound staleness (15 s/60 s).
  A `layer_tier=1` sample should require an unbroken seq window around it.
- ZMK reports peripheral SoC `0` on split disconnect: render `—`, don't alarm.

## Flash addendum (extends the [BLE-first runbook](2026-06-12-glance-ble-flash-runbook.md))

Same gate as the runbook: **nothing flashes while the §5 soak trap is armed.**
When the trap concludes, the sequence becomes:

1. Runbook §2–§3 unchanged: flash `skinner39_left_GLANCE_BLE` (widget image,
   the one-variable boot-crash experiment), verify `hidutil` + `keysmithd
   glance` frames on glass.
2. **Then** flash `skinner39_left_GLANCE_BLE_STATUS` (same shape +
   status reports + the battery fetch/proxy conf that's been on main since
   `1a6d891`). Verify, cable out:
   - `hidutil list | grep -i ff60` still shows the collection;
   - a 30 s `keysmithd glance` run still paints the OLED (host→device leg);
   - device→host leg: watch any hidapi reader for `0xB8` within 15 s and
     `0xB9` within 60 s; hold `&mo 6` (macro-layer 35/36) and see the bitmask
     flip bit 6 with reason=1.
   - macOS Bluetooth menu may now show a second battery (the BAS proxy).
3. If step 1's widget image crashed and you're on `_nowidget`: use
   `skinner39_left_GLANCE_BLE_STATUS_nowidget` in step 2 — telemetry works
   wire-wise either way.
4. Revert UF2 for any step: the prior step's image (or `skinner39_left_USB_LOGGING`
   to re-arm the trap). Bonds survive; the new GATT services (HID + BAS proxy)
   can prompt a one-time Service-Changed rediscovery — BT off/on before
   suspecting anything worse.

Merge order after everything verifies: `glance-status-reports` ⊇
`p0-6-ble-glance`, so merging the status branch into main retires both; delete
both remote branches (single-branch hygiene).
