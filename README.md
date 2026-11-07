# Pathfinder

An auto-player mod for **Geometry Dash** (Geode, GD 2.2081) that:

1. **Scans the level** — reads every object of the running `PlayLayer` and turns it into
   solid/hazard rectangles.
2. **Searches for a path** — a deterministic physics simulator explores every reachable
   input sequence for the next few seconds.
3. **Only clicks when the click survives** — a press is only issued if the simulator
   proves survival afterwards. If no press survives, the bot says so instead of guessing — and if it has no plan
at all, it hands the button back to you rather than freezing your input.

Every decision is verifiable: the plan is replayed through the simulator before it is
used, and a benchmark proves the closed loop clears levels end-to-end.

```
src/solver/   pure C++17 solver (no GD, no Geode) — simulation + search
src/bot/      receding-horizon controller — turns plans into per-frame button presses
src/geode/    the mod itself — level scan, player read, hooks, HUD
tools/        stub Geode API so the glue can be syntax-checked without the SDK
tests/        unit tests + randomised stress benchmark
demo/         browser demo of the exact solver (same algorithm, compiled to WASM)
```

## Why it isn't just "jump at spikes"

GD levels are full of things that need a *specific* jump frame, and a jump that looks
fine can leave you committed in mid-air with no way down. So the bot does not use
heuristics like "jump when a spike is 3 blocks away". It searches:

- **State**: `x, y, vy, onGround, holding, gravity, gamemode`. `x` is identical for every
  branch on a given frame (GD players never decelerate), which is exactly why ranking
  branches by "how far right" fails — it prunes the jumps you need.
- **Search**: a breadth-first wavefront over frames. Every distinct reachable state is
  kept per frame (merged at 0.5 unit / 0.05 velocity resolution), so the frontier is
  bounded by the discretised `(y, vy, flags)` space — a few thousand states even on
  dense levels. No greedy beam, no missed jump.
- **Goal**: reach the level finish, or secure a horizon and prefer plan ends where the
  player is *standing on something* (free to act) over ends that are mid-jump (committed).
- **Gate**: `clickSurvives()` re-simulates the press for `clickHorizon` frames, then
  requires that a continuation still exists afterwards. A press that walks into a dead
  end is vetoed even though it looks safe for the next two seconds.

## Measured results

`make -C tests run` — closed-loop, i.e. the bot drives the real simulator frame by frame:

```
levels generated      : 150
impossible (detected) : 68     <- a full-horizon oracle proves these cannot be beaten
beatable              : 82
survived              : 81 (98.8% of beatable)
solver failures       : 1
avg progress          : 98.9%
plan time avg/worst   : 0.000 / 0.001 ms
```

The 68 "impossible" levels are generated walls that no input sequence can clear
(e.g. a 2-block block with a spike on top). The bot reports `DEAD END` for those instead
of pretending, which is the honest failure mode.

Physics is calibrated to GD: jump apex **70.9 units (2.36 blocks)** and **~25 frames**
airtime at 1x speed, matching the real cube (2.3 blocks). That also means the earlier
"apex 59.7" version — which could not clear a 2-block wall — was wrong, and it is the
reason 2-block obstacles were failing before.

## Building the mod

```bash
# one-time
git clone https://github.com/geode-sdk/geode.git   # the SDK
export GEODE_SDK=/path/to/geode

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
geode package build Pathfinder.geode
```

Requirements: Geode 4.6+, GD 2.2081, C++23 (the SDK's own requirement). The solver and
bot sources are plain C++17 and have no GD/Geode dependency, so they also build with
`make -C tests`.

Without the SDK you can still verify the glue:

```bash
make -C tests check   # unit tests + benchmark + Geode API syntax check
```

`make -C tests check` runs, in order: the Geode API syntax check, the strict
(`-Wconversion -Wsign-conversion -Wshadow -Wpedantic`) build of the core, the unit
tests and the benchmark. The core is warning-free under those flags and clean under
ASan/UBSan.

`tools/geode-check/` declares exactly the Geode/GD API surface this mod uses
(`$modify`, `GJBaseGameLayer::update`, `PlayLayer::init/resetLevel/onQuit`,
`PlayerObject::pushButton/releaseButton`, `GameObjectType`, `getObjectRect()`, ...).
It is a compile-time guard, not a substitute for a real `geode build`.

## Using it

1. Install and enable the mod, then turn on **Auto-play** in the mod settings.
2. Start a level. The HUD (top left) shows mode, scan counts, presses, plan horizon and
   the current note.
3. Turn **Auto-play** off to take control back — the bot releases the button first so the
   cube is never left holding.

Settings worth knowing:

| Setting | Meaning |
| --- | --- |
| **Search horizon** | How far ahead the simulator looks (frames). 240 = 4 s. |
| **Secured horizon** | Stop searching once a plan this long provably survives. |
| **Click survival check** | Minimum proven survival before a press is allowed. |
| **Re-plan interval** | Re-decide from the live state this often. Lower = more responsive. |
| **State budget per frame** | CPU cap. Lower it if a brutal level causes frame drops. |
| **Verify every plan** | Replay each plan through the simulator before trusting it. |
| **Physics: gravity / jump velocity / hitbox** | Calibration knobs. See `docs/CALIBRATION.md`. |

## Supported

| | Status |
| --- | --- |
| Cube | ✅ modelled **and benchmarked** (98.8% of beatable levels cleared) |
| Ball, UFO, Wave, Robot | ⚠️ modelled from published GD constants, unit-tested, but **not** benchmarked against generated levels. The survival gate protects you; the HUD does not claim these are calibrated. |
| Ship | ⛔ refused. Its vertical control is an acceleration needing GD-specific clamping that has not been verified here — guessing is how a bot flies into a wall. |
| Spider, Swing | ⛔ refused (teleport + 2.2 swing physics are not modelled) |
| Orbs, jump pads, portals, triggers | ⛔ scanned and counted, not simulated yet (see roadmap) |

The scan reports orbs/portals/speed changes so the HUD tells you *why* a level is not
solved yet, rather than silently dying.

## Honest limitations

- **Geometry, not game logic.** Orbs, pads and portals change physics; a level relying on
  them needs the trigger/orb model (roadmap item 1). Ships/waves need the same treatment
  for their size portals.
- **Triggers.** Objects toggled by move/rotate/toggle triggers are scanned as static.
  `m_isGroupDisabled` is honoured at scan time, but live trigger changes are not re-scanned.
- **Slopes** are treated as their bounding box — conservative (may block slightly more
  than GD), never less. A slope-aware model is roadmap item 2.
- **Dual mode** is not handled: only `m_player1` is driven.
- **Ship is deliberately unsupported** (see the table above). Level parts that switch
  into a ship are refused, so the bot stops driving there and you take over.
- **Platformer mode** (`m_isPlatformer`) is not detected; the physics assumes classic
  auto-scroll.
- **"Verified survival" ≠ guaranteed.** It means "the calibrated simulator says yes for N
  frames". Read `docs/CALIBRATION.md` before trusting it on a demon.
- This is an auto-player for levels you can play yourself. Do not use it to submit
  illegitimate completions — that is what gets accounts in trouble.

## Roadmap

1. Orb and jump-pad model (yellow/pink/red/green orbs, pads) — the big one for real levels.
2. Slope-aware collision (real `CollisionObject` triangles instead of bounding boxes).
3. Trigger-aware dynamic re-scan (move/rotate/toggle).
4. Dual-mode support (`m_player2`).
5. Record/replay: dump the generated plan as a macro, and play it back.

## Licence

MIT — see `LICENSE`.
