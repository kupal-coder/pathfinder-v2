#include "LevelScanner.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace pf {
namespace {

/// Object ids that look like solids to the type system but do not collide, plus
/// the handful of hazards that are not tagged as hazards. Every id here is
/// something you can check in the editor; keep the list short and justified.
constexpr int kNonSolidIds[] = {
    211, 212, 213,  // animated decoration blocks
    260, 261, 262,  // animated decoration blocks (alt)
    373, 374, 375,  // saw blades (the blade itself is the hazard, the frame is not)
};

bool isNonSolid(int id) {
    for (int v : kNonSolidIds) {
        if (v == id) return true;
    }
    return false;
}

/// Objects that cannot hurt or block right now.
bool isInert(gd::GameObject* ob) {
    if (!ob) return true;
    if (ob->m_isGroupDisabled) return true; // toggled off by a trigger
    if (ob->getScaleX() == 0.f || ob->getScaleY() == 0.f) return true;
    return false;
}

/// GameObject::getObjectRect() is the same rectangle GD uses for collisions,
/// so the scan never has to reimplement anchor/scale/rotation maths.
Rect rectOf(gd::GameObject* ob) {
    const cocos2d::CCRect r = ob->getObjectRect();
    return Rect::fromCorners(r.origin.x, r.origin.y, r.origin.x + r.size.width,
                             r.origin.y + r.size.height);
}

} // namespace

ScanStats scanLayer(gd::GJBaseGameLayer* layer, LevelModel& out, double* groundYOut) {
    ScanStats st;
    out.clear();
    out.startX = 0.0;
    out.hasCeiling = false;
    if (groundYOut) *groundYOut = 0.0;

    if (!layer || !layer->m_objects) return st;

    auto* objects = layer->m_objects;
    const int count = static_cast<int>(objects->count());
    out.solids.reserve(static_cast<size_t>(count) / 2 + 8);
    out.hazards.reserve(static_cast<size_t>(count) / 4 + 8);

    for (int i = 0; i < count; ++i) {
        auto* ob = static_cast<gd::GameObject*>(objects->objectAtIndex(i));
        if (isInert(ob)) {
            st.skipped++;
            continue;
        }

        const gd::GameObjectType type = ob->m_objectType;
        const bool solidish = type == gd::GameObjectType::Solid ||
                              type == gd::GameObjectType::CollisionObject ||
                              type == gd::GameObjectType::Breakable ||
                              type == gd::GameObjectType::Slope;
        const bool hazardish = type == gd::GameObjectType::Hazard ||
                               type == gd::GameObjectType::AnimatedHazard;

        if (type == gd::GameObjectType::Orb) st.orbs++;
        else if (type == gd::GameObjectType::Portal) st.portals++;
        else if (type == gd::GameObjectType::Modifier) st.speedChanges++;
        else if (type == gd::GameObjectType::EnterEffectObject) st.triggers++;

        if (solidish) {
            if (isNonSolid(ob->m_objectID)) {
                st.skipped++;
                continue;
            }
            const Rect r = rectOf(ob);
            if (r.width() <= 0.0 || r.height() <= 0.0) {
                st.skipped++;
                continue;
            }
            // Slopes are stored as their bounding box: conservative (may block a
            // little more than GD does) which is the safe direction for a solver.
            out.solids.push_back(r);
            st.solids++;
            if (type == gd::GameObjectType::Slope) st.slopes++;
            st.minX = std::min(st.minX, r.x1);
            st.maxX = std::max(st.maxX, r.x2);
            st.minY = std::min(st.minY, r.y1);
            st.maxY = std::max(st.maxY, r.y2);
        } else if (hazardish) {
            const Rect r = rectOf(ob);
            if (r.width() <= 0.0 || r.height() <= 0.0) {
                st.skipped++;
                continue;
            }
            out.hazards.push_back(r);
            st.hazards++;
            st.minX = std::min(st.minX, r.x1);
            st.maxX = std::max(st.maxX, r.x2);
        }
    }

    if (st.minX > st.maxX) {
        st.minX = 0.0;
        st.maxX = 1000.0;
    }

    // The surface the player starts on. Taken from the live player if possible so
    // the solver's Y origin is always the same as the game's.
    if (groundYOut) {
        double ground = 0.0;
        if (layer->m_player1) {
            const cocos2d::CCPoint p = layer->m_player1->getPosition();
            ground = p.y - layer->m_player1->getContentSize().height * 0.5;
        }
        *groundYOut = ground;
    }

    out.startX = std::min(0.0, st.minX);
    out.endX = st.maxX + 120.0; // a little room past the last object
    out.groundY = groundYOut ? *groundYOut : 0.0;
    out.finalize();

    log::info("scan: {} solids ({} slopes), {} hazards, {} orbs, {} portals, {} speed, "
              "{} triggers, {} skipped, x {:.0f}..{:.0f}",
              st.solids, st.slopes, st.hazards, st.orbs, st.portals, st.speedChanges,
              st.triggers, st.skipped, st.minX, st.maxX);
    return st;
}

Mode detectMode(gd::PlayerObject* player) {
    if (!player) return Mode::Unknown;
    if (player->m_isShip) return Mode::Ship;
    if (player->m_isBall) return Mode::Ball;
    if (player->m_isBird) return Mode::Ufo;
    if (player->m_isDart) return Mode::Wave;
    if (player->m_isRobot) return Mode::Robot;
    if (player->m_isSpider) return Mode::Spider;
    if (player->m_isSwing) return Mode::Swing;
    return Mode::Cube;
}

double readPlayerSpeed(gd::PlayerObject* player) {
    if (!player) return speedUnitsPerSecond(1);

    // GD keeps the horizontal speed as a *multiplier* (1.0 == 1x) and the solver
    // works in units/second. Note that getCurrentXVelocity() is NOT usable here:
    // it returns ~5.19 for a 1x cube, i.e. a per-step value in a scaled space, so
    // multiplying it by 60 would give ~311 units -- close enough to look right and
    // wrong enough to ruin the search. Convert the multiplier instead.
    const double multiplier = static_cast<double>(player->m_playerSpeed) *
                              static_cast<double>(player->m_speedMultiplier);
    if (!(multiplier > 0.0)) return speedUnitsPerSecond(1);
    return speedFromMultiplier(multiplier);
}

PlayerState readPlayer(gd::PlayerObject* player, const PhysicsParams& phys) {
    PlayerState s;
    s.size = phys.playerSize;
    s.speed = readPlayerSpeed(player);
    s.mode = detectMode(player);
    if (!player) return s;

    const cocos2d::CCRect r = player->getObjectRect();
    s.x = r.origin.x;
    s.y = r.origin.y;
    s.vy = player->m_yVelocity;
    s.onGround = player->m_isOnGround;
    s.gravityFlipped = player->m_isUpsideDown;
    s.airJumps = 0;
    return s;
}

} // namespace pf
