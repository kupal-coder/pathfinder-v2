#include "Types.hpp"

namespace pf {

void LevelModel::finalize() {
    if (solids.empty() && hazards.empty()) {
        solidBuckets.clear();
        hazardBuckets.clear();
        return;
    }

    double minX = 1e18;
    double maxX = -1e18;
    for (const auto& r : solids) {
        minX = std::min(minX, r.x1);
        maxX = std::max(maxX, r.x2);
    }
    for (const auto& r : hazards) {
        minX = std::min(minX, r.x1);
        maxX = std::max(maxX, r.x2);
    }

    if (startX == 0.0) startX = std::min(0.0, minX);
    if (endX <= startX) endX = maxX;

    const int count = std::max(1, static_cast<int>(std::ceil((maxX - startX) / kBucketWidth)) + 2);

    auto build = [count](const std::vector<Rect>& rects, double origin,
                         std::vector<std::vector<uint32_t>>& buckets) {
        buckets.assign(static_cast<size_t>(count), {});
        for (uint32_t i = 0; i < rects.size(); ++i) {
            int b0 = static_cast<int>(std::floor((rects[i].x1 - origin) / kBucketWidth));
            int b1 = static_cast<int>(std::floor((rects[i].x2 - origin) / kBucketWidth));
            b0 = std::max(b0, 0);
            b1 = std::min(b1, count - 1);
            for (int b = b0; b <= b1; ++b) buckets[static_cast<size_t>(b)].push_back(i);
        }
    };

    build(solids, startX, solidBuckets);
    build(hazards, startX, hazardBuckets);
}

} // namespace pf
