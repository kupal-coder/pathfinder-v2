// Pathfinder - the controller that turns a plan into actual button presses.
//
// This is deliberately free of GD code: it takes the *real* player state each
// frame (whoever reads it) and answers "hold or not".
//
// Control scheme: receding-horizon ("model predictive") control.
//   1. the planner searches for the best input sequence from the *live* state
//   2. that sequence is replayed through the simulator as a verification pass
//      (this is the "only click if the click survives" gate)
//   3. the bot executes step 0 of the sequence, then repeats
// Re-planning happens on a timer and immediately whenever the game state drifts
// from the prediction, so a physics mismatch can only ever cost a few frames.
#pragma once

#include <string>
#include <vector>

#include "solver/Physics.hpp"
#include "solver/Planner.hpp"
#include "solver/Types.hpp"

namespace pf {

struct BotConfig {
    PlanConfig plan;

    /// Frames of look-ahead a click must survive before the bot is willing to
    /// press. 120 frames == 2 seconds of guaranteed survival.
    int clickHorizon = 120;
    /// Re-plan at least this often (frames). The plan horizon must be larger.
    int replanEvery = 12;
    /// Re-plan immediately when the live player drifts this far (units).
    double driftToleranceX = 10.0;
    double driftToleranceY = 10.0;
    /// Stop the bot after this many frames without any progress (stuck/dead).
    int stuckFrames = 240;
    /// True = run one verification replay of the plan before trusting it.
    bool verifyPlans = true;
};

/// What the bot wants the game to do this frame.
struct Decision {
    bool hold = false;      ///< true = button should be down this frame
    bool changed = false;   ///< hold differs from the previous frame
    bool verified = false;  ///< this action came from a verified plan
    bool lastResort = false;///< no surviving plan existed, bot is guessing
    std::string reason;
};

struct BotStatus {
    bool active = false;
    bool unsupported = false;
    bool deadEnd = false;       ///< no surviving continuation from here
    bool hasPlan = false;
    int planLength = 0;         ///< frames the current plan covers
    int planCursor = 0;
    int planPresses = 0;
    int planPressesTotal = 0;
    int replans = 0;
    int vetoes = 0;
    int framesAlive = 0;
    double maxX = 0.0;
    double confidence = 0.0;    ///< 0..1 secured horizon / clickHorizon
    double lastPlanMs = 0.0;
    std::string note;
};

class AutoPlayer {
public:
    AutoPlayer() = default;

    void configure(const LevelModel* level, const PhysicsParams& physics, const BotConfig& config);

    void setEnabled(bool on);
    bool enabled() const { return enabled_; }

    /// Called once per GD physics frame with the live player state.
    Decision update(const PlayerState& real);

    /// Call when the level (re)starts, so stale plans are dropped.
    void reset();

    const BotStatus& status() const { return status_; }
    const Plan& plan() const { return plan_; }
    int planCursor() const { return cursor_; }

private:
    void invalidate();
    bool buildPlan(const PlayerState& real);
    bool verify(const PlayerState& real, const Plan& plan) const;
    bool lastResortInput(const PlayerState& real, Decision& d);

    const LevelModel* level_ = nullptr;
    PhysicsParams physics_;
    BotConfig config_;

    bool enabled_ = false;
    bool pendingReplan_ = true;
    bool predictedValid_ = false;
    bool inDeadEnd_ = false;
    int framesSincePlan_ = 0;
    int cursor_ = 0;
    int lastProgressX_ = 0;
    int framesStuck_ = 0;
    bool holding_ = false;
    PlayerState predicted_;

    Plan plan_;
    PlanResult lastResult_;
    BotStatus status_;
    Planner planner_;
};

} // namespace pf
