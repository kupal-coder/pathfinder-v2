// Pathfinder - practice-mode "learn from death" loop.
//
// Geometry Dash already has everything needed to retry a section: practice mode,
// checkpoints, and a "back one checkpoint" action. This wraps that in the
// behaviour the mod wants:
//
//   * practice mode on, and the bot places its own checkpoints as it advances
//   * when the bot dies at (roughly) the same spot N times in a row, it goes back
//     one checkpoint and re-plans that section with a wider search
//   * it repeats that until the section is solved, then keeps the progress
//
// Everything here is plain C++ and takes an interface, so the death/revert policy
// is unit-tested without Geometry Dash (see tests/main.cpp).
#pragma once

#include <string>
#include <vector>

namespace pf {

/// What the policy is asking the host (the PlayLayer hook) to do.
enum class PracticeAction {
    None,
    /// Create a checkpoint at the current position and keep going.
    PlaceCheckpoint,
    /// Death repeated at the same spot: rewind to the previous checkpoint and
    /// widen the search so the section gets another, better attempt.
    RevertAndRetry,
    /// Rewind is not helping either: give up on this section and stop driving.
    GiveUp,
};

struct PracticeConfig {
    /// Deaths within `sameDeathRadius` units of each other that count as
    /// "dying to the same spot over and over".
    int sameSpotDeaths = 3;
    /// Two deaths are "the same spot" if they are this close horizontally.
    double sameDeathRadius = 90.0;
    /// Rewinds before declaring the section unsolvable.
    int maxReverts = 4;
    /// Place a checkpoint after the player has advanced this far past the last one.
    double checkpointEvery = 240.0;
    /// Never place a checkpoint closer than this to a known hazard (a checkpoint
    /// inside a spike run is worse than no checkpoint at all).
    double checkpointClearance = 60.0;
    /// Each revert multiplies the search horizon by this much (capped below).
    double retryHorizonScale = 1.5;
    /// A retry always searches at least this far ahead, regardless of how small
    /// the user's normal horizon is. Growing a 40 frame horizon by 35% per retry
    /// never reaches the horizon a hard spot actually needs.
    int minRetryHorizon = 240;
    int maxRetryHorizon = 600;
};

/// Where the bot keeps dying, and what it has tried.
struct PracticeStatus {
    int deaths = 0;
    int reverts = 0;
    int checkpointsPlaced = 0;
    double deathX = 0.0;        ///< last death position
    double clusterX = 0.0;      ///< where the current death cluster started
    int clusterDeaths = 0;
    double bestX = 0.0;         ///< furthest the bot has ever got
    double lastCheckpointX = 0.0;
    bool givenUp = false;
    std::string note;
};

class PracticeController {
public:
    void configure(const PracticeConfig& config) { config_ = config; }
    const PracticeConfig& config() const { return config_; }

    void reset();

    /// Called once the level geometry is known, so checkpoints can avoid hazards.
    void setHazardXs(const std::vector<double>& hazardCentres);

    /// The bot advanced to `x` without dying. Returns PlaceCheckpoint when a new
    /// checkpoint is due there.
    PracticeAction onProgress(double x);

    /// The player died at `x`. Returns RevertAndRetry once the same spot has
    /// killed the bot `sameSpotDeaths` times, else None.
    PracticeAction onDeath(double x);

    /// The host successfully moved back to the previous checkpoint.
    void onRevertDone();

    /// The host could not move back (no checkpoint yet, or GD refused).
    /// Returns GiveUp once the rewind budget is spent, so the caller always knows
    /// when the bot has stopped being able to make progress.
    PracticeAction onRevertFailed();

    /// Search horizon to use for the next plan (grows after each revert).
    int horizonFor(int baseHorizon) const;

    /// Minimum committed horizon for the next plan.
    ///
    /// This matters as much as the search width: the planner stops as soon as it
    /// has *any* `minFrames` long continuation, so a retry with a huge horizon but
    /// a tiny commitment threshold still commits to the first short-lived branch
    /// and dies. Retries must raise both.
    int minFramesFor(int baseMinFrames) const;

    /// True once the bot has rewound at least once (i.e. it is on a retry).
    bool onRetry() const { return status_.reverts > 0; }

    const PracticeStatus& status() const { return status_; }

private:
    bool isHazardNear(double x) const;

    PracticeConfig config_{};
    std::vector<double> hazardXs_;
    PracticeStatus status_{};
};

} // namespace pf
