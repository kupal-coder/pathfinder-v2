#include "Planner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace pf {
namespace {

/// One explored state. Small on purpose: a search layer can hold thousands.
struct Node {
    PlayerState state;
    int32_t parent = -1; ///< index in the previous layer
    uint8_t input = 0;   ///< 1 = held on the frame that produced this node
    uint16_t presses = 0;
};

std::string fmt(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return std::string(buf);
}

uint64_t hashState(const PlayerState& s, double pq, double vq) {
    const int64_t x = static_cast<int64_t>(std::llround(s.x / pq));
    const int64_t y = static_cast<int64_t>(std::llround(s.y / pq));
    const int64_t vy = static_cast<int64_t>(std::llround(s.vy / vq));
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(static_cast<uint64_t>(x));
    mix(static_cast<uint64_t>(y));
    mix(static_cast<uint64_t>(vy));
    mix(s.onGround ? 1u : 0u);
    mix(s.holding ? 2u : 0u);
    mix(s.gravityFlipped ? 4u : 0u);
    mix(static_cast<uint64_t>(s.airJumps));
    mix(static_cast<uint64_t>(s.mode));
    return h;
}

/// Walks the parent chain back to the root and materialises the plan.
Plan buildPlan(const std::vector<std::vector<Node>>& layers, int32_t layerIndex,
               int32_t nodeIndex, double startX) {
    std::vector<uint8_t> inputs;
    int32_t li = layerIndex;
    int32_t ni = nodeIndex;
    while (li > 0) {
        const Node& n = layers[static_cast<size_t>(li)][static_cast<size_t>(ni)];
        inputs.push_back(n.input);
        ni = n.parent;
        --li;
        if (ni < 0) break;
    }
    std::reverse(inputs.begin(), inputs.end());

    Plan plan;
    plan.startX = startX;
    plan.frames.reserve(inputs.size());

    int i = 0;
    const int n = static_cast<int>(inputs.size());
    while (i < n) {
        if (!inputs[static_cast<size_t>(i)]) {
            plan.frames.push_back(false);
            ++i;
            continue;
        }
        const int begin = i;
        while (i < n && inputs[static_cast<size_t>(i)]) ++i;
        plan.presses.push_back(PressEvent{begin, i - begin});
        for (int f = begin; f < i; ++f) plan.frames.push_back(true);
    }
    plan.ok = !plan.frames.empty();
    return plan;
}

/// Rough number of frames until the player next touches something. Used to
/// tell "committed to a long jump" apart from "about to land".
double framesToGround(const PlayerState& s, const LevelModel& lvl, const PhysicsParams& p) {
    if (s.onGround) return 0.0;
    const double g = p.gravity;
    if (g <= 0.0) return 0.0;
    const double dir = s.gravityFlipped ? -1.0 : 1.0;
    // Distance to travel before contact.
    const double target = s.gravityFlipped ? lvl.ceilingY - (s.y + s.size) : s.y - lvl.groundY;
    if (target <= 0.0) return 0.0;
    // y(t) = y0 + vy*t + 0.5*g*t^2  (vy positive = up)
    const double vy = s.vy;
    const double disc = vy * vy + 2.0 * g * target;
    if (disc < 0.0) return 999.0;
    const double t = (-vy + std::sqrt(disc)) / g;
    return t < 0.0 ? 0.0 : t * dir;
}

/// Higher is better. A plan that ends with the player standing on the floor is
/// worth far more than one that ends mid-jump with no options left.
double scoreEnd(const Node& n, const LevelModel& lvl, const PhysicsParams& p,
                const PlanConfig& cfg) {
    double score = 0.0;
    if (cfg.preferGroundedEnds) {
        if (n.state.onGround) {
            score += 1000.0;
        } else {
            const double t = framesToGround(n.state, lvl, p);
            score -= std::min(400.0, t * 8.0);
        }
        // Sitting right under a ceiling is a trap; a little headroom is good.
        if (lvl.hasCeiling) score += std::min(50.0, (lvl.ceilingY - n.state.y) * 0.5);
    }
    score -= cfg.pressPenalty * n.presses;
    return score;
}

} // namespace

Planner::Planner(const LevelModel* level, PhysicsParams params, PlanConfig config)
    : level_(level), params_(params), config_(config) {}

PlanResult Planner::plan(const PlayerState& start) {
    PlanResult out;
    out.maxX = start.x;

    if (!level_) {
        out.reason = "no level loaded";
        return out;
    }
    if (!isModeSupported(start.mode)) {
        out.reason = std::string("unsupported gamemode: ") + modeName(start.mode);
        return out;
    }
    if (level_->solids.empty() && level_->hazards.empty()) {
        out.reason = "level has no parsed geometry";
        return out;
    }

    const int maxFrames = std::max(2, config_.maxFrames);
    const int minFrames = std::clamp(config_.minFrames, 1, maxFrames);
    const int cap = std::max(64, config_.maxStatesPerLayer);
    const double pq = std::max(0.02, config_.posQuantum);
    const double vq = std::max(0.005, config_.velQuantum);

    // layers[i] holds every distinct state reachable exactly i frames from the
    // start, so the parent chains are already the input history.
    std::vector<std::vector<Node>> layers;
    layers.reserve(static_cast<size_t>(maxFrames) + 1);
    layers.emplace_back();
    layers.back().push_back(Node{start, -1, 0, 0});

    std::unordered_map<uint64_t, int32_t> index;
    std::vector<Node> next;

    for (int frame = 0; frame < maxFrames; ++frame) {
        const auto& cur = layers.back();
        next.clear();
        index.clear();
        index.reserve(cur.size() * 2);

        int32_t goalNode = -1;

        for (size_t i = 0; i < cur.size(); ++i) {
            // Two actions per state. The cube ignores a hold that repeats a hold,
            // so the "hold" branch is skipped when it would be a duplicate; that
            // halves the work on long jumps with no change in reachable states.
            const bool canHold = !cur[i].state.holding ||
                                 cur[i].state.mode != Mode::Cube ||
                                 cur[i].state.onGround;
            const int actions = canHold ? 2 : 1;
            for (int a = 0; a < actions; ++a) {
                const bool hold = a == 1;
                Node child = cur[i];
                child.parent = static_cast<int32_t>(i);
                child.input = static_cast<uint8_t>(hold ? 1 : 0);
                child.state.holding = cur[i].state.holding;
                if (hold && !cur[i].state.holding) child.presses++;

                const StepResult r = step(child.state, *level_, params_, hold);
                out.statesExplored++;
                if (!r.alive) continue;

                out.maxX = std::max(out.maxX, child.state.x);

                if (child.state.x >= level_->endX - config_.goalMargin) {
                    // Reaching the finish is the strongest possible result.
                    layers.push_back({child});
                    goalNode = 0;
                    break;
                }

                const uint64_t key = hashState(child.state, pq, vq);
                auto it = index.find(key);
                if (it == index.end()) {
                    index.emplace(key, static_cast<int32_t>(next.size()));
                    next.push_back(child);
                } else {
                    // Keep the variant that needed fewer presses.
                    Node& kept = next[static_cast<size_t>(it->second)];
                    if (child.presses < kept.presses) kept = child;
                }
            }
            if (goalNode >= 0) break;
        }

        if (goalNode >= 0) {
            out.success = true;
            out.plan = buildPlan(layers, static_cast<int32_t>(layers.size()) - 1, goalNode, start.x);
            out.framesSurvived = out.plan.length();
            out.reason = "reached finish";
            return out;
        }

        if (next.empty()) {
            out.reason = "frontier died at x=" + fmt(out.maxX) + " (frame " +
                         std::to_string(frame) + ")";
            out.framesSurvived = frame;
            return out;
        }

        // Safety valve: keep a deterministic, spatially spread subset.
        if (static_cast<int>(next.size()) > cap) {
            std::stable_sort(next.begin(), next.end(), [](const Node& a, const Node& b) {
                return a.state.y < b.state.y;
            });
            const int stride = static_cast<int>(next.size()) / cap + 1;
            std::vector<Node> reduced;
            reduced.reserve(static_cast<size_t>(cap));
            for (size_t i = 0; i < next.size() && static_cast<int>(reduced.size()) < cap;
                 i += static_cast<size_t>(stride)) {
                reduced.push_back(std::move(next[i]));
            }
            next.swap(reduced);
        }

        layers.push_back(std::move(next));
        next = {};

        if (frame + 1 >= minFrames) {
            // Plenty of horizon secured. Pick the safest continuation.
            const auto& layer = layers.back();
            size_t best = 0;
            double bestScore = scoreEnd(layer[0], *level_, params_, config_);
            for (size_t i = 1; i < layer.size(); ++i) {
                const double sc = scoreEnd(layer[i], *level_, params_, config_);
                if (sc > bestScore) {
                    bestScore = sc;
                    best = i;
                }
            }
            out.success = true;
            out.plan = buildPlan(layers, static_cast<int32_t>(layers.size()) - 1,
                                 static_cast<int32_t>(best), start.x);
            out.framesSurvived = out.plan.length();
            out.reason = "horizon secured (" + std::to_string(out.plan.length()) + "f, " +
                         std::to_string(countPresses(out.plan)) + " presses)";
            return out;
        }
    }

    // Horizon exhausted but the frontier is alive: still a usable path.
    const auto& layer = layers.back();
    size_t best = 0;
    double bestScore = scoreEnd(layer[0], *level_, params_, config_);
    for (size_t i = 1; i < layer.size(); ++i) {
        const double sc = scoreEnd(layer[i], *level_, params_, config_);
        if (sc > bestScore) {
            bestScore = sc;
            best = i;
        }
    }
    out.success = true;
    out.plan = buildPlan(layers, static_cast<int32_t>(layers.size()) - 1,
                         static_cast<int32_t>(best), start.x);
    out.framesSurvived = out.plan.length();
    out.reason = "max horizon reached, path survives";
    return out;
}

bool Planner::clickSurvives(const PlayerState& start, int holdFrames, int horizon) {
    if (!level_) return false;
    if (!isModeSupported(start.mode)) return false;

    const int hold = std::clamp(holdFrames, 0, std::max(1, horizon));
    PlayerState s = start;
    s.holding = false; // the click starts now

    for (int f = 0; f < horizon; ++f) {
        const StepResult r = step(s, *level_, params_, f < hold);
        if (!r.alive) return false;
    }

    // Surviving the horizon is not enough: the click must not walk into a dead
    // end either. Require that a continuation still exists afterwards.
    PlanConfig probe = config_;
    probe.maxFrames = horizon;
    probe.minFrames = horizon;
    probe.maxStatesPerLayer = std::min(config_.maxStatesPerLayer, 1500);
    Planner sub(level_, params_, probe);
    return sub.plan(s).success;
}

bool Planner::releaseSurvives(const PlayerState& start, int horizon) {
    if (!level_) return false;
    PlayerState s = start;
    s.holding = false;
    for (int f = 0; f < horizon; ++f) {
        const StepResult r = step(s, *level_, params_, false);
        if (!r.alive) return false;
    }
    return true;
}

ReplayResult replay(const PlayerState& start, const Plan& plan, const LevelModel& level,
                    const PhysicsParams& params, int maxFrames) {
    ReplayResult out;
    out.state = start;
    out.maxX = start.x;

    const int limit = maxFrames < 0 ? plan.length() : std::min(maxFrames, plan.length());
    for (int f = 0; f < limit; ++f) {
        const StepResult r = step(out.state, level, params, plan.inputAt(f));
        out.framesRun = f + 1;
        out.maxX = std::max(out.maxX, out.state.x);
        if (!r.alive) {
            out.alive = false;
            return out;
        }
    }
    return out;
}

int countPresses(const Plan& plan) {
    int presses = 0;
    bool prev = false;
    for (bool b : plan.frames) {
        if (b && !prev) presses++;
        prev = b;
    }
    return presses;
}

} // namespace pf
