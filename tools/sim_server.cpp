// Headless closed-loop runner used by the browser demo.
//
// Generates a level from a seed + difficulty, then lets AutoPlayer drive it
// frame by frame against the same simulator the mod uses, recording every frame.
// Output is JSON, so the demo page is a pure renderer - the physics and the
// search in the browser are literally this binary's output.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "LevelBuilder.hpp"
#include "LevelGen.hpp"
#include "bot/AutoPlayer.hpp"
#include "solver/Planner.hpp"

using namespace pf;

namespace {

struct Frame {
    float x, y, vy;
    bool hold;
    bool onGround;
    bool deadEnd;
    int planFrames;
    int cursor;
    int presses;
};

std::string num(double v, int decimals = 2) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return std::string(buf);
}

/// Escape a string for JSON (only used for the HUD note).
std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

struct Options {
    unsigned seed = 1;
    int difficulty = 2; // 1..5
    bool showPlans = true;
};

LevelModel buildLevel(const Options& opt, GenConfig& outGen) {
    std::mt19937 rng(opt.seed * 2654435761u + static_cast<unsigned>(opt.difficulty) * 7919u);

    // Difficulty 1..5 drives density and gap size. Gaps stay above the maximum
    // jump distance so generated levels are mostly beatable.
    const double density = 0.20 + 0.09 * (opt.difficulty - 1);
    outGen.length = 2400.0 + 500.0 * (opt.difficulty - 1);
    outGen.spikeChance = density;
    outGen.blockChance = 0.30;
    outGen.sawChance = 0.12;
    outGen.allowCeiling = opt.difficulty >= 4;
    outGen.minGap = 150 - 6 * opt.difficulty;
    outGen.maxGap = 300 - 10 * opt.difficulty;
    return generateLevel(rng, outGen);
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) opt.seed = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (arg == "--difficulty" && i + 1 < argc) opt.difficulty = std::atoi(argv[++i]);
    }
    if (opt.difficulty < 1) opt.difficulty = 1;
    if (opt.difficulty > 5) opt.difficulty = 5;

    GenConfig gen;
    const LevelModel lvl = buildLevel(opt, gen);

    PhysicsParams phys;
    BotConfig cfg;
    cfg.plan.maxFrames = 240;
    cfg.plan.minFrames = 120;
    cfg.clickHorizon = 120;
    cfg.replanEvery = 6;

    AutoPlayer bot;
    bot.configure(&lvl, phys, cfg);
    bot.setEnabled(true);

    PlayerState s;
    s.size = phys.playerSize;
    s.speed = speedUnitsPerSecond(gen.speedIndex);

    std::vector<Frame> frames;
    frames.reserve(1200);
    bool alive = true;
    double maxPlanMs = 0.0;
    std::string note;

    const int maxFrames = 6000;
    for (int f = 0; f < maxFrames; ++f) {
        const Decision d = bot.update(s);
        const BotStatus& st = bot.status();
        maxPlanMs = std::max(maxPlanMs, st.lastPlanMs);
        note = st.note;

        frames.push_back(Frame{static_cast<float>(s.x), static_cast<float>(s.y),
                               static_cast<float>(s.vy), d.hold, s.onGround, st.deadEnd,
                               st.planLength, st.planCursor, st.planPressesTotal});

        const StepResult r = step(s, lvl, phys, d.hold);
        if (!r.alive) {
            alive = false;
            break;
        }
        if (s.x >= lvl.endX - 30.0) break;
    }

    const BotStatus& st = bot.status();

    // ------------------------------------------------------------------ JSON
    std::printf("{\n");
    std::printf("  \"seed\": %u,\n", opt.seed);
    std::printf("  \"difficulty\": %d,\n", opt.difficulty);
    std::printf("  \"alive\": %s,\n", alive ? "true" : "false");
    std::printf("  \"finished\": %s,\n", st.maxX >= lvl.endX - 60.0 ? "true" : "false");
    std::printf("  \"endX\": %s,\n", num(lvl.endX).c_str());
    std::printf("  \"progress\": %s,\n", num(std::min(1.0, st.maxX / lvl.endX), 3).c_str());
    std::printf("  \"presses\": %d,\n", st.planPressesTotal);
    std::printf("  \"replans\": %d,\n", st.replans);
    std::printf("  \"maxPlanMs\": %s,\n", num(maxPlanMs, 3).c_str());
    std::printf("  \"note\": \"%s\",\n", jsonEscape(note).c_str());

    std::printf("  \"solids\": [");
    for (size_t i = 0; i < lvl.solids.size(); ++i) {
        const auto& r = lvl.solids[i];
        if (r.y2 <= lvl.groundY + 0.5f && r.y1 < lvl.groundY) continue; // skip the floor
        std::printf("%s[%s,%s,%s,%s]", i ? "," : "", num(r.x1).c_str(), num(r.y1).c_str(),
                    num(r.width()).c_str(), num(r.height()).c_str());
    }
    std::printf("],\n");

    std::printf("  \"hazards\": [");
    for (size_t i = 0; i < lvl.hazards.size(); ++i) {
        const auto& r = lvl.hazards[i];
        std::printf("%s[%s,%s,%s,%s]", i ? "," : "", num(r.x1).c_str(), num(r.y1).c_str(),
                    num(r.width()).c_str(), num(r.height()).c_str());
    }
    std::printf("],\n");

    std::printf("  \"frames\": [");
    for (size_t i = 0; i < frames.size(); ++i) {
        const Frame& fr = frames[i];
        std::printf("%s[%s,%s,%s,%d,%d,%d,%d,%d,%d]", i ? "," : "", num(fr.x).c_str(),
                    num(fr.y).c_str(), num(fr.vy).c_str(), fr.hold ? 1 : 0,
                    fr.onGround ? 1 : 0, fr.deadEnd ? 1 : 0, fr.planFrames, fr.cursor,
                    fr.presses);
    }
    std::printf("]\n}\n");
    return 0;
}
