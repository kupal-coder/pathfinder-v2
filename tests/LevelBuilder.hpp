// Helpers for building synthetic levels in unit tests / the CLI demo.
#pragma once

#include <cmath>
#include <string>

#include "solver/Types.hpp"

namespace pf {

/// Small fluent builder. Distances are in GD units (30 = 1 block).
class LevelBuilder {
public:
    LevelBuilder& ground(double y = 0.0) {
        level_.groundY = y;
        return *this;
    }

    LevelBuilder& ceiling(double y) {
        level_.hasCeiling = true;
        level_.ceilingY = y;
        return *this;
    }

    LevelBuilder& start(double x) {
        level_.startX = x;
        return *this;
    }

    LevelBuilder& end(double x) {
        level_.endX = x;
        return *this;
    }

    LevelBuilder& block(double x, double y, double w = 30.0, double h = 30.0) {
        level_.solids.push_back(Rect::fromPosSize(x, y, w, h));
        return *this;
    }

    LevelBuilder& spike(double x, double y = 0.0, double size = 30.0) {
        // Spikes are triangles; the solid part is the lower middle of the cell.
        level_.hazards.push_back(Rect::fromPosSize(x + size * 0.22, y, size * 0.56, size * 0.86));
        return *this;
    }

    LevelBuilder& spikes(double x, int count, double y = 0.0) {
        for (int i = 0; i < count; ++i) spike(x + i * 30.0, y);
        return *this;
    }

    LevelBuilder& saw(double x, double y, double size = 30.0) {
        level_.hazards.push_back(Rect::fromPosSize(x, y, size, size));
        return *this;
    }

    LevelBuilder& build() {
        level_.finalize();
        return *this;
    }

    LevelModel take() {
        level_.finalize();
        return level_;
    }

    const LevelModel& level() const { return level_; }

private:
    LevelModel level_;
};

} // namespace pf
