// Procedural level generator used by the benchmark.
#pragma once

#include <random>

#include "LevelBuilder.hpp"

namespace pf {

struct GenConfig {
    double length = 3000.0;
    int speedIndex = 1;
    double spikeChance = 0.45;
    double blockChance = 0.35;
    double sawChance = 0.20;
    bool allowCeiling = true;
    /// Smallest gap between obstacles. Small gaps can produce levels that are
    /// literally impossible to clear - the benchmark detects those separately.
    int minGap = 110;
    int maxGap = 260;
};

inline LevelModel generateLevel(std::mt19937& rng, const GenConfig& gc) {
    LevelBuilder b;
    b.ground(0).start(0).end(gc.length);
    if (gc.allowCeiling && std::uniform_real_distribution<double>(0, 1)(rng) < 0.3) b.ceiling(330);

    std::uniform_real_distribution<double> chance(0, 1);
    std::uniform_int_distribution<int> gap(gc.minGap, gc.maxGap);

    double x = 300.0;
    while (x < gc.length - 200) {
        const double roll = chance(rng);
        if (roll < gc.spikeChance) {
            const int n = std::uniform_int_distribution<int>(1, 3)(rng);
            b.spikes(x, n);
        } else if (roll < gc.spikeChance + gc.blockChance) {
            const int h = std::uniform_int_distribution<int>(1, 2)(rng) * 30;
            b.block(x, 0, 60, h);
            if (chance(rng) < 0.5) b.spike(x + 15, h);
        } else if (roll < gc.spikeChance + gc.blockChance + gc.sawChance) {
            b.saw(x, 30.0, 30.0);
        }
        x += gap(rng);
    }
    return b.take();
}

} // namespace pf
