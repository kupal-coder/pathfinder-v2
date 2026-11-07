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

static void testGamemodes() {
    std::printf("\n== gamemodes ==\n");
    PhysicsParams p = makeParams();

    LevelBuilder lb;
    lb.ground(0).start(0).end(5000).ceiling(600);
    LevelModel lvl = lb.take();

    // --- ball: a press must reverse which way the ball falls --------------
    {
        PlayerState s;
        s.mode = Mode::Ball;
        s.size = p.playerSize;
        s.speed = speedUnitsPerSecond(1);
        s.onGround = true;
        const double y0 = s.y;
        step(s, lvl, p, true); // one press flips gravity
        for (int i = 0; i < 20; ++i) {
            const StepResult r = step(s, lvl, p, false);
            if (!r.alive) break;
        }
        std::printf("  ball: flipped=%d, y %.1f -> %.1f\n", s.gravityFlipped ? 1 : 0, y0, s.y);
        CHECK(s.gravityFlipped);
        CHECK(s.y > y0 + 30.0); // must climb towards the ceiling
    }

    // --- wave: 45 degree flight, symmetric in both directions -------------
    {
        PlayerState s;
        s.mode = Mode::Wave;
        s.size = p.playerSize;
        s.speed = speedUnitsPerSecond(1);
        s.y = 300.0;
        s.onGround = false;
        const double y0 = s.y;
        for (int i = 0; i < 10; ++i) step(s, lvl, p, true);
        const double up = s.y - y0;
        const double y1 = s.y;
        for (int i = 0; i < 10; ++i) step(s, lvl, p, false);
        const double down = y1 - s.y;
        std::printf("  wave: +%.1f up, -%.1f down\n", up, down);
        CHECK(up > 40.0);
        CHECK(std::abs(up - down) < 2.0);
    }

    // --- ufo: air jumps are limited, not infinite --------------------------
    {
        PlayerState s;
        s.mode = Mode::Ufo;
        s.size = p.playerSize;
        s.speed = speedUnitsPerSecond(1);
        s.onGround = true;

        // Count how many extra jumps happen inside a single airborne phase.
        int airJumpsThisPhase = 0;
        int worstPhase = 0;
        bool wasAirborne = false;
        double prevVy = 0.0;
        for (int i = 0; i < 200; ++i) {
            const bool press = (i % 4 == 0); // spam the button
            const StepResult r = step(s, lvl, p, press);
            if (!r.alive) break;

            const bool airborne = !s.onGround;
            if (airborne && !wasAirborne) {
                // new airborne phase
                airJumpsThisPhase = 0;
            }
            // a jump shows up as a sudden upward velocity kick while airborne
            if (airborne && s.vy > prevVy + 5.0) airJumpsThisPhase++;
            if (!airborne && wasAirborne) worstPhase = std::max(worstPhase, airJumpsThisPhase);

            wasAirborne = airborne;
            prevVy = s.vy;
        }
        worstPhase = std::max(worstPhase, airJumpsThisPhase);
        std::printf("  ufo: max air jumps in one airborne phase = %d (cap %d)\n", worstPhase,
                    p.ufoAirJumps);
        // The phase that starts on the ground also contains the initial jump.
        CHECK(worstPhase <= p.ufoAirJumps + 1);
    }

    // --- robot: a longer charge must jump higher ---------------------------
    {
        std::vector<double> peaks;
        for (int hold : {1, 10, 22}) {
            PlayerState s;
            s.mode = Mode::Robot;
            s.size = p.playerSize;
            s.speed = speedUnitsPerSecond(1);
            s.onGround = true;
            double peak = 0.0;
            for (int i = 0; i < 60; ++i) {
                const StepResult r = step(s, lvl, p, i < hold);
                if (!r.alive) break;
                peak = std::max(peak, s.y);
            }
            peaks.push_back(peak);
            std::printf("  robot: hold %2d frames -> peak %.1f\n", hold, peak);
        }
        // A broken charge reset makes every one of these zero.
        CHECK(peaks[0] > 5.0);
        CHECK(peaks[2] > peaks[0] + 5.0);
    }

    // --- ship must be refused, not guessed at -----------------------------
    CHECK(!isModeSupported(Mode::Ship));
    CHECK(!isModeSupported(Mode::Spider));
    CHECK(isModeSupported(Mode::Cube));
    CHECK(isModeCalibrated(Mode::Cube));
    CHECK(!isModeCalibrated(Mode::Wave));
}

/// Prints a compact side-on view of the level with the planned path drawn on top.
static void dumpLevel(const LevelModel& lvl, int fromX, int toX, int cellW = 4) {
    const int rows = 10;
    const size_t cols = static_cast<size_t>((toX - fromX) / cellW + 1);
    std::vector<std::string> grid(static_cast<size_t>(rows), std::string(cols, ' '));

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
        testGamemodes();
    }
    showDemo();

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "\033[32mALL TESTS PASSED\033[0m"
                                                       : "\033[31mTESTS FAILED\033[0m",
                gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
