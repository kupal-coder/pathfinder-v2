#include "Practice.hpp"

#include <algorithm>
#include <cmath>

namespace pf {

void PracticeController::reset() {
    status_ = PracticeStatus{};
    status_.note = "practice armed";
}

void PracticeController::setHazardXs(const std::vector<double>& hazardCentres) {
    hazardXs_ = hazardCentres;
    std::sort(hazardXs_.begin(), hazardXs_.end());
}

bool PracticeController::isHazardNear(double x) const {
    if (hazardXs_.empty()) return false;
    // hazardXs_ is sorted; everything within the clearance window is a hit.
    const auto it = std::lower_bound(hazardXs_.begin(), hazardXs_.end(),
                                     x - config_.checkpointClearance);
    for (auto i = it; i != hazardXs_.end() && *i <= x + config_.checkpointClearance; ++i) {
        if (std::abs(*i - x) <= config_.checkpointClearance) return true;
    }
    return false;
}

PracticeAction PracticeController::onProgress(double x) {
    status_.bestX = std::max(status_.bestX, x);

    // A checkpoint is due when we have advanced far enough since the last one and
    // the spot is not sitting inside a hazard.
    const bool farEnough =
        x - status_.lastCheckpointX >= config_.checkpointEvery;
    if (!farEnough) return PracticeAction::None;

    // Do not checkpoint in the middle of a spike run: the bot would respawn with
    // no reaction distance.
    if (isHazardNear(x)) {
        status_.note = "checkpoint deferred (hazard nearby)";
        return PracticeAction::None;
    }

    status_.lastCheckpointX = x;
    status_.checkpointsPlaced++;
    status_.note = "checkpoint placed";
    return PracticeAction::PlaceCheckpoint;
}

PracticeAction PracticeController::onDeath(double x) {
    status_.deaths++;
    status_.deathX = x;

    // Group deaths that happen in the same place.
    const bool sameCluster =
        std::abs(x - status_.clusterX) <= config_.sameDeathRadius;
    if (!sameCluster) {
        status_.clusterX = x;
        status_.clusterDeaths = 0;
    }
    status_.clusterDeaths++;

    if (status_.givenUp) {
        // Stay idempotent, but keep reporting GiveUp so a caller that missed the
        // first one still finds out the bot is done.
        status_.note = "gave up";
        return PracticeAction::GiveUp;
    }

    if (status_.clusterDeaths < config_.sameSpotDeaths) {
        status_.note = "died at x=" + std::to_string(static_cast<int>(x)) + " (" +
                       std::to_string(status_.clusterDeaths) + "/" +
                       std::to_string(config_.sameSpotDeaths) + " before reverting)";
        return PracticeAction::None;
    }

    if (status_.reverts >= config_.maxReverts) {
        status_.givenUp = true;
        status_.note = "gave up: section still unsolved after " +
                       std::to_string(status_.reverts) + " rewinds";
        return PracticeAction::GiveUp;
    }

    // Same spot keeps killing us: go back and try again with a wider search.
    status_.note = "same spot " + std::to_string(status_.clusterDeaths) +
                   "x -> rewinding a checkpoint";
    return PracticeAction::RevertAndRetry;
}

void PracticeController::onRevertDone() {
    status_.reverts++;
    // Restart the death counter for the retried section, but keep the cluster
    // centre so a second failure at the same place is still detected.
    status_.clusterDeaths = 0;
    // The checkpoint we went back to no longer counts as progress.
    status_.lastCheckpointX = std::max(0.0, status_.lastCheckpointX - config_.checkpointEvery);
    status_.note = "rewound to checkpoint, retrying with a wider search";
}

PracticeAction PracticeController::onRevertFailed() {
    // No checkpoint to fall back to (GD keeps none until one has been placed), so
    // rewinding cannot make progress. Count the failed attempt and give up once
    // the budget is spent, instead of dying at the same spot forever.
    status_.reverts++;
    status_.clusterDeaths = 0;

    if (status_.reverts >= config_.maxReverts) {
        status_.givenUp = true;
        status_.note = "gave up: cannot rewind (no checkpoint)";
        return PracticeAction::GiveUp;
    }

    // The cluster is deliberately NOT reset: the next death at this spot should
    // trip the rewind again immediately rather than needing another N deaths.
    status_.clusterDeaths = config_.sameSpotDeaths;
    status_.note = "no checkpoint to rewind to yet";
    return PracticeAction::None;
}

int PracticeController::minFramesFor(int baseMinFrames) const {
    if (status_.reverts <= 0) return baseMinFrames;
    // Commit to the retry's full search depth instead of stopping at the first
    // surviving branch. Half the widened horizon is the practical sweet spot.
    const int target = std::max(baseMinFrames, config_.minRetryHorizon / 2);
    return std::clamp(target, baseMinFrames, config_.maxRetryHorizon);
}

int PracticeController::horizonFor(int baseHorizon) const {
    if (status_.reverts <= 0) return baseHorizon;

    double scaled = static_cast<double>(baseHorizon);
    for (int i = 0; i < status_.reverts; ++i) scaled *= config_.retryHorizonScale;

    // Jump straight to a serious search on the first retry: a multiplicative
    // crawl from a small horizon never reaches what a hard spot needs.
    const int target = std::max(static_cast<int>(scaled), config_.minRetryHorizon);
    const int upper = std::max(config_.maxRetryHorizon, baseHorizon);
    return std::clamp(target, baseHorizon, upper);
}

} // namespace pf
