// Pathfinder - level/solver types.
// This header is intentionally free of any Geometry Dash / Geode dependency so the
// solver can be unit-tested on any machine (see tests/).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pf {

/// Axis aligned rectangle, in level units (30 units == 1 Geometry Dash block).
struct Rect {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;

    static Rect fromCorners(double ax, double ay, double bx, double by) {
        Rect r;
        r.x1 = std::min(ax, bx);
        r.y1 = std::min(ay, by);
        r.x2 = std::max(ax, bx);
        r.y2 = std::max(ay, by);
        return r;
    }

    static Rect fromPosSize(double x, double y, double w, double h) {
        return Rect{x, y, x + std::max(0.0, w), y + std::max(0.0, h)};
    }

    double width() const { return x2 - x1; }
    double height() const { return y2 - y1; }
    double centerX() const { return (x1 + x2) * 0.5; }
    double centerY() const { return (y1 + y2) * 0.5; }

    bool overlaps(const Rect& o) const {
        return x1 < o.x2 && x2 > o.x1 && y1 < o.y2 && y2 > o.y1;
    }

    bool contains(double x, double y) const {
        return x >= x1 && x <= x2 && y >= y1 && y <= y2;
    }

    Rect inflated(double margin) const {
        return Rect{x1 - margin, y1 - margin, x2 + margin, y2 + margin};
    }

    /// Shrinks both axes by `fraction` of their size (0.3 -> 30% smaller in total).
    Rect shrunkBy(double fraction) const {
        const double dx = width() * 0.5 * fraction;
        const double dy = height() * 0.5 * fraction;
        return Rect{x1 + dx, y1 + dy, x2 - dx, y2 - dy};
    }
};

/// The subset of a level the solver cares about: lethal rects and blocking rects.
///
/// Objects are bucketed along X so a frame step only tests the handful of rects
/// near the player instead of the whole level (levels routinely have 10k+ objects).
struct LevelModel {
    std::vector<Rect> solids;   ///< blocks: you stand on them, side contact kills
    std::vector<Rect> hazards;  ///< spikes, saws, ...: any contact kills

    double startX = 0.0;
    double endX = 0.0;      ///< level finish in X (goal for the solver)
    double groundY = 0.0;   ///< Y of the ground surface
    bool hasCeiling = false;
    double ceilingY = 0.0;

    static constexpr double kBucketWidth = 150.0;

    std::vector<std::vector<uint32_t>> solidBuckets;
    std::vector<std::vector<uint32_t>> hazardBuckets;

    bool empty() const { return solids.empty() && hazards.empty(); }

    void clear() {
        solids.clear();
        hazards.clear();
        solidBuckets.clear();
        hazardBuckets.clear();
        startX = 0.0;
        endX = 0.0;
        groundY = 0.0;
        hasCeiling = false;
        ceilingY = 0.0;
    }

    /// Builds the X buckets and derives startX/endX if they were left at 0.
    void finalize();

    int bucketIndex(double x) const {
        return static_cast<int>(std::floor((x - startX) / kBucketWidth));
    }

    template <class Fn>
    void forEither(const std::vector<Rect>& rects,
                   const std::vector<std::vector<uint32_t>>& buckets,
                   double x1, double x2, Fn&& fn) const {
        if (buckets.empty()) {
            for (const auto& r : rects) {
                if (r.x2 >= x1 && r.x1 <= x2) fn(r);
            }
            return;
        }
        int b0 = bucketIndex(x1) - 1;
        int b1 = bucketIndex(x2) + 1;
        b0 = std::max(b0, 0);
        b1 = std::min<int>(b1, static_cast<int>(buckets.size()) - 1);
        for (int b = b0; b <= b1; ++b) {
            for (uint32_t i : buckets[static_cast<size_t>(b)]) {
                const Rect& r = rects[i];
                if (r.x2 >= x1 && r.x1 <= x2) fn(r);
            }
        }
    }

    template <class Fn>
    void forSolidsInRange(double x1, double x2, Fn&& fn) const {
        forEither(solids, solidBuckets, x1, x2, fn);
    }

    template <class Fn>
    void forHazardsInRange(double x1, double x2, Fn&& fn) const {
        forEither(hazards, hazardBuckets, x1, x2, fn);
    }
};

} // namespace pf
