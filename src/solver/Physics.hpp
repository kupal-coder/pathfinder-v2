// Pathfinder - deterministic player simulation.
//
// Geometry Dash is deterministic, so a plan can be validated by simply re-running
// it. The constants below reproduce GD's cube physics closely enough that a path
// which survives the simulation survives the game (see README "Calibration").
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
bool isModeSupported(Mode m);

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
    double landTolerance = 8.0;     ///< vertical slack that still counts as a landing

    // --- hazards ----------------------------------------------------------
    double hazardShrink = 0.30;        ///< hazards are smaller than their sprite
    double playerHazardShrink = 0.20;  ///< the death hitbox is smaller than the solid one

    // --- ship -------------------------------------------------------------
    double shipGravity = 0.60;
    double shipThrust = 1.10;
    double shipMaxSpeed = 9.0;

    // --- ufo --------------------------------------------------------------
    double ufoJumpVelocity = 10.4;
    int ufoAirJumps = 2;

    // --- wave -------------------------------------------------------------
    // (the wave moves at +-dx per frame, i.e. exactly 45 degrees)

    // --- robot ------------------------------------------------------------
    double robotMinJump = 5.5;
    double robotJumpVelocity = 11.18;
    double robotChargeFrames = 22.0;
};

/// X speed in units/second for GD speed portals (0 = 0.5x ... 4 = 4x).
double speedUnitsPerSecond(int speedIndex);
int speedIndexOf(double unitsPerSecond);

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
