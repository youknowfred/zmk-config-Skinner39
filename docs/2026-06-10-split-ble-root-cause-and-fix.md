# Skinner39 split/BLE connectivity — root cause and durable fix (2026-06-10)

Two distinct problems were diagnosed and fixed in this pass: the **acute 2026-06-10
total outage** (both halves dead, zombie macOS connection) and the **chronic
intermittent split death** that has plagued this board for years (ZMK upstream
[#718](https://github.com/zmkfirmware/zmk/issues/718) /
[#2776](https://github.com/zmkfirmware/zmk/issues/2776) territory). They turned out
to be unrelated mechanisms — the acute incident was not even a bug.

## 1. The acute outage (2026-06-10, ~04:00)

**Symptoms:** no keystrokes from either half; LEFT OLED output icon EMPTY; macOS
showed `SKINNER39` as *Connected* with battery reads working ("zombie"); reflashing
both halves with known-good images changed nothing; single-tap resets changed
nothing.

**Root cause:** the LEFT (central) had its **active BLE profile setting pointed away
from profile 0**, where the Mac's bond lives. ZMK only sends HID input to the
*active* profile's connection. macOS kept a bond-level link up (hence "Connected"
and battery reads — BAS is not gated on the active profile) while ZMK dropped every
keystroke. The setting lives in the **settings partition, which firmware reflashes
do not touch** — which is why reflashing could never fix it, and why the historical
"clear-bond + replug + BT toggle" ritual *did* (settings wipe by side effect).

**The empty OLED icon was the system working as coded, not display corruption:**
with no USB cable and the active profile not connected, `get_selected_transport()`
returns `ZMK_TRANSPORT_NONE` — and the nice!view-derived status widget had no
`case` for it, drawing an empty string. The keyboard was truthfully reporting
"output: none" in a vocabulary with no glyph for it.

**Live fix (zero flashing):** Q+P sticky combo → system layer → `BT_SEL 0`.
Wireless typing returned instantly. The wired path (and the both-transports-ready
path) tested healthy afterwards.

**Plausible trigger:** the keymap had `BT_SEL` keys only for profiles 0–2 (of ZMK's
5) and **no `&out` bindings at all**. Anything that moved the active profile to 3/4
(ZMK Studio, a mis-tap during the raw-HID-era instability) was unrecoverable from
the keys. Hardening below.

## 2. The chronic intermittent split death

### What it is

For years: the split link (or the host link) intermittently dies and **stays dead
until one half is power-cycled**. Documented prior attempts (upstream issues, conf
hardening — `PREF_LATENCY=10`, `PREF_TIMEOUT=500`, +8 dBm TX, PHY 1M — and the
`e96a20c` scan-retry fork) reduced frequency but never eliminated it.

### Root-cause audit (pin `zmkfirmware@26246da`, 2026-05-30 — effectively upstream HEAD)

The reconnect machinery on **both** sides is built from **single-shot recovery
actions**: one attempt, and on any transient BLE-stack error the machinery wedges
**permanently** — no retry is ever armed. RF noise, callback-context restrictions,
or controller-object lifetime races make such transient errors routine on nRF52840.
Five distinct wedge classes were identified; the original cure fixed exactly one:

| # | Side | Wedge | Mechanism | Fixed by |
|---|------|-------|-----------|----------|
| V1 | central | scan-start wedge | `bt_le_scan_start()` fails from a BT callback → `is_scanning` stuck `true` → every later `start_scanning()` early-returns "already running" | `e96a20c` (scan-retry cure, 2026-06-07) |
| V2 | central | slot-leak brick | `bt_le_scan_stop()` fails in `split_central_eir_found()` → just-reserved slot stranded in `CONNECTING` → every future `reserve_peripheral_slot()` returns `-ENOMEM`, no scan running, nothing armed to retry | **this pass** (`d62e565c`) |
| V5 | central | mute link | any `bt_gatt_discover`/`bt_gatt_subscribe` failure → connection stays up with no subscriptions; central sees "all connected" and stops scanning; split looks alive but relays nothing | **this pass** |
| V6 | peripheral | silent half | `bt_le_adv_start()` fails in `advertising_cb` (single-shot, e.g. directed advertising while the stale conn object drains) → logged and never retried | **this pass** |
| V7 | peripheral | recycled dependency | advertising restart hinges entirely on Zephyr's `.recycled` callback; if late or lost, nothing ever rearms advertising | **this pass** |

This taxonomy matches the lived experience precisely: *which half needed the
power-cycle varied by which side's wedge fired*; the link otherwise worked for
days at a time (the wedges need a transient error to trigger).

### The fix (fork commit `d62e565c`, branch `skinner39-split-reconnect`)

Philosophy: **every recovery action retries until it succeeds, and every
"connected but broken" state self-destructs into the normal reconnect cycle.**

- central `eir_found`: scan-stop failure → release the slot, reschedule the scan
  cycle (500 ms).
- central discovery/subscribe failures (3 sites): disconnect the link so
  `disconnected → release slot → start_scanning` retries cleanly.
- central `connected()` with no reserved slot: disconnect and rescan (was: leak an
  unsupervised live conn).
- central **subscription watchdog**: 10 s after any central-role connect, any slot
  still `CONNECTED` but missing discovery handles/subscriptions is torn down for a
  clean retry. Catches anything the explicit error paths miss. Battery-level
  subscription deliberately exempt (must not flap the link).
- peripheral advertising: delayable work, self-retry every 1 s on real failure
  (`-EALREADY` tolerated as success), guarded on `enabled && !is_connected`.
- peripheral `disconnected()`: rearm advertising at 2 s as a backstop in case
  `.recycled` never fires (the normal path still wins the race).
- peripheral `set_enabled(false)`: cancel pending advertising work.

### Config-repo hardening (same pass)

- `west.yml`: pin bumped `e96a20c` → `d62e565c`.
- Keymap system layer: full `BT_SEL 0–4` coverage + `&out OUT_USB / OUT_BLE /
  OUT_TOG` (physical, via Q+P combo: Q W E R = profiles 0–3, A = profile 4,
  S/D/F = USB/BLE/toggle, T = bootloader, Z = BT_CLR).
- keysmith_glance status widget: `ZMK_TRANSPORT_NONE` (and any unknown endpoint
  state) now renders `–` instead of an empty icon.

## 3. Field guide (when something looks dead)

1. **Zombie (macOS "Connected", battery OK, no keys):** Q+P → Q (`BT_SEL 0`).
   Don't reflash — it can't help (settings partition survives).
2. **Output icon shows `–`:** no transport is ready. Plug USB or fix the BLE
   profile; this is a state display, not a crash.
3. **Split dead after this fix:** it should self-heal within seconds. If it ever
   doesn't, capture logs before power-cycling (USB-logging build: swap snippet
   `studio-rpc-usb-uart` → `zmk-usb-logging`, add `CONFIG_ZMK_LOG_LEVEL_DBG=y`,
   LEFT only) — that's a new wedge class worth a writeup.
4. **Full bond scramble (rare):** asymmetric reset playbook in `build.yaml`
   comments. RIGHT must NEVER get the minimal `settings_reset` shield (kills the
   PMW3610) — use `skinner39_right_CLEAR_BONDS.uf2`.

## 4. Soak-test protocol (post-flash)

- Days 1–3 normal use, both halves on battery, LEFT occasionally on USB.
- Stress the reconnect paths deliberately a few times: walk the RIGHT half out of
  range until it drops, return — it must rejoin without touching anything (this
  exercised V1/V2/V6/V7). Sleep/wake the Mac (host-link path).
- Any unrecovered drop → logging build per §3.3.

## Verification status

- [x] Acute outage root-caused and fixed live (2026-06-10, no flash needed)
- [x] All five wedge classes patched on the fork (`d62e565c`)
- [x] CI green on the bumped pin (main run 27275269447, commit `26f6d06`)
- [x] Both halves flashed with the v2-cure build (2026-06-10 ~05:20)
- [x] Post-flash verification: LEFT/RIGHT/trackball on USB ✓, BLE typing cable-out
      (the 04:00 failure state) ✓, **deliberate RIGHT power-cycle drop → rejoined
      hands-off** ✓ — settings (bonds, active profile) survived the reflash as designed
- [ ] 1-week soak with deliberate drop/reconnect stress (started 2026-06-10; if a
      drop ever fails to self-heal, capture logs per §3.3 before power-cycling)

## 5. Day-1 soak finding (2026-06-11) — open investigation

Cure v2 measurably improved things, but a **new, more severe failure class**
showed up a few times on day 1:

- RIGHT drops; power-cycling RIGHT does **not** fix it (so the peripheral was
  advertising — central-side fault confirmed);
- **the LEFT (on battery) stops typing too** — i.e. the central's *host* link is
  down simultaneously with the split link;
- only a LEFT power-cycle recovers.

Both of the central's BLE roles dying together is not the single-shot-wedge
family of §2 — candidate mechanisms (radio-scheduling starvation during
aggressive reconnect-scanning, controller-level lockup, workqueue stall, battery
brownout under TX load) need **evidence, not guesses**. Per §3.3 discipline, the
diagnostic instrumentation is now standing:

- `skinner39_left_USB_LOGGING` build artifact (in `build.yaml`): cure-v2 LEFT
  firmware with `zmk-usb-logging` + `ZMK_LOG_LEVEL_DBG` (ZMK Studio unavailable
  while flashed — the CDC port is the log console).
- [tools/split-log-capture.sh](../tools/split-log-capture.sh): continuous
  timestamped capture on the Mac, one file per connect-session, survives resets.
- Protocol: LEFT stays on USB (it keeps typing via USB even mid-failure). On the
  next drop: note wall-clock time, glance at the OLED (frozen vs still updating —
  distinguishes firmware stall from BLE-stack death), check whether macOS still
  lists SKINNER39 as connected, then power-cycle as needed — the log keeps
  everything.

What the log should reveal at the drop timestamp: whether `disconnected` fired
for the split conn, whether scanning restarted (and the eir_found/create_conn
cycle), what happened to the host-link conn, and whether logging itself goes
silent (= firmware-wide stall).
