#include "AutoPlayer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace pf {

void AutoPlayer::configure(const LevelModel* level, const PhysicsParams& physics,
                           const BotConfig& config) {
    level_ = level;
    physics_ = physics;
    config_ = config;
    planner_ = Planner(level, physics, config.plan);
    invalidate();
}

void AutoPlayer::setEnabled(bool on) {
    if (enabled_ == on) return;
    enabled_ = on;
    status_.active = on;
    invalidate();
    holding_ = false;
    plan_ = Plan{};
    status_.hasPlan = false;
    status_.note = on ? "searching" : "idle";
}

void AutoPlayer::reset() {
    status_ = BotStatus{};
    status_.active = enabled_;
    holding_ = false;
    plan_ = Plan{};
    lastResult_ = PlanResult{};
    framesStuck_ = 0;
    lastProgressX_ = 0;
    invalidate();
}

void AutoPlayer::invalidate() {
    pendingReplan_ = true;
    framesSincePlan_ = 0;
    cursor_ = 0;
    predictedValid_ = false;
}

/// Replays the whole plan against the simulator. This is the safety gate: a plan
/// the bot acts on has survived every frame of its own horizon, verified twice
/// (once by the search, once here from the live state).
bool AutoPlayer::verify(const PlayerState& real, const Plan& plan) const {
    if (!config_.verifyPlans) return true;
    if (!level_ || plan.length() == 0) return false;
    const ReplayResult r = replay(real, plan, *level_, physics_);
    return r.alive && r.framesRun == plan.length();
}

bool AutoPlayer::buildPlan(const PlayerState& real) {
    const auto t0 = std::chrono::steady_clock::now();
    lastResult_ = planner_.plan(real);
    const auto t1 = std::chrono::steady_clock::now();
    status_.lastPlanMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    status_.replans++;

    if (!lastResult_.success || !verify(real, lastResult_.plan)) {
        lastResult_.success = false;
        plan_ = Plan{};
        status_.hasPlan = false;
        status_.planLength = 0;
        status_.confidence = 0.0;
        status_.deadEnd = true;
        status_.note = "no surviving plan: " + lastResult_.reason;
        return false;
    }

    plan_ = lastResult_.plan;
    status_.hasPlan = true;
    status_.deadEnd = false;
    status_.planLength = plan_.length();
    status_.planPresses = countPresses(plan_);
    status_.confidence =
        std::min(1.0, plan_.length() / static_cast<double>(std::max(1, config_.clickHorizon)));
    status_.note = lastResult_.reason;
    cursor_ = 0;
    framesSincePlan_ = 0;
    pendingReplan_ = false;
    return true;
}

/// No full plan exists. Fall back to asking the survival gate directly:
/// press if pressing survives, otherwise press anyway (coasting to certain death
/// is never better than trying).
bool AutoPlayer::lastResortInput(const PlayerState& real, Decision& d) {
    const int horizon = std::max(config_.clickHorizon, 60);
    if (planner_.clickSurvives(real, 3, horizon)) {
        d.lastResort = true;
        d.reason = "no plan; unverified jump";
        return true;
    }
    d.lastResort = true;
    d.reason = "dead end; pressing anyway";
    return true;
}

Decision AutoPlayer::update(const PlayerState& real) {
    Decision d;
    status_.framesAlive++;
    status_.maxX = std::max(status_.maxX, real.x);

    if (!enabled_ || !level_) {
        d.reason = "disabled";
        return d;
    }
    if (!isModeSupported(real.mode)) {
        status_.unsupported = true;
        status_.note = std::string("unsupported gamemode: ") + modeName(real.mode);
        d.reason = "unsupported gamemode";
        d.hold = holding_ = false;
        return d;
    }
    status_.unsupported = false;

    // --- stuck detection ---------------------------------------------------
    // No forward progress at all is the definition of stuck. (The previous
    // version only counted while airborne, so being pinned against a wall on the
    // floor - the actual stuck case - was never caught.)
    if (static_cast<int>(real.x) > lastProgressX_) {
        lastProgressX_ = static_cast<int>(real.x);
        framesStuck_ = 0;
    } else {
        framesStuck_++;
    }

    // --- is the current plan still trustworthy? ----------------------------
    if (predictedValid_) {
        const double dx = std::abs(predicted_.x - real.x);
        const double dy = std::abs(predicted_.y - real.y);
        const bool vFlip = predicted_.gravityFlipped != real.gravityFlipped;
        if (dx > config_.driftToleranceX || dy > config_.driftToleranceY || vFlip) {
            pendingReplan_ = true;
            status_.note = "drift -> replan";
        }
    }
    if (framesSincePlan_ >= config_.replanEvery) pendingReplan_ = true;
    if (status_.hasPlan && cursor_ >= plan_.length()) pendingReplan_ = true;
    if (!status_.hasPlan) pendingReplan_ = true;

    if (pendingReplan_) buildPlan(real);

    // --- decide this frame's input ----------------------------------------
    bool desired;
    if (status_.hasPlan) {
        // The survival gate already ran: buildPlan() only publishes a plan after
        // replaying it through the simulator for its whole length from this live
        // state. So any press this frame comes from a proven-surviving sequence.
        desired = plan_.inputAt(cursor_);
        d.verified = true;
    } else {
        desired = lastResortInput(real, d);
        status_.deadEnd = true;
    }

    d.hold = desired;
    d.changed = desired != holding_;
    if (d.changed && desired) status_.planPressesTotal++;
    holding_ = desired;
    if (d.reason.empty()) {
        if (d.changed) d.reason = desired ? "press" : "release";
        else d.reason = desired ? "hold" : "coast";
    }

    // --- one-frame shadow simulation (drift detection) ---------------------
    predicted_ = real;
    const StepResult r = step(predicted_, *level_, physics_, desired);
    predictedValid_ = r.alive;

    if (status_.hasPlan) cursor_++;
    framesSincePlan_++;
    status_.planCursor = cursor_;

    if (framesStuck_ > config_.stuckFrames) {
        status_.note = "stuck (no forward progress)";
    }

    return d;
}

} // namespace pf
