// Pathfinder - shared runtime state for the mod's hooks.
#pragma once

#include <Geode/Geode.hpp>
#include <string>

#include "../bot/AutoPlayer.hpp"
#include "../solver/Physics.hpp"
#include "../solver/Types.hpp"

namespace pf {

/// HUD snapshot, refreshed once per physics frame.
struct HudState {
    bool enabled = false;
    bool unsupported = false;
    bool deadEnd = false;
    int presses = 0;
    int replans = 0;
    int planFrames = 0;
    int cursor = 0;
    int solids = 0;
    int hazards = 0;
    bool scanned = false;
    double confidence = 0.0;
    double planMs = 0.0;
    std::string mode = "cube";
    std::string note = "idle";
    Decision lastInput;
};

extern bool g_enabled;
extern HudState g_hud;

/// True while a level is being played (set on PlayLayer::resetLevel, cleared on
/// PlayLayer::onExit). The bot must not press the button in the editor or menus.
extern bool g_inLevel;

/// Cached settings snapshot. Reading a Geode setting allocates and does a JSON
/// lookup, so it must never happen on the physics path.
struct Settings {
    bool enabled = false;
    int planHorizon = 240;
    int minSecured = 120;
    int clickHorizon = 120;
    int replanEvery = 8;
    int stateBudget = 4000;
    bool verifyPlans = true;
    double gravity = 0.958199;
    double jumpVelocity = 11.180032;
    double hitboxBlocks = 1.0;
};
extern Settings g_settings;

AutoPlayer& bot();
PhysicsParams& physics();
BotConfig& botConfig();
LevelModel& scannedLevel();

/// Re-reads every mod setting (cheap: fills a small struct).
void refreshSettings();

/// Applies the cached settings to the live controller.
void applySettings();

/// True when the bot is driving and the human's press must be ignored.
bool swallowHumanInput();

/// Drops the cached level scan + plan (new attempt, level restart, ...).
void invalidateLevel();

/// Clears the HUD counters so a fresh attempt does not show last attempt's numbers.
void resetHud();

/// Called once per physics frame from the GJBaseGameLayer::update hook.
void onGameFrame(gd::GJBaseGameLayer* layer);

/// Called from PlayLayer hooks when an attempt starts/ends.
void onLevelStart(gd::PlayLayer* layer);

/// Installs the mod's hooks. Called from onModLoaded.
void installHooks();

} // namespace pf
