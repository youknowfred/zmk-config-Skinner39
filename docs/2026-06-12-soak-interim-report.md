# Soak interim report — the trap's first 41 hours (2026-06-12)

Status of the **dual-role failure investigation** ([root-cause doc §5](2026-06-10-split-ble-root-cause-and-fix.md)):
the `skinner39_left_USB_LOGGING` instrument has been on LEFT since 2026-06-10 ~19:41
(SNIPE-LEFT-LOGGING image, pin `13b4175c`), rig capturing continuously. This is a
read-out of `split-logs/20260611-032247.log` (141 MB, capture session 2026-06-11
03:22 → 2026-06-12 ~14:17, still live at time of writing).

## 1. Headline: the split link has not dropped once

Across ~41 hours of firmware time (one reboot in the middle, §3), the LEFT↔RIGHT
split link recorded **zero disconnects** — no `Disconnected` for the split conn, no
`Scanning successfully started`, no `Initiating new connection` (the cure-v2
lifecycle lines that fired during the validated 2026-06-10 power-cycle test, so
their absence means *no events*, not missing instrumentation). Trackball, snipe and
scroll traffic flowed the whole window.

## 2. Host-link blips: 4 events, all benign, all self-healed

Four times — 06-11 05:30, 06-11 17:02, 06-12 04:47, 06-12 13:30, i.e. roughly every
11–12 h — the **host (Mac) BLE link** dropped with `reason 36 (0x24 = LL response
timeout)` and reconnected hands-off in **~1.0–1.1 s** (macOS re-initiates; no scan
involved on our side). Pattern (idle link, periodic, instant rejoin) reads as
macOS-side link management / RF hiccup on an idle HID link, not board pathology.

Two log-hygiene findings ride on those events:

- **Spurious `Failed to release peripheral slot (-22)` WRN** fires on every
  host-link disconnect: the central's `disconnected()` callback runs for *all*
  connections and tries to release a peripheral slot for the host conn, which was
  never in the slot table (`-EINVAL`). Benign but it muddies grep-based triage —
  a filter is queued for the next fork commit.
- `BAS Notifications disabled` precedes each (normal CCC cleanup on disconnect).

## 3. One unexplained event: spontaneous reboot at uptime 36:24:29

At **2026-06-12 08:04:39** the firmware uptime reset to zero. Context: active
trackball use — a mouse-button release 9 s before, mouse-layer churn until **T−3 s**,
then a clean boot with no error, fault, or stall logged (a hard fault resets the
nRF before anything reaches CDC at this config — no coredump backend compiled).

- Not a power event (LEFT is USB-powered), not a log stall (host-timestamp vs
  firmware-uptime deltas track 1:1 across every idle gap in the window).
- **Candidate: input-dispatcher stack overflow.** The whole `zip_*` processor
  chain (one-euro float math included) runs on Zephyr's input thread, default
  stack **1024 B** — doc-07 flagged `CONFIG_INPUT_THREAD_STACK_SIZE=4096` as the
  unmet prerequisite for exactly this configuration. Mitigation landed on main
  same day (`skinner39.conf`); it takes effect at the next LEFT flash.
- **Or: Fred reset it.** If you single-tapped reset / replugged around 08:04 on
  06-12, say so and this section closes as a non-event.

## 4. The topology trap — why 41 clean hours do NOT close §5

The day-1 failure class (RIGHT drops → RIGHT power-cycle doesn't fix → LEFT's
host link dies too → only LEFT power-cycle recovers) was observed **with LEFT on
battery**, i.e. with the central radio simultaneously serving the split link
(central role) and an *active* host HID link (peripheral role). The armed trap
runs **LEFT on USB**: the host link idles (HID rides the cable) and the dual-role
radio-load pattern that plausibly triggers the failure never occurs.

At the day-1 observed rate ("a few times a day"), ~41 h with zero events is wildly
unlikely under unchanged dynamics — the topology, not luck, is the difference.
**Conclusion: the trap as armed can confirm cure-v2's split stability (it has) but
probably cannot catch the dual-role failure.** Holding it longer mostly buys more
of §1's evidence, not the §5 capture.

## 5. Options forward (Fred's call — all artifacts staged or queued)

1. **Hold the current trap** — zero effort; catches only USB-mode recurrences.
2. **Re-arm as the battery-mode black box (recommended):** a fork commit (queued)
   grows the deferred-log ring (`CONFIG_LOG_BUFFER_SIZE=32768` in the
   `zmk-usb-logging` snippet) so the recorder retains the last ~minutes of events
   in RAM with no cable attached. LEFT then runs **on battery, normal wireless
   use** — the failure-prone topology. When the wedge fires: **plug the cable
   FIRST, power-cycle LAST** — the rig (or any serial reader) drains the retained
   ring, capturing the wedge's lead-up. Caveats, stated honestly: the flush needs
   the USB + logging threads alive (a full firmware stall may yield nothing — and
   even that is a finding); a power-cycle before plugging loses the RAM ring.
3. **Hybrid:** battery by day, periodic plug-ins to drain the ring.

Sequencing with the Glance lane (the [BLE-first runbook](2026-06-12-glance-ble-flash-runbook.md)
gate): option 2 **continues the trap, upgraded** — it's the same instrument family,
so it does not "end" the §5 investigation, it points it at the right topology. The
GLANCE_BLE flash *does* evict the instrument; the recommended order remains
**black-box trap until the §5 capture lands (or the window is declared closed) →
then the Glance flash sequence.**

## 6. Connectivity parameter matrix — current state (for the record)

| Lever | Value | Status |
|---|---|---|
| TX power | +8 dBm (`CONFIG_BT_CTLR_TX_PWR_PLUS_8`) | max useful, keep |
| PHY | 1M forced (`CONFIG_BT_CTLR_PHY_2M=n`) | robustness over speed, keep |
| Split latency/timeout | 10 / 500 (`ZMK_SPLIT_BLE_PREF_*`) | hardened 06-04, keep |
| Conn-interval pins (T0-F) | staged, commented out | **contraindicated** while §5 is open — pinning 7.5 ms raises radio load in exactly the suspect direction |
| Scan-window bounding | not implemented | candidate fork lever **if** the §5 capture shows reconnect-scan starving the host link; evidence first |

No parameter changes ship while the investigation is open — the baseline must stay
interpretable.
