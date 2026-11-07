# Physics calibration

The whole approach rests on one claim: **the simulator matches GD closely enough that a
path which survives in simulation survives in game.** This file is the audit trail for
that claim, plus instructions to re-check it after a GD update.

## Where the constants come from

`PhysicsParams` in `src/solver/Physics.hpp`, all units are GD level units (30 units = one
block) and 60 Hz frames.

| Constant | Value | Source |
| --- | --- | --- |
| Gravity | `0.958199` u/frame² | GD decompilation (widely published value) |
| Cube jump velocity | `11.180032` u/frame | same |
| Terminal fall speed | `15.0` u/frame | same |
| Speeds 0..4 | `251.16 / 311.58 / 387.42 / 468.00 / 576.00` u/s | GD speed portal values |
| Player hitbox | `1.0` block | GD (hazard hitbox is smaller, see below) |
| Hazard shrink | `0.30` | spikes/saws are smaller than their sprite |
| Player hazard shrink | `0.20` | GD's death hitbox is inset |

## Integration order matters (this was a real bug)

GD advances **position first, then velocity** (explicit Euler). Doing it the other way
round shaves ~10 units off the jump apex and makes 2-block walls unjumpable:

| Integration order | Apex | Clears 2 blocks? |
| --- | --- | --- |
| velocity → position | 59.7 u (1.99 blocks) | ❌ no |
| position → velocity (GD) | **70.9 u (2.36 blocks)** | ✅ yes |

The benchmark caught this: levels with 2-block obstacles were reported as "impossible".
If a physics tweak suddenly makes simple levels unsolvable, check this order first.

## Ground-truth checks in `tests/main.cpp`

- flat ground at 1x: moves `311.6` units in 60 frames (expected 311.6)
- jump apex `70.9` units, airtime `25` frames, lands back on the floor
- coasting into a spike **dies** (otherwise the other tests prove nothing)
- a click at `x=0` is vetoed with a 240-frame horizon because it walks into a dead end;
  the same click just before the spike is allowed

## Calibrating against a new GD version

The mod exposes the three numbers people actually need to retune, as settings:

- `gravity`
- `jump-velocity`
- `hitbox-blocks` (hitbox size in blocks; `1.0` is stock)

Procedure:

1. Enable the HUD and play a flat, empty level. Note where the cube lands after a jump.
2. Adjust `jump-velocity` in `0.01` steps until the landing frame matches.
3. For ship/wave, compare the vertical drift over 60 frames while holding / not holding.
   Those constants live in `PhysicsParams` and would need recompiling.

If you find a systematic mismatch, the honest fix is to change the constant in
`PhysicsParams` and re-run `make -C tests run` — the benchmark is randomised, so a broken
constant shows up immediately as a survival-rate collapse.

## What "verified" means (and does not mean)

`Planner::clickSurvives()` presses the button in simulation for `holdFrames`, then runs a
second search from the end state. Both must succeed. `AutoPlayer::verify()` additionally
replays the complete plan from the live state before it is used.

So a green HUD means: *the calibrated model says this input survives `N` frames and does
not lead into a state with no continuation.*

It does **not** mean:
- GD's real numbers match the model for every gamemode (ships/waves are approximations),
- unrescanned triggers will not change the level under you,
- dual mode, orbs, pads and portals are accounted for (they are not yet).

That is why unsupported gamemodes (spider, swing) are refused outright instead of driven
optimistically.
