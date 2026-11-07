#include "State.hpp"

#include <algorithm>
#include <cmath>

#include "Hooks.hpp"
#include "LevelScanner.hpp"

using namespace geode::prelude;

namespace pf {

bool g_enabled = false;
HudState g_hud{};

namespace {

AutoPlayer g_bot;
PhysicsParams g_physics;
BotConfig g_botConfig;
LevelModel g_level;

ScanStats g_lastScan;
bool g_scanned = false;
gd::GJBaseGameLayer* g_scannedOwner = nullptr;
bool g_ownsInput = false;

/// True once the player is actually spawned and moving in a level.
bool readyToDrive(gd::GJBaseGameLayer* layer) {
    if (!layer || !layer->m_player1) return false;
    if (!layer->m_objects || layer->m_objects->count() == 0) return false;
    return true;
}

void ensureScanned(gd::GJBaseGameLayer* layer) {
    if (g_scanned && g_scannedOwner == layer) return;

    double groundY = 0.0;
    g_lastScan = scanLayer(layer, g_level, &groundY);
    // scanLayer already put groundY into the level model; re-bucket with it.
    g_level.groundY = groundY;
    g_level.finalize();

    g_scannedOwner = layer;
    g_scanned = true;

    g_bot.configure(&g_level, g_physics, g_botConfig);
    g_bot.setEnabled(g_enabled);

    g_hud.solids = g_lastScan.solids;
    g_hud.hazards = g_lastScan.hazards;
    g_hud.scanned = true;
}

} // namespace

AutoPlayer& bot() { return g_bot; }
PhysicsParams& physics() { return g_physics; }
BotConfig& botConfig() { return g_botConfig; }
LevelModel& scannedLevel() { return g_level; }

void applySettings() {
    auto* mod = Mod::get();

    g_botConfig.plan.maxFrames =
        static_cast<int>(mod->getSettingValue<int64_t>("plan-horizon-frames"));
    g_botConfig.plan.minFrames =
        std::min(g_botConfig.plan.maxFrames, static_cast<int>(mod->getSettingValue<int64_t>("min-secured-frames")));
    g_botConfig.clickHorizon =
        static_cast<int>(mod->getSettingValue<int64_t>("click-horizon-frames"));
    g_botConfig.replanEvery =
        std::max(1, static_cast<int>(mod->getSettingValue<int64_t>("replan-every-frames")));
    g_botConfig.verifyPlans = mod->getSettingValue<bool>("verify-plans");
    g_botConfig.plan.maxStatesPerLayer =
        static_cast<int>(mod->getSettingValue<int64_t>("max-states-per-frame"));

    // Physics: expose the two numbers people actually want to tune.
    g_physics.gravity = mod->getSettingValue<double>("gravity");
    g_physics.jumpVelocity = mod->getSettingValue<double>("jump-velocity");
    g_physics.playerSize =
        g_physics.unitsPerBlock * mod->getSettingValue<double>("hitbox-blocks");

    g_bot.configure(&g_level, g_physics, g_botConfig);
}

bool swallowHumanInput() {
    // Only swallow input while the bot is actually driving this frame.
    return g_enabled && g_ownsInput;
}

void invalidateLevel() {
    g_scanned = false;
    g_scannedOwner = nullptr;
    g_ownsInput = false;
    g_bot.reset();
    g_bot.setEnabled(g_enabled);
}

void onLevelStart(gd::PlayLayer* layer) {
    invalidateLevel();
    if (!layer || !layer->m_player1) return;
    ensureScanned(layer);
    g_bot.reset();
    g_bot.setEnabled(g_enabled);
    log::info("attempt started - level rescanned, bot {}",
              g_enabled ? "enabled" : "disabled");
}

void onGameFrame(gd::GJBaseGameLayer* layer) {
    g_ownsInput = false;
    if (!g_enabled || !readyToDrive(layer)) return;

    ensureScanned(layer);

    PlayerState live = readPlayer(layer->m_player1, g_physics);
    if (!isModeSupported(live.mode)) {
        g_hud.unsupported = true;
        g_hud.note = std::string("unsupported gamemode: ") + modeName(live.mode);
        return;
    }
    g_hud.unsupported = false;

    const Decision d = g_bot.update(live);
    g_ownsInput = true;
    g_hud.enabled = true;
    g_hud.lastInput = d;

    const BotStatus& st = g_bot.status();
    g_hud.presses = st.planPressesTotal;
    g_hud.replans = st.replans;
    g_hud.planFrames = st.planLength;
    g_hud.cursor = st.planCursor;
    g_hud.confidence = st.confidence;
    g_hud.note = st.note;
    g_hud.planMs = st.lastPlanMs;
    g_hud.deadEnd = st.deadEnd;
    g_hud.mode = modeName(live.mode);

    // Drive the game: press/release exactly when the bot's plan says so.
    if (d.changed) {
        if (d.hold) {
            layer->m_player1->pushButton(gd::PlayerButton::Jump);
        } else {
            layer->m_player1->releaseButton(gd::PlayerButton::Jump);
        }
    }
}

} // namespace pf
