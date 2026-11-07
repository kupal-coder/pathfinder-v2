#include "State.hpp"

#include <algorithm>
#include <cmath>

#include "Hooks.hpp"
#include "LevelScanner.hpp"

using namespace geode::prelude;

namespace pf {

bool g_enabled = false;
bool g_inLevel = false;
HudState g_hud{};
Settings g_settings{};

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
/// Note: LevelEditorLayer also derives from GJBaseGameLayer and calls update(),
/// so the play layer has to be checked explicitly or the bot would drive the
/// editor too.
bool readyToDrive(gd::GJBaseGameLayer* layer) {
    if (!layer || !layer->m_player1) return false;
    if (!g_inLevel) return false;
    if (gd::PlayLayer::get() != layer) return false;
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

void refreshSettings() {
    auto* mod = Mod::get();
    Settings st;

    st.enabled = mod->getSettingValue<bool>("enabled");
    st.planHorizon = static_cast<int>(mod->getSettingValue<int64_t>("plan-horizon-frames"));
    st.minSecured = static_cast<int>(mod->getSettingValue<int64_t>("min-secured-frames"));
    st.clickHorizon = static_cast<int>(mod->getSettingValue<int64_t>("click-horizon-frames"));
    st.replanEvery = static_cast<int>(mod->getSettingValue<int64_t>("replan-every-frames"));
    st.stateBudget = static_cast<int>(mod->getSettingValue<int64_t>("max-states-per-frame"));
    st.verifyPlans = mod->getSettingValue<bool>("verify-plans");
    st.gravity = mod->getSettingValue<double>("gravity");
    st.jumpVelocity = mod->getSettingValue<double>("jump-velocity");
    st.hitboxBlocks = mod->getSettingValue<double>("hitbox-blocks");

    // Clamp to sane ranges: a setting edit must never be able to wedge the search.
    st.planHorizon = std::clamp(st.planHorizon, 30, 900);
    st.minSecured = std::clamp(st.minSecured, 1, st.planHorizon);
    st.clickHorizon = std::clamp(st.clickHorizon, 10, 600);
    st.replanEvery = std::clamp(st.replanEvery, 1, 60);
    st.stateBudget = std::clamp(st.stateBudget, 64, 40000);
    st.gravity = std::clamp(st.gravity, 0.05, 4.0);
    st.jumpVelocity = std::clamp(st.jumpVelocity, 1.0, 30.0);
    st.hitboxBlocks = std::clamp(st.hitboxBlocks, 0.25, 2.0);

    g_settings = st;
}

void applySettings() {
    g_enabled = g_settings.enabled;

    g_botConfig.plan.maxFrames = g_settings.planHorizon;
    g_botConfig.plan.minFrames = std::min(g_settings.planHorizon, g_settings.minSecured);
    g_botConfig.plan.maxStatesPerLayer = g_settings.stateBudget;
    g_botConfig.clickHorizon = g_settings.clickHorizon;
    g_botConfig.replanEvery = g_settings.replanEvery;
    g_botConfig.verifyPlans = g_settings.verifyPlans;

    g_physics.gravity = g_settings.gravity;
    g_physics.jumpVelocity = g_settings.jumpVelocity;
    g_physics.playerSize = g_physics.unitsPerBlock * g_settings.hitboxBlocks;

    g_bot.configure(&g_level, g_physics, g_botConfig);
    g_bot.setEnabled(g_enabled);
}

bool swallowHumanInput() {
    // Only swallow input while the bot is actually driving this frame.
    return g_enabled && g_ownsInput;
}

void resetHud() {
    const bool wasEnabled = g_hud.enabled;
    const int solids = g_hud.solids;
    const int hazards = g_hud.hazards;
    const bool scanned = g_hud.scanned;
    g_hud = HudState{};
    g_hud.enabled = wasEnabled;
    g_hud.solids = solids;
    g_hud.hazards = hazards;
    g_hud.scanned = scanned;
    g_hud.note = "new attempt";
}

void invalidateLevel() {
    g_inLevel = false;
    g_scanned = false;
    g_scannedOwner = nullptr;
    g_ownsInput = false;
    g_bot.reset();
    g_bot.setEnabled(g_enabled);
}

void onLevelStart(gd::PlayLayer* layer) {
    g_inLevel = true;
    if (!layer || !layer->m_player1) return;

    // The level geometry is identical on every attempt, so the CACHED scan is
    // reused (a big level has 10k+ objects; rescanning per death would stutter).
    // If the scan is missing it happens lazily on the first frame.
    if (!g_scanned) ensureScanned(layer);

    g_bot.reset();
    g_bot.setEnabled(g_enabled);
    log::info("attempt started (scan cached: {}), bot {}", g_scanned ? "yes" : "no",
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

    // Only claim the input when the bot actually has something to say. If the
    // search found no plan at all (unsupported geometry, a level built on orbs the
    // simulator does not model yet, ...), leave the human in control instead of
    // freezing their button -- being softlocked in a mod is worse than dying.
    g_ownsInput = g_bot.status().hasPlan;
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
