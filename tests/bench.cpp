// Randomised stress benchmark.
//
// For every generated level:
//   1. a reference planner (long horizon, large frontier) decides whether the
//      level is beatable at all - an impossible level is reported as such
//   2. the closed-loop AutoPlayer plays it against the real simulator
//
// The number that matters is the survival rate on *beatable* levels.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "LevelBuilder.hpp"
#include "LevelGen.hpp"
#include "bot/AutoPlayer.hpp"
#include "solver/Planner.hpp"

using namespace pf;

int main(int argc, char** argv) {
    const int count = argc > 1 ? std::atoi(argv[1]) : 40;
    std::mt19937 rng(12345);

    PhysicsParams phys;
    BotConfig cfg;
    cfg.plan.maxFrames = 200;
    cfg.plan.minFrames = 90;
    cfg.replanEvery = 8;

    int beatable = 0, survived = 0, impossible = 0, solverFailures = 0;
    double worstPlanMs = 0.0, sumPlanMs = 0.0;
    long long planCalls = 0;
    double progressSum = 0.0;

    for (int i = 0; i < count; ++i) {
        GenConfig gc;
        gc.length = 2000.0 + (i % 5) * 600.0;
        gc.spikeChance = 0.35 + 0.10 * ((i / 5) % 4);
        const LevelModel lvl = generateLevel(rng, gc);

        const double speed = speedUnitsPerSecond(gc.speedIndex);

        // --- 1. reference feasibility ---------------------------------------
        // The oracle has to run the whole level with a huge frontier to decide
        // whether a level can be beaten at all - a short horizon proves nothing.
        PlayerState s0;
        s0.size = 30;
        s0.speed = speed;
        const int needFrames =
            static_cast<int>(lvl.endX / (speed / phys.fps)) + static_cast<int>(phys.fps * 2);
        PlanConfig refCfg;
        refCfg.maxFrames = needFrames;
        refCfg.minFrames = needFrames;
        refCfg.maxStatesPerLayer = 200000;
        Planner ref(&lvl, phys, refCfg);
        const PlanResult refPlan = ref.plan(s0);
        const bool feasible = refPlan.success;
        if (!feasible) {
            impossible++;
            printf("level %2d len=%5.0f  IMPOSSIBLE  (ref planner: %s)\n", i, lvl.endX,
                   refPlan.reason.c_str());
            continue;
        }
        beatable++;

        // --- 2. closed loop --------------------------------------------------
        AutoPlayer bot;
        bot.configure(&lvl, phys, cfg);
        bot.setEnabled(true);

        PlayerState s = s0;
        bool alive = true;
        int frames = 0;
        while (frames < 4000) {
            const Decision d = bot.update(s);
            const StepResult r = step(s, lvl, phys, d.hold);
            frames++;
            if (!r.alive) { alive = false; break; }
            if (s.x >= lvl.endX - 30) break;
        }

        const BotStatus& st = bot.status();
        sumPlanMs += st.lastPlanMs;
        worstPlanMs = std::max(worstPlanMs, st.lastPlanMs);
        planCalls += st.replans;
        progressSum += std::min(1.0, s.x / lvl.endX);
        if (alive) survived++;
        else solverFailures++;

        printf("level %2d len=%5.0f  %-5s x=%6.1f/%6.1f  presses=%3d replans=%3d plan=%.2fms  %s\n",
               i, lvl.endX, alive ? "OK" : "DIED", s.x, lvl.endX, st.planPressesTotal, st.replans,
               st.lastPlanMs, st.note.c_str());
    }

    printf("\n---\n");
    printf("levels generated      : %d\n", count);
    printf("impossible (detected) : %d\n", impossible);
    printf("beatable              : %d\n", beatable);
    printf("survived              : %d (%.1f%% of beatable)\n", survived,
           beatable ? 100.0 * survived / beatable : 0.0);
    printf("solver failures       : %d\n", solverFailures);
    printf("avg progress          : %.1f%%\n", 100.0 * progressSum / std::max(1, beatable));
    printf("plan time avg/worst   : %.3f / %.3f ms over %lld calls\n",
           sumPlanMs / static_cast<double>(std::max<long long>(1, planCalls)), worstPlanMs,
           static_cast<long long>(planCalls));
    return survived == beatable ? 0 : 1;
}
