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
#include "bot/Practice.hpp"
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
    int horizon = 240;  // search width; small values make the practice loop kick in
    int minFrames = 120;
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
        else if (arg == "--horizon" && i + 1 < argc) opt.horizon = std::atoi(argv[++i]);
        else if (arg == "--min-frames" && i + 1 < argc) opt.minFrames = std::atoi(argv[++i]);
    }
    if (opt.difficulty < 1) opt.difficulty = 1;
    if (opt.difficulty > 5) opt.difficulty = 5;

    GenConfig gen;
    const LevelModel lvl = buildLevel(opt, gen);

    PhysicsParams phys;
    // ------------------------------------------------------------------ play
    // Runs the real practice loop: die, die, die at the same spot, rewind, retry
    // with a wider search, keep going. The frames of every attempt are emitted so
    // the viewer can show the rewind happening.
    PracticeConfig pcfg;
    pcfg.sameSpotDeaths = 3;
    pcfg.maxReverts = 5;
    PracticeController pc;
    pc.configure(pcfg);
    pc.reset();

    std::vector<Frame> frames;
    frames.reserve(4000);
    std::vector<int> attemptStarts;
    std::vector<double> attemptDeaths;
    bool alive = true;
    bool finished = false;
    double lastDeathX = 0.0;
    double maxPlanMs = 0.0;
    double progressX = 0.0;
    int totalPresses = 0;
    int totalReplans = 0;
    std::string note;

    int baseHorizon = opt.horizon;
    for (int attempt = 0; attempt < 8; ++attempt) {
        BotConfig cfg;
        cfg.plan.maxFrames = pc.horizonFor(baseHorizon);
        cfg.plan.minFrames = std::min(cfg.plan.maxFrames, pc.minFramesFor(opt.minFrames));
        cfg.clickHorizon = std::min(120, cfg.plan.maxFrames);
        cfg.replanEvery = 6;

        AutoPlayer bot;
        bot.configure(&lvl, phys, cfg);
        bot.setEnabled(true);

        PlayerState s;
        s.size = phys.playerSize;
        s.speed = speedUnitsPerSecond(gen.speedIndex);
        attemptStarts.push_back(static_cast<int>(frames.size()));

        // Marker frame so the viewer can draw the rewind as a jump backwards.
        frames.push_back(Frame{static_cast<float>(s.x), static_cast<float>(s.y), 0.f, 0, 1,
                               attempt > 0, cfg.plan.maxFrames, 0, 0});

        bool died = false;
        for (int f = 0; f < 6000; ++f) {
            const Decision d = bot.update(s);
            const BotStatus& st = bot.status();
            maxPlanMs = std::max(maxPlanMs, st.lastPlanMs);
            progressX = std::max(progressX, s.x);
            totalPresses = st.planPressesTotal;
            totalReplans += 1;
            note = st.note;

            frames.push_back(Frame{static_cast<float>(s.x), static_cast<float>(s.y),
                                   static_cast<float>(s.vy), d.hold, s.onGround, st.deadEnd,
                                   st.planLength, st.planCursor, st.planPressesTotal});

            const StepResult r = step(s, lvl, phys, d.hold);
            if (!r.alive) {
                died = true;
                lastDeathX = s.x;
                attemptDeaths.push_back(s.x);
                break;
            }
            if (s.x >= lvl.endX - 30.0) {
                finished = true;
                break;
            }
        }

        if (!died) {
            alive = true;
            break;
        }

        alive = false;
        // The level keeps a checkpoint from the previous attempt, so a rewind
        // restarts at the last checkpoint rather than at x = 0.
        const PracticeAction action = pc.onDeath(lastDeathX);
        if (action == PracticeAction::RevertAndRetry) {
            pc.onRevertDone();
        } else if (action == PracticeAction::GiveUp) {
            break;
        }
    }
    const int attemptsRun = static_cast<int>(attemptStarts.size());

    // ------------------------------------------------------------------ JSON
    std::printf("{\n");
    std::printf("  \"seed\": %u,\n", opt.seed);
    std::printf("  \"difficulty\": %d,\n", opt.difficulty);
    std::printf("  \"alive\": %s,\n", alive ? "true" : "false");
    std::printf("  \"finished\": %s,\n", finished ? "true" : "false");
    std::printf("  \"attempts\": %d,\n", attemptsRun);
    std::printf("  \"rewinds\": %d,\n", pc.status().reverts);
    std::printf("  \"deaths\": %d,\n", pc.status().deaths);
    std::printf("  \"endX\": %s,\n", num(lvl.endX).c_str());
    std::printf("  \"attemptStarts\": [");
    for (size_t i = 0; i < attemptStarts.size(); ++i)
        std::printf("%s%d", i ? "," : "", attemptStarts[i]);
    std::printf("],\n");
    std::printf("  \"attemptDeaths\": [");
    for (size_t i = 0; i < attemptDeaths.size(); ++i)
        std::printf("%s%s", i ? "," : "", num(attemptDeaths[i]).c_str());
    std::printf("],\n");
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

    std::printf("  \"deathX\": %s,\n", num(lastDeathX).c_str());
    std::printf("  \"progress\": %s,\n", num(std::min(1.0, progressX / lvl.endX), 3).c_str());
    std::printf("  \"presses\": %d,\n", totalPresses);
    std::printf("  \"replans\": %d,\n", totalReplans);
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
