// Pathfinder - the search itself.
//
// Given a LevelModel and a player state, the planner explores every input
// sequence that is reachable within a horizon using the deterministic simulator,
// merging states that are physically equivalent. Because the simulator is exact,
// "this path survives the horizon" is the same statement as "this click survives".
//
// Why a wavefront (= breadth-first over frames) and not a greedy beam:
// the horizontal position of the player is identical for every branch on a given
// frame (GD players never decelerate), so ranking branches by "how far right"
// prunes exactly the jumps you need. Instead we keep *every* distinct reachable
// state per frame, which is a small set: after merging, the frontier is bounded
// by the discretised (y, vy, flags) space - a few thousand states even on dense
// levels. That makes the search complete for the discretisation it uses.
#pragma once

#include <string>
#include <vector>

#include "Physics.hpp"
#include "Types.hpp"

namespace pf {

/// One click: press at `frame`, release `holdFrames` later (-1 = hold forever).
struct PressEvent {
    int frame = 0;
    int holdFrames = 1;
};

/// A concrete input sequence starting from the state the planner was called with.
struct Plan {
    std::vector<PressEvent> presses;
    /// Per-frame ground truth: frames[i] == true means "hold on frame i".
    std::vector<bool> frames;
    double startX = 0.0;
    bool ok = false;

    bool inputAt(int frame) const {
        if (frame < 0 || frame >= static_cast<int>(frames.size())) return false;
        return frames[static_cast<size_t>(frame)];
    }

    int length() const { return static_cast<int>(frames.size()); }
};

struct PlanConfig {
    /// Hard cap on how far ahead the search looks (frames).
    int maxFrames = 240;
    /// Once this many frames are secured the search may stop early.
    int minFrames = 90;
    /// Maximum distinct states kept per frame. Dense levels stay well below this;
    /// it only exists so a pathological level cannot allocate without bound.
    int maxStatesPerLayer = 4000;
    /// Treat this distance past the level end as "done".
    double goalMargin = 45.0;
    /// When several equally long ends exist, prefer ends the player can act
    /// from (standing on something) over ends that are mid-jump and committed.
    bool preferGroundedEnds = true;
    /// How much a single extra press is penalised when scoring plan ends.
    double pressPenalty = 25.0;

    /// State-merging resolution (level units). Finer = fewer mistakes, more states.
    double posQuantum = 0.5;
    double velQuantum = 0.05;
};

struct PlanResult {
    bool success = false;
    Plan plan;
    double maxX = 0.0;      ///< furthest X any candidate reached
    int framesSurvived = 0;
    int statesExplored = 0;
    std::string reason;     ///< filled in on failure ("frontier died at x=..." etc.)
};

class Planner {
public:
    Planner() = default;
    Planner(const LevelModel* level, PhysicsParams params, PlanConfig config = {});

    void setLevel(const LevelModel* level) { level_ = level; }
    void setParams(const PhysicsParams& params) { params_ = params; }
    void setConfig(const PlanConfig& config) { config_ = config; }
    const PhysicsParams& params() const { return params_; }
    const PlanConfig& config() const { return config_; }

    /// Searches for an input sequence of at least `minFrames` frames that keeps
    /// the player alive (and, if possible, reaches the level finish).
    PlanResult plan(const PlayerState& start);

    /// The gate the mod uses before pressing. `holdFrames` is how long the click
    /// would be held. Returns true only if some continuation of that click
    /// survives `horizon` frames.
    bool clickSurvives(const PlayerState& start, int holdFrames, int horizon);

    /// True if coasting (no input) for `horizon` frames is survivable.
    bool releaseSurvives(const PlayerState& start, int horizon);

private:
    const LevelModel* level_ = nullptr;
    PhysicsParams params_;
    PlanConfig config_;
};

/// Rolls a plan forward from `start` and reports how it went.
struct ReplayResult {
    bool alive = true;
    int framesRun = 0;
    double maxX = 0.0;
    PlayerState state;
};

ReplayResult replay(const PlayerState& start, const Plan& plan, const LevelModel& level,
                    const PhysicsParams& params, int maxFrames = -1);

/// Counts presses in a plan (fewer presses == less to go wrong).
int countPresses(const Plan& plan);

} // namespace pf
