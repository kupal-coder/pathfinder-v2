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

AutoPlayer& bot();
PhysicsParams& physics();
BotConfig& botConfig();
LevelModel& scannedLevel();

/// Reads mod settings into botConfig()/physics().
void applySettings();

/// True when the bot is driving and the human's press must be ignored.
bool swallowHumanInput();

/// Drops the cached level scan + plan (new attempt, level restart, ...).
void invalidateLevel();

/// Called once per physics frame from the GJBaseGameLayer::update hook.
void onGameFrame(gd::GJBaseGameLayer* layer);

/// Called from PlayLayer hooks when an attempt starts/ends.
void onLevelStart(gd::PlayLayer* layer);

/// Installs the mod's hooks. Called from onModLoaded.
void installHooks();

} // namespace pf
