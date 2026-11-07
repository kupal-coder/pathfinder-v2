// Pathfinder - converts a live Geometry Dash level into the solver's level model.
#pragma once

#include <Geode/Geode.hpp>

#include "../solver/Physics.hpp"
#include "../solver/Types.hpp"

namespace pf {

struct ScanStats {
    int solids = 0;
    int hazards = 0;
    int slopes = 0;
    int skipped = 0;
    int orbs = 0;
    int portals = 0;
    int speedChanges = 0;
    int triggers = 0;
    double minX = 1e9;
    double maxX = -1e9;
    double minY = 1e9;
    double maxY = -1e9;
};

/// Walks every object in a running level and fills `out` with solid/hazard
/// rectangles, in the same coordinate space GD uses for objects
/// (30 units == one block).
///
/// `groundY` is the surface the player spawns on; it is derived from the scan so
/// the solver never has to guess GD's vertical origin. See docs/CALIBRATION.md.
ScanStats scanLayer(gd::GJBaseGameLayer* layer, LevelModel& out, double* groundYOut);

/// Gamemode of a live player object.
Mode detectMode(gd::PlayerObject* player);

/// Snapshot of the live player in the solver's units.
PlayerState readPlayer(gd::PlayerObject* player, const PhysicsParams& phys);

/// Horizontal speed of the live player in units/second.
double readPlayerSpeed(gd::PlayerObject* player);

} // namespace pf
