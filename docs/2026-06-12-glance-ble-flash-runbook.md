# Glance BLE-first — Fred's flash runbook (P0-6, host-data-on-glass over HID-over-GATT)

> **THE GATE, FIRST:** the LEFT half currently runs `skinner39_left_USB_LOGGING` as the
> **armed trap** for the open dual-role failure
> ([2026-06-10-split-ble-root-cause-and-fix.md §5](2026-06-10-split-ble-root-cause-and-fix.md)).
> Flashing LEFT **evicts the instrument and loses the capture the whole §5 investigation
> waits on.** Run this runbook only **after the soak/§5 investigation concludes** (the drop
> was caught and written up, or the soak window closes clean) **or after you explicitly
> decide the trap is no longer worth holding.** Nothing below is urgent; the artifacts wait.

What this flash changes: the LEFT gains a second HID service **over BLE only**
(`vendor/zmk-raw-hid` `hog.c`, usage page `0xFF60`/usage `0x61`) so `keysmithd glance` can
push WPM / comfort / app / tip onto the middle OLED cell. **USB enumeration is
byte-identical to the primary build** (the banned CDC+2×HID composite stays banned) — and,
side-benefit, this restores **ZMK Studio** on LEFT (the logging instrument had repurposed
the CDC port as a log console).

## 0. Build (one push — CI was authored but not fired; this session ran no-push)

```sh
cd ~/Documents/skinner39/ZMK/zmk-config-Skinner39
git push origin p0-6-ble-glance      # branch exists locally @ 8d68fa0; CI fires on the push
gh run watch -R youknowfred/zmk-config-Skinner39   # all 7 targets must go green
```

(A stray **empty** `p0-6-ble-glance` ref already exists on origin pointing at old main —
push residue from a denied command, zero content; the push above simply fast-forwards it.)

## 1. Strings-probe BEFORE anything touches the board (the rule that caught two bad builds)

```sh
gh run download <run-id> -R youknowfred/zmk-config-Skinner39 -D /tmp/glance-ble
tools/glance-ble-probe.sh /tmp/glance-ble
```

Expected: `PASS` for both `GLANCE_BLE` images (descriptor `06 60 FF 09 61 A1 01` present,
`HID_1` absent) and `PASS` for the primary LEFT artifact (descriptor absent — the
experiment flags didn't leak). **Any FAIL → stop, do not flash.**

## 2. Flash LEFT — widget image first (the discriminating experiment)

The widget boot-crash was only ever observed **inside** the USB composite (2026-06-09/10
bisect; the bisect never could build widget-without-USB — the BLE-only image is that
missing experiment). Flash order:

1. Double-tap reset on LEFT → "Key Ball" boot drive → `cp` **`skinner39_left_GLANCE_BLE`**'s
   UF2 onto it (cp, not Finder-drag).
2. **If it boots** (OLED lights, BLE reconnects, typing works): the widget is exonerated
   outside the composite — the 2026-06-09 crash was a composite-coupled failure. Continue to §3.
3. **If it does NOT boot** (no OLED, no BLE, USB invisible): single-tap reset → boot drive →
   flash **`skinner39_left_GLANCE_BLE_nowidget`**. If that boots, the widget is proven
   independently broken (a real isolated repro at last — worth its own debug session);
   glance still works wire-wise, just nothing renders on the middle cell yet.
4. **Escape hatches** (unchanged): single-tap reset clears a wedge; zombie connection →
   Q+P → Q (`BT_SEL 0`); full scramble → the Repair playbook (`build.yaml` comments;
   RIGHT never gets `settings_reset`). Revert UF2 = the primary LEFT artifact from the
   same run (or `skinner39_left_USB_LOGGING` to re-arm the trap).

⚠ One new risk class: the BLE image **adds a HID service to the GATT table** — bonded
macOS should rediscover via Service Changed, but if the Mac side acts confused
(keys dead while "Connected"), toggle Bluetooth off/on first; full re-pair only if that
fails. Settings (bonds, active profile) survive the reflash as always.

## 3. Verify the BLE collection from the Mac — cable OUT first

```sh
# with NO USB cable (this is the whole point — HID-over-GATT):
hidutil list | grep -i "ff60\|skinner"        # expect a 0xFF60 usage-page entry
cd ~/Documents/keysmith
cargo run -p keysmithd --release -- glance --secs 30
```

Expected within a few seconds: `✓ link healthy`, a state line with your real WPM/comfort/
app/tier-tip, `N frame(s) on the wire` — and, on the **widget image**, the middle OLED
cell switches from the profile circles to the comfort number + bar (it falls back to
circles 65 s after the host goes quiet; the 30 s heartbeat keeps it alive while running).

Then plug USB in and confirm: typing still works on the cable, Studio reachable
(`keysmithd probe-rpc`), and `glance` keeps flowing (it rides BLE regardless).

If `glance` reports `no 0xFF60/0x61 collection found`: check macOS sees the board as
connected (the bond), then `hidutil list`. If hidutil shows it but open fails
`not permitted`, grant Input Monitoring to the invoking binary (a terminal `cargo run`
inherits the terminal's grant — same rule as shift-lab).

## 4. Afterwards

- Record the outcome in the keysmith tracker §5 `P0-6` row + a §11 entry (which image
  booted, hidutil evidence, first frames on glass).
- Merge the branch once everything above is green:
  `git checkout main && git merge p0-6-ble-glance && git push origin main && git push origin --delete p0-6-ble-glance`
  (single-branch hygiene restored).
- If the widget image crashed and the nowidget image is on the board: file the isolated
  widget repro as the next firmware session's first target — it finally has a
  one-variable reproduction.
