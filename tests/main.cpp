// Test / demo driver for the Pathfinder solver.
//
//   make -C tests run
//
// It (1) runs the physics sanity checks, (2) plans a bunch of synthetic levels
// and replays the resulting plan through the simulator, and (3) can dump an
// ASCII trace of a level so you can eyeball what the bot is doing.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "LevelBuilder.hpp"
#include "bot/AutoPlayer.hpp"
#include "solver/Planner.hpp"

using namespace pf;

static int gFailures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("  \033[31mFAIL\033[0m %s (line %d)\n", #cond, __LINE__);    \
            gFailures++;                                                             \
        }                                                                            \
    } while (0)

static PhysicsParams makeParams() {
    PhysicsParams p;
    return p;
}

static void testPhysicsSanity() {
    std::printf("\n== physics sanity ==\n");
    PhysicsParams p = makeParams();

    LevelBuilder b;
    b.ground(0).start(0).end(2000).build();
    LevelModel lvl = b.take();

    PlayerState s;
    s.y = 0;
    s.onGround = true;
    s.size = p.playerSize;
    s.speed = speedUnitsPerSecond(1);

    // A cube on flat ground stays alive forever and moves right at the expected rate.
    const double startX = s.x;
    for (int i = 0; i < 60; ++i) {
        const StepResult r = step(s, lvl, p, false);
        CHECK(r.alive);
    }
    const double expected = speedUnitsPerSecond(1);
    std::printf("  moved %.1f units in 60 frames (expected ~%.1f)\n", s.x - startX, expected);
    CHECK(std::abs((s.x - startX) - expected) < 6.0);
    CHECK(std::abs(s.y) < 0.01);

    // Jump apex should clear a normal spike.
    PlayerState j;
    j.size = p.playerSize;
    j.onGround = true;
    double peak = 0.0;
    bool landed = false;
    for (int i = 0; i < 60; ++i) {
        const StepResult r = step(j, lvl, p, i < 1);
        CHECK(r.alive);
        peak = std::max(peak, j.y);
        if (i > 3 && j.onGround) landed = true;
    }
    std::printf("  jump apex %.1f units (a block is 30)\n", peak);
    CHECK(peak > 30.0);
    CHECK(landed);

    // Ground contact has to count, otherwise the cube could never jump again.
    const int jumpFrames = [] {
        PlayerState t;
        t.size = 30;
        t.onGround = true;
        LevelBuilder g;
        g.ground(0).build();
        LevelModel gl = g.take();
        PhysicsParams gp;
        int frames = 0;
        bool airborne = false;
        for (int i = 0; i < 400; ++i) {
            const StepResult r = step(t, gl, gp, !airborne && frames == 0);
            if (!r.alive) return -1;
            if (frames == 0) airborne = true;
            if (airborne) ++frames;
            if (airborne && t.onGround) return frames;
        }
        return frames;
    }();
    std::printf("  jump airtime %d frames\n", jumpFrames);
    CHECK(jumpFrames > 18);
}

static void testSpikeStrip() {
    std::printf("\n== spike strip (must jump) ==\n");
    PhysicsParams p = makeParams();

    LevelBuilder b;
    b.ground(0).start(0).end(1600);
    for (int i = 0; i < 6; ++i) b.spike(300.0 + i * 260.0);
    LevelModel lvl = b.take();

    // Coasting must kill the player - otherwise the test proves nothing.
    PlayerState s;
    s.size = p.playerSize;
    s.speed = speedUnitsPerSecond(1);
    bool coastDies = false;
    for (int i = 0; i < 400; ++i) {
        const StepResult r = step(s, lvl, p, false);
        if (!r.alive) {
            coastDies = true;
            break;
        }
    }
    printf("  coasting into the spikes dies: %s\n", coastDies ? "yes" : "no");
    CHECK(coastDies);

    PlanConfig cfg;
    cfg.minFrames = 700;
    cfg.maxFrames = 700;
    Planner planner(&lvl, p, cfg);
    PlayerState start;
    start.size = p.playerSize;
    start.speed = speedUnitsPerSecond(1);

    const auto t0 = std::chrono::steady_clock::now();
    const PlanResult r = planner.plan(start);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::printf("  plan: success=%d frames=%d presses=%d (%.2f ms) %s\n", r.success ? 1 : 0,
                r.plan.length(), countPresses(r.plan), ms, r.reason.c_str());
    CHECK(r.success);
    CHECK(countPresses(r.plan) >= 1);

    const ReplayResult rep = replay(start, r.plan, lvl, p);
    std::printf("  replay survived=%d ran %d/%d frames, reached x=%.1f\n", rep.alive ? 1 : 0,
                rep.framesRun, r.plan.length(), rep.maxX);
    CHECK(rep.alive);
}

static void testClickSurvivalGate() {
    std::printf("\n== click survival gate ==\n");
    PhysicsParams p = makeParams();

    LevelBuilder b;
    b.ground(0).start(0).end(1200);
    b.spike(400);
    LevelModel lvl = b.take();

    Planner planner(&lvl, p);
    PlayerState s;
    s.size = p.playerSize;
    s.speed = speedUnitsPerSecond(1);

    // A click far too early: it lands and the player then coasts into the spike.
    const bool early = planner.clickSurvives(s, 1, 240);
    std::printf("  click at x=0 lives 240 frames: %s\n", early ? "yes" : "no");

    // Walk up to the spike and verify a click right before it works.
    bool foundSafe = false;
    for (int i = 0; i < 200; ++i) {
        const StepResult r = step(s, lvl, p, false);
        if (!r.alive) break;
        if (s.x > 330) {
            foundSafe = planner.clickSurvives(s, 4, 240);
            std::printf("  click at x=%.1f lives 240 frames: %s\n", s.x, foundSafe ? "yes" : "no");
            break;
        }
    }
    CHECK(foundSafe);
}

static void testBlocksAndCeiling() {
    std::printf("\n== blocks / corridor ==\n");
    PhysicsParams p = makeParams();

    LevelBuilder b;
    b.ground(0).start(0).end(1500);
    // Staircase of blocks with spikes on top of the last one.
    b.block(400, 0, 60, 60);
    b.block(500, 0, 60, 90);
    b.spike(560, 90);
    b.block(700, 0, 120, 60);
    b.spike(760, 60);
    b.spike(790, 60);
    LevelModel lvl = b.take();

    Planner planner(&lvl, p);
    PlayerState start;
    start.size = p.playerSize;
    start.speed = speedUnitsPerSecond(1);

    const PlanResult r = planner.plan(start);
    std::printf("  plan: success=%d frames=%d presses=%d %s\n", r.success ? 1 : 0,
                r.plan.length(), countPresses(r.plan), r.reason.c_str());
    CHECK(r.success);

    const ReplayResult rep = replay(start, r.plan, lvl, p);
    std::printf("  replay survived=%d, reached x=%.1f\n", rep.alive ? 1 : 0, rep.maxX);
    CHECK(rep.alive);
}

static void testAutoPlayerLoop() {
    std::printf("\n== closed loop (AutoPlayer drives a simulated game) ==\n");
    PhysicsParams p = makeParams();

    LevelBuilder b;
    b.ground(0).start(0).end(2000);
    for (int i = 0; i < 8; ++i) b.spike(250.0 + i * 200.0);
    LevelModel lvl = b.take();

    BotConfig cfg;
    cfg.plan.maxFrames = 200;
    cfg.plan.minFrames = 90;

    AutoPlayer bot;
    bot.configure(&lvl, p, cfg);
    bot.setEnabled(true);

    PlayerState s;
    s.size = p.playerSize;
    s.speed = speedUnitsPerSecond(1);

    int frames = 0;
    bool alive = true;
    for (; frames < 1500; ++frames) {
        const Decision d = bot.update(s);
        const StepResult r = step(s, lvl, p, d.hold);
        if (!r.alive) {
            alive = false;
            break;
        }
        if (s.x >= lvl.endX - 30.0) break;
    }

    const BotStatus& st = bot.status();
    std::printf("  survived=%d frames=%d x=%.1f presses=%d replans=%d vetoes=%d\n",
                alive ? 1 : 0, frames, s.x, st.planPressesTotal, st.replans, st.vetoes);
    std::printf("  note: %s\n", st.note.c_str());
    CHECK(alive);
    CHECK(st.planPressesTotal > 0);
}

/// Prints a compact side-on view of the level with the planned path drawn on top.
static void dumpLevel(const LevelModel& lvl, int fromX, int toX, int cellW = 4) {
    const int rows = 10;
    std::vector<std::string> grid(rows, std::string((toX - fromX) / cellW + 1, ' '));

    auto plot = [&](double x, double y, char c) {
        const int col = static_cast<int>((x - fromX) / cellW);
        const int row = static_cast<int>(y / 30.0);
        if (col < 0 || col >= static_cast<int>(grid[0].size())) return;
        if (row < 0 || row >= rows) return;
        // row 0 is the bottom of the picture
        char& cell = grid[static_cast<size_t>(rows - 1 - row)][static_cast<size_t>(col)];
        if (cell == ' ' || c == '*') cell = c;
    };

    for (const auto& s : lvl.solids)
        for (double x = s.x1; x <= s.x2; x += cellW)
            for (double y = s.y1; y < s.y2; y += 15) plot(x, y, '#');
    for (const auto& h : lvl.hazards)
        for (double x = h.x1; x <= h.x2; x += cellW)
            for (double y = h.y1; y < h.y2; y += 15) plot(x, y, '^');

    for (size_t i = 0; i < grid.size(); ++i) {
        std::printf("  %2d |%s|\n", (rows - 1 - static_cast<int>(i)) * 30,
                    grid[i].c_str());
    }
}

static void showDemo() {
    std::printf("\n== demo level trace ==\n");
    PhysicsParams p = makeParams();

    LevelBuilder b;
    b.ground(0).start(0).end(1200);
    b.spike(300);
    b.spikes(600, 2);
    b.block(800, 0, 90, 60);
    b.spike(830, 60);
    LevelModel lvl = b.take();

    dumpLevel(lvl, 0, 1200, 8);

    Planner planner(&lvl, p);
    PlayerState s;
    s.size = p.playerSize;
    s.speed = speedUnitsPerSecond(1);

    const PlanResult r = planner.plan(s);
    std::printf("  plan: success=%d presses=%d, first press at frame %d\n", r.success ? 1 : 0,
                countPresses(r.plan),
                r.plan.presses.empty() ? -1 : r.plan.presses.front().frame);

    // Replay and draw the path.
    PlayerState t = s;
    std::printf("  ");
    for (size_t i = 0; i < r.plan.frames.size(); ++i) {
        const StepResult sr = step(t, lvl, p, r.plan.frames[i]);
        if (!sr.alive) {
            std::printf("\n  DIED at frame %zu x=%.1f\n", i, t.x);
            break;
        }
        if (r.plan.frames[i] && (i == 0 || !r.plan.frames[i - 1])) std::printf("[PRESS x=%.0f] ", t.x);
    }
    std::printf("\n");
}

int main(int argc, char** argv) {
    const bool onlyDemo = argc > 1 && std::strcmp(argv[1], "--demo") == 0;

    std::printf("Pathfinder solver tests\n");

    if (!onlyDemo) {
        testPhysicsSanity();
        testSpikeStrip();
        testClickSurvivalGate();
        testBlocksAndCeiling();
        testAutoPlayerLoop();
    }
    showDemo();

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "\033[32mALL TESTS PASSED\033[0m"
                                                       : "\033[31mTESTS FAILED\033[0m",
                gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
