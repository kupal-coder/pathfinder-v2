#include "State.hpp"

#include <algorithm>
#include <cmath>

#include <limits>

#include "Hooks.hpp"
#include "LevelScanner.hpp"

using namespace geode::prelude;

namespace pf {

bool g_enabled = false;
bool g_inLevel = false;
HudState g_hud{};
Settings g_settings{};

namespace {

/// Drops GD's most recent checkpoint and rewinds to the one before it.
///
/// GD's practice stack is what restores object state and the camera, so this uses
/// it instead of teleporting the cube. Returns false when there is nothing to go
/// back to (GD refuses to remove the very first checkpoint of an attempt).
bool rewindOneCheckpoint(gd::PlayLayer* layer) {
    if (!layer || !layer->m_checkpointArray) return false;
    const unsigned int count = layer->m_checkpointArray->count();
    if (count == 0) return false;

    // removeCheckpoint(true) drops the last checkpoint; the next respawn lands on
    // the one before it.
    layer->removeCheckpoint(true);

    const unsigned int after = layer->m_checkpointArray
                                   ? layer->m_checkpointArray->count()
                                   : 0u;
    return after < count;
}

AutoPlayer g_bot;
PracticeController g_practice;
PhysicsParams g_physics;
BotConfig g_botConfig;
LevelModel g_level;

ScanStats g_lastScan;
bool g_scanned = false;
gd::GJBaseGameLayer* g_scannedOwner = nullptr;
bool g_ownsInput = false;

/// Hazard centres handed to the practice controller so it does not drop a
/// checkpoint inside a spike run.
std::vector<double> g_hazardXs;

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

    // Practice checkpoints should never land inside a hazard.
    g_hazardXs.clear();
    g_hazardXs.reserve(g_level.hazards.size());
    for (const auto& h : g_level.hazards) g_hazardXs.push_back(h.centerX());
    g_practice.setHazardXs(g_hazardXs);
}

} // namespace

AutoPlayer& bot() { return g_bot; }
PracticeController& practice() { return g_practice; }
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
    st.practiceMode = mod->getSettingValue<bool>("practice-mode");
    st.sameSpotDeaths = static_cast<int>(mod->getSettingValue<int64_t>("same-spot-deaths"));
    st.maxReverts = static_cast<int>(mod->getSettingValue<int64_t>("max-rewinds"));

    // Clamp to sane ranges: a setting edit must never be able to wedge the search.
    st.planHorizon = std::clamp(st.planHorizon, 30, 900);
    st.minSecured = std::clamp(st.minSecured, 1, st.planHorizon);
    st.clickHorizon = std::clamp(st.clickHorizon, 10, 600);
    st.replanEvery = std::clamp(st.replanEvery, 1, 60);
    st.stateBudget = std::clamp(st.stateBudget, 64, 40000);
    st.gravity = std::clamp(st.gravity, 0.05, 4.0);
    st.jumpVelocity = std::clamp(st.jumpVelocity, 1.0, 30.0);
    st.hitboxBlocks = std::clamp(st.hitboxBlocks, 0.25, 2.0);
    st.sameSpotDeaths = std::clamp(st.sameSpotDeaths, 1, 20);
    st.maxReverts = std::clamp(st.maxReverts, 1, 20);

    g_settings = st;
}

void applySettings() {
    g_enabled = g_settings.enabled;

    PracticeConfig pc;
    pc.sameSpotDeaths = g_settings.sameSpotDeaths;
    pc.maxReverts = g_settings.maxReverts;
    g_practice.configure(pc);

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
    applyPracticeMode(layer);
    log::info("attempt started (scan cached: {}), bot {}", g_scanned ? "yes" : "no",
              g_enabled ? "enabled" : "disabled");
}

void applyPracticeMode(gd::PlayLayer* layer) {
    if (!layer) return;

    // Pathfinder runs in practice mode while it is enabled: that is what gives it
    // its own checkpoints and the ability to rewind after a repeated death.
    const bool wantPractice = g_enabled && g_settings.practiceMode;
    g_hud.practice = wantPractice;

    if (layer->m_isPracticeMode != wantPractice) {
        layer->togglePracticeMode(wantPractice);
        log::info("practice mode {}", wantPractice ? "on" : "off");
    }

    // A fresh attempt starts a fresh death ledger; the checkpoints GD keeps are
    // cleared so the bot's own checkpoint logic owns the positions.
    g_practice.reset();
    g_practice.status();
    g_hud.deaths = 0;
    g_hud.reverts = 0;
    g_hud.checkpoints = 0;
    g_hud.clusterDeaths = 0;
    g_hud.deathX = 0.0;
}

void onPlayerDied(gd::PlayLayer* layer) {
    if (!g_enabled || !layer || !layer->m_player1) return;

    const double x = readPlayer(layer->m_player1, g_physics).x;
    const PracticeAction action = g_practice.onDeath(x);

    const PracticeStatus& st = g_practice.status();
    g_hud.deaths = st.deaths;
    g_hud.reverts = st.reverts;
    g_hud.checkpoints = st.checkpointsPlaced;
    g_hud.clusterDeaths = st.clusterDeaths;
    g_hud.deathX = st.deathX;
    g_hud.practiceNote = st.note;

    if (action == PracticeAction::RevertAndRetry) {
        // Go back one checkpoint and let the controller re-plan the section with a
        // wider search. GD's own rewind is used rather than teleporting the cube:
        // it restores object states, camera and the attempt counter correctly.
        const bool rewound = rewindOneCheckpoint(layer);
        if (rewound) {
            g_practice.onRevertDone();
            g_hud.note = "rewound to previous checkpoint, retrying";
        } else {
            const PracticeAction after = g_practice.onRevertFailed();
            g_hud.note = g_practice.status().note;
            if (after == PracticeAction::GiveUp) {
                g_inLevel = false; // stop driving, hand control back
            }
        }
        g_hud.reverts = g_practice.status().reverts;
        g_hud.practiceNote = g_practice.status().note;

        // Make the next plans search further; the extra CPU is worth it when the
        // bot has already proven it cannot solve this spot at the default horizon.
        g_botConfig.plan.maxFrames = g_practice.horizonFor(g_settings.planHorizon);
        g_botConfig.plan.minFrames =
            std::min(g_botConfig.plan.maxFrames,
                     g_practice.minFramesFor(g_settings.minSecured));
        g_bot.configure(&g_level, g_physics, g_botConfig);
        g_bot.setEnabled(g_enabled);
    } else if (action == PracticeAction::GiveUp) {
        g_hud.note = g_practice.status().note;
        g_inLevel = false; // stop driving; hand control back
    } else {
        g_hud.note = g_practice.status().note;
    }
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

    // Practice: the bot places its own checkpoints as it advances, so a repeated
    // death has somewhere to rewind to. markCheckpoint() lives on PlayLayer, and
    // readyToDrive() already established that this layer *is* the play layer.
    if (g_hud.practice && live.onGround) {
        const PracticeAction action = g_practice.onProgress(live.x);
        if (action == PracticeAction::PlaceCheckpoint) {
            if (auto* play = gd::PlayLayer::get()) play->markCheckpoint();
            g_hud.checkpoints = g_practice.status().checkpointsPlaced;
            g_hud.practiceNote = g_practice.status().note;
        }
    }

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
