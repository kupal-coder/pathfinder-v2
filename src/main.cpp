// Pathfinder - an auto-player that scans the level, searches for a path with a
// deterministic simulator, and only presses when the press is proven to survive.
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "geode/Hooks.hpp"
#include "geode/Hud.hpp"
#include "geode/State.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    pf::applySettings();
    pf::installHooks();

    // React to settings changes without a restart.
    listenForSettingChanges<bool>("enabled", [](bool value) {
        pf::g_enabled = value;
        pf::bot().setEnabled(value);
        if (!value) {
            // Make sure the cube does not get stuck holding the button.
            if (auto* pl = gd::PlayLayer::get(); pl && pl->m_player1) {
                pl->m_player1->releaseButton(gd::PlayerButton::Jump);
            }
        }
        log::info("pathfinder {}", value ? "enabled" : "disabled");
    });

    // Any tuning change is picked up live; no restart needed.
    const char* intKeys[] = {"plan-horizon-frames", "min-secured-frames",
                             "click-horizon-frames", "replan-every-frames",
                             "max-states-per-frame"};
    for (const char* key : intKeys) {
        listenForSettingChanges<int64_t>(key, [](int64_t) { pf::applySettings(); });
    }
    const char* floatKeys[] = {"gravity", "jump-velocity", "hitbox-blocks"};
    for (const char* key : floatKeys) {
        listenForSettingChanges<double>(key, [](double) { pf::applySettings(); });
    }
    listenForSettingChanges<bool>("verify-plans", [](bool) { pf::applySettings(); });

    pf::g_enabled = Mod::get()->getSettingValue<bool>("enabled");
    log::info("pathfinder loaded (enabled={})", pf::g_enabled);
}
