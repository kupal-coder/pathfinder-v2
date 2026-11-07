// Pathfinder - small in-game overlay showing what the solver is doing.
#pragma once

#include <Geode/Geode.hpp>

namespace pf {

/// Creates the overlay and attaches it to a PlayLayer. Safe to call repeatedly.
void attachHud(gd::PlayLayer* layer);

/// Refreshes the overlay text at most ~10x per second. Called from the physics
/// frame hook; `dt` is the game's frame delta.
void hudTick(float dt);

} // namespace pf
