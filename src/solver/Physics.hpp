// Pathfinder - deterministic player simulation.
//
// Geometry Dash is deterministic, so a plan can be validated by simply re-running
// it. The constants below reproduce GD's cube physics closely enough that a path
// which survives the simulation survives the game (see docs/CALIBRATION.md).
#pragma once

#include "Types.hpp"

namespace pf {

enum class Mode : uint8_t {
    Cube = 0,
    Ship,
    Ball,
    Ufo,
    Wave,
    Robot,
    Spider,
    Swing,
    Unknown,
};

const char* modeName(Mode m);

/// Modes this simulation actually models well enough to command.
///
/// Cube: benchmarked end-to-end against generated levels (see tests/bench.cpp).
/// Ball / UFO / Robot / Wave: modelled from the published GD constants but *not*
///   benchmarked, so the mod drives them while the survival gate protects you.
/// Ship: deliberately unsupported. Its vertical control is an acceleration that
///   needs GD-specific clamping that has not been verified here, and a ship guess
///   is how a bot flies straight into a wall. Refusing is the honest answer.
/// Spider / Swing: unsupported (teleport + 2.2 swing physics are not modelled).
bool isModeSupported(Mode m);

/// True when the mode has been validated against generated levels, as opposed to
/// "modelled but uncalibrated". Used to warn in the HUD.
bool isModeCalibrated(Mode m);

/// Tunables of the simulation. Every number is in GD level units and "frames"
/// are solver frames at `fps` (GD's physics tick is 60 Hz).
struct PhysicsParams {
    double fps = 60.0;
    double unitsPerBlock = 30.0;
    double playerSize = 30.0;

    // --- cube -------------------------------------------------------------
    double gravity = 0.958199;      ///< units / frame^2
    double jumpVelocity = 11.180032;///< units / frame
    double maxFallSpeed = 15.0;     ///< terminal velocity, units / frame
    /// Vertical slack that still counts as a landing.
    ///
    /// This used to be 8.0, which let the simulation "ledge grab" a wall corner
    /// up to 8 units below the top: a 2-block wall looked clearable in the
    /// simulation and then killed the player in game. GD's own landing tolerance
    /// is about one unit, so keep this small.
    double landTolerance = 1.5;
    /// Solids are inflated by this much for collision purposes (pessimistic).
    ///
    /// Plans must never rely on shaving a corner, because a few units of mismatch
    /// between this model and GD is exactly what turns "2 blocks, cleared" into a
    /// death. Planning against a slightly bigger world keeps a safety margin.
    double solidMargin = 0.75;

    // --- hazards ----------------------------------------------------------
    double hazardShrink = 0.30;        ///< hazards are smaller than their sprite
    double playerHazardShrink = 0.20;  ///< the death hitbox is smaller than the solid one

    // --- ship -------------------------------------------------------------
    // Kept for reference only: ships are refused by isModeSupported() until the
    // vertical control has been calibrated. See docs/CALIBRATION.md.
    double shipGravity = 0.60;
    double shipThrust = 1.10;
    double shipMaxSpeed = 9.0;

    // --- ufo --------------------------------------------------------------
    double ufoJumpVelocity = 10.4;
    int ufoAirJumps = 2;

    // --- wave -------------------------------------------------------------
    /// The wave moves at +-dx per frame, i.e. exactly 45 degrees. No gravity.
    double waveAngleSlope = 1.0;

    // --- robot ------------------------------------------------------------
    double robotMinJump = 5.5;
    double robotJumpVelocity = 11.18;
    double robotChargeFrames = 22.0;
};

/// X speed in units/second for GD speed portals (0 = 0.5x ... 4 = 4x).
double speedUnitsPerSecond(int speedIndex);
int speedIndexOf(double unitsPerSecond);
/// Turns GD's speed multiplier (1.0 == 1x) into units/second.
double speedFromMultiplier(double multiplier);

/// Everything the solver needs to know about the player at a given instant.
struct PlayerState {
    double x = 0.0;   ///< left edge of the hitbox
    double y = 0.0;   ///< bottom edge of the hitbox
    double vy = 0.0;  ///< vertical velocity, units/frame (negative = falling)
    double speed = 311.58; ///< horizontal speed, units/second
    double size = 30.0;    ///< hitbox height/width
    bool onGround = true;
    bool holding = false;
    bool gravityFlipped = false;
    Mode mode = Mode::Cube;
    int holdFrames = 0; ///< how long the button has been held (robot charge)
    int airJumps = 0;   ///< ufo jumps used since leaving the floor
};

struct StepResult {
    bool alive = true;
    bool landed = false;
    bool bonked = false; ///< head hit a ceiling / block underside
};

/// Advances the state by exactly one frame with the given input.
/// Returns `alive == false` when the move is lethal.
StepResult step(PlayerState& s, const LevelModel& lvl, const PhysicsParams& p, bool hold);

} // namespace pf
