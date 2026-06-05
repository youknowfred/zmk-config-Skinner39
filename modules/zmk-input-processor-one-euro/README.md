# zmk-input-processor-one-euro — DRAFT (T1-E)

Adaptive **1-Euro** low-pass smoothing for the trackball's relative-delta stream:
heavy jitter/shimmer suppression at rest/slow speed, near-zero lag on fast throws.
This is the highest-ceiling *smoothness* lever in the master plan — and the riskiest,
because it is custom C in the input path. **It is a starting point, not a finished
module.** Status: **not compiled, not flashed.**

## Before you enable it (hard gates)
1. **Stack bump** — `CONFIG_MAIN_STACK_SIZE=4096` (a custom processor overflowing the
   input thread bricks the board until reflash; RECOVERY kit in `firmware/RECOVERY/`).
2. **FPU** — this draft uses `float` for readability; add `CONFIG_FPU=y`. The master
   plan calls for a **Q16.16 fixed-point** port — do that if RAM/latency demands.
3. **Soak** — run it for a day before trusting it. Try `ema-only` first (simpler).

## How to enable (LEFT half only)
1. Add this module to `config/west.yml` as a local project (or push it to a repo and
   pin a SHA):
   ```yaml
   - name: zmk-input-processor-one-euro
     path: config/modules/zmk-input-processor-one-euro   # local module
   ```
   (adjust the path to wherever west roots your manifest).
2. In `skinner39_left_nrf52840_zmk.dts`, define the node and place it FIRST in the
   pipeline (smooth → accelerate → scale):
   ```dts
   / {
       input_processors {
           one_euro: one_euro {
               compatible = "zmk,input-processor-one-euro";
               #input-processor-cells = <0>;
               // ema-only;                 // try this arm FIRST
               // ema-alpha-milli = <400>;
               min-cutoff-milli = <1000>;
               beta-milli = <7>;            // sweep {3, 7, 15}
               d-cutoff-milli = <1000>;
           };
       };
   };
   &pmw_listener { input-processors = <&one_euro &pointer_accel>; };
   ```
   Every-layer coverage: the scroll/snipe overrides REPLACE the base chain, so they
   skip `one_euro`. If you want smoothing there too, add `process-next` to those
   children (and re-check double-attenuation).

## Parameters
| Property | Default | Meaning |
|---|---|---|
| `min-cutoff-milli` | 1000 (1 Hz) | Lower = smoother at rest, more lag. |
| `beta-milli` | 7 | Higher = less lag on fast motion. Sweep {3,7,15}. |
| `d-cutoff-milli` | 1000 (1 Hz) | Derivative cutoff. |
| `ema-only` | (off) | Fixed-alpha EMA instead of full 1-Euro — the simpler A-arm. |
| `ema-alpha-milli` | 400 | EMA new-sample weight (0..1000) when `ema-only`. |

## Known limitation (from the audit)
`Te` is measured from `k_uptime` on the central, which reflects **BLE burst cadence**
(`PREF_LATENCY=10` lets the peripheral coalesce), not true sensor timing. So the
adaptive cutoff is noisier than nominal. If smoothing feels wrong, this is why —
prefer the `ema-only` arm, or move the LEFT half to USB while tuning.
