#include "Hooks.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include "Hud.hpp"
#include "State.hpp"

using namespace geode::prelude;

namespace pf {

/// One physics frame. GD integrates the player inside GJBaseGameLayer::update, so
/// this is the last moment a press can still influence the current frame. We run
/// before the original, exactly where a human press would have landed.
class $modify(PathfinderBaseGameLayer, gd::GJBaseGameLayer) {
    void update(float dt) {
        onGameFrame(this);
        gd::GJBaseGameLayer::update(dt);
        hudTick(dt);
    }
};

/// Level lifecycle: attach the HUD, rescan the level and drop stale plans.
class $modify(PathfinderPlayLayer, gd::PlayLayer) {
    bool init(gd::GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!gd::PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        attachHud(this);
        invalidateLevel();
        return true;
    }

    void resetLevel() {
        gd::PlayLayer::resetLevel();
        onLevelStart(this);
    }

    void onQuit() {
        invalidateLevel();
        gd::PlayLayer::onQuit();
    }
};

/// While the bot is driving, it is the only thing allowed to press. Keyboard,
/// mouse and touch all funnel through these two functions.
class $modify(PathfinderPlayerObject, gd::PlayerObject) {
    bool pushButton(gd::PlayerButton button) {
        if (button == gd::PlayerButton::Jump && swallowHumanInput()) {
            return true; // bot already pressed this frame
        }
        return gd::PlayerObject::pushButton(button);
    }

    bool releaseButton(gd::PlayerButton button) {
        if (button == gd::PlayerButton::Jump && swallowHumanInput()) {
            return true;
        }
        return gd::PlayerObject::releaseButton(button);
    }
};

void installHooks() {
    log::info("hooks: GJBaseGameLayer::update, PlayLayer::init/resetLevel/onQuit, "
              "PlayerObject::pushButton/releaseButton");
}

} // namespace pf
