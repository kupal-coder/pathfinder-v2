#include "Physics.hpp"

#include <algorithm>
#include <cmath>

namespace pf {
namespace {

constexpr double kGdSpeeds[] = {251.16, 311.58, 387.42, 468.00, 576.00};

inline double clampAbs(double v, double limit) {
    if (v > limit) return limit;
    if (v < -limit) return -limit;
    return v;
}

/// Vertical control that happens *before* gravity is integrated.
void applyControl(PlayerState& s, const PhysicsParams& p, bool hold, double gravityDir, double dx) {
    switch (s.mode) {
        case Mode::Cube:
            // Cube jumps on press *and* auto-jumps every time it touches the floor
            // while the button is held.
            if (s.onGround && hold) {
                s.vy = -gravityDir * p.jumpVelocity;
                s.onGround = false;
            }
            break;

        case Mode::Ufo: {
            const bool pressed = hold && !s.holding;
            if (pressed && (s.onGround || s.airJumps < p.ufoAirJumps)) {
                if (!s.onGround) s.airJumps++;
                s.vy = -gravityDir * p.ufoJumpVelocity;
                s.onGround = false;
            }
            break;
        }

        case Mode::Ball:
            // Every fresh press flips gravity. `gravityDir` reads
            // `gravityFlipped`, so the flip has to be undone *inside* this step,
            // otherwise the old direction moves the ball for one frame and the
            // flip is a no-op.
            if (hold && !s.holding) {
                s.gravityFlipped = !s.gravityFlipped;
                s.vy = 0.0;
                gravityDir = s.gravityFlipped ? 1.0 : -1.0;
            }
            break;

        case Mode::Robot:
            if (s.onGround) {
                if (hold) {
                    s.holdFrames++;
                } else if (s.holdFrames > 0) {
                    const double t = std::min<double>(s.holdFrames, p.robotChargeFrames) /
                                     p.robotChargeFrames;
                    s.vy = -gravityDir *
                           (p.robotMinJump + t * (p.robotJumpVelocity - p.robotMinJump));
                    s.holdFrames = 0;
                    s.onGround = false;
                }
            }
            break;

        case Mode::Wave:
            // Fixed 45 degree flight: vy mirrors dx.
            s.vy = (hold ? dx : -dx) * (s.gravityFlipped ? -1.0 : 1.0) * p.waveAngleSlope;
            break;

        default:
            break;
    }
}

/// Gravity and terminal velocity for the current mode.
void gravityFor(const PlayerState& s, const PhysicsParams& p, double& gravity, double& maxSpeed) {
    gravity = p.gravity;
    maxSpeed = p.maxFallSpeed;
    if (s.mode == Mode::Ship) {
        gravity = s.holding ? -p.shipThrust : p.shipGravity;
        maxSpeed = p.shipMaxSpeed;
    }
}

} // namespace

const char* modeName(Mode m) {
    switch (m) {
        case Mode::Cube: return "cube";
        case Mode::Ship: return "ship";
        case Mode::Ball: return "ball";
        case Mode::Ufo: return "ufo";
        case Mode::Wave: return "wave";
        case Mode::Robot: return "robot";
        case Mode::Spider: return "spider";
        case Mode::Swing: return "swing";
        default: return "unknown";
    }
}

bool isModeSupported(Mode m) {
    switch (m) {
        case Mode::Cube:
        case Mode::Ball:
        case Mode::Ufo:
        case Mode::Wave:
        case Mode::Robot:
            return true;
        default:
            // Ship / spider / swing: see the header comment.
            return false;
    }
}

bool isModeCalibrated(Mode m) {
    // Only the cube has been validated against generated levels.
    return m == Mode::Cube;
}

double speedUnitsPerSecond(int speedIndex) {
    const int i = std::clamp(speedIndex, 0, 4);
    return kGdSpeeds[i];
}

int speedIndexOf(double unitsPerSecond) {
    int best = 1;
    double bestDist = 1e18;
    for (int i = 0; i < 5; ++i) {
        const double d = std::abs(kGdSpeeds[i] - unitsPerSecond);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

double speedFromMultiplier(double multiplier) {
    // 1.0 == 1x == kGdSpeeds[1]. GD's multiplier is relative to the normal speed.
    if (!(multiplier > 0.0)) return kGdSpeeds[1];
    return kGdSpeeds[1] * multiplier;
}

StepResult step(PlayerState& s, const LevelModel& lvl, const PhysicsParams& p, bool hold) {
    StepResult res;

    const double size = s.size > 1.0 ? s.size : p.playerSize;
    const bool wasOnGround = s.onGround;
    double gravityDir = s.gravityFlipped ? 1.0 : -1.0;
    const double dx = std::max(0.0, s.speed) / p.fps;

    const double prevBottom = s.y;
    const double prevTop = s.y + size;

    // GD's player never decelerates horizontally.
    s.x += dx;

    applyControl(s, p, hold, gravityDir, dx);
    gravityDir = s.gravityFlipped ? 1.0 : -1.0; // may have been flipped above

    // GD integrates position *before* velocity (explicit Euler), which is what
    // gives the cube its characteristic ~2.3 block jump. Keep that order: doing
    // it the other way round shaves ~10 units off the apex and makes 2-block
    // walls impossible to clear.
    double gravity = p.gravity;
    double maxSpeed = p.maxFallSpeed;
    gravityFor(s, p, gravity, maxSpeed);

    const bool groundedBefore = s.onGround;
    if (s.mode == Mode::Wave) {
        s.y += s.vy; // fixed-slope flight, no gravity
    } else if (groundedBefore && s.mode != Mode::Ship) {
        s.vy = 0.0;
        // still allow the jump that applyControl() just set
        if (s.vy == 0.0) s.y += s.vy;
    } else {
        s.y += s.vy;
    }

    if (s.mode != Mode::Wave && !(groundedBefore && s.mode != Mode::Ship)) {
        s.vy = clampAbs(s.vy + gravityDir * gravity, maxSpeed);
    }

    // ---------------------------------------------------------------- resolve
    bool grounded = false;
    const double landTol = p.landTolerance;

    if (!s.gravityFlipped) {
        if (s.y <= lvl.groundY) {
            s.y = lvl.groundY;
            s.vy = 0.0;
            grounded = true;
        }
    } else if (lvl.hasCeiling) {
        if (s.y + size >= lvl.ceilingY) {
            s.y = lvl.ceilingY - size;
            s.vy = 0.0;
            grounded = true;
        }
    }

    bool crashed = false;
    const double margin = std::max(0.0, p.solidMargin);
    lvl.forSolidsInRange(s.x - 60.0, s.x + size + 60.0, [&](const Rect& raw) {
        if (crashed) return;
        // Inflate by the safety margin: if the plan only survives by shaving the
        // corner of this block, the margin turns it into a death *here*, in the
        // search, instead of a death in the game.
        const Rect b = raw.inflated(margin);
        const Rect me{s.x, s.y, s.x + size, s.y + size};
        if (!me.overlaps(b)) return;

        // Landing on top of a block.
        if (s.vy <= 0.0 && prevBottom >= b.y2 - landTol && s.y < b.y2) {
            s.y = b.y2;
            s.vy = 0.0;
            grounded = true;
            return;
        }

        // Hitting the underside: a head bonk when upright, a landing when flipped.
        if (s.vy > 0.0 && prevTop <= b.y1 + landTol && s.y + size > b.y1) {
            s.y = b.y1 - size;
            s.vy = 0.0;
            if (s.gravityFlipped) {
                grounded = true;
            } else {
                res.bonked = true;
            }
            return;
        }

        // Anything else is a side impact, which is lethal in GD.
        crashed = true;
    });

    if (crashed) {
        res.alive = false;
        return res;
    }

    // ---------------------------------------------------------------- hazards
    const Rect hazardBox = Rect{s.x, s.y, s.x + size, s.y + size}.shrunkBy(p.playerHazardShrink);
    bool dead = false;
    lvl.forHazardsInRange(hazardBox.x1 - 45.0, hazardBox.x2 + 45.0, [&](const Rect& h) {
        if (dead) return;
        if (hazardBox.overlaps(h.shrunkBy(p.hazardShrink))) dead = true;
    });

    if (dead) {
        res.alive = false;
        return res;
    }

    if (grounded) {
        s.airJumps = 0;
        // Only clear the robot's charge on an actual landing. Clearing it every
        // grounded frame made the charge reset before the button was released,
        // so the robot could never jump at all.
        if (!wasOnGround) s.holdFrames = 0;
    }

    s.onGround = grounded;
    s.holding = hold;
    res.landed = grounded;
    return res;
}

} // namespace pf
