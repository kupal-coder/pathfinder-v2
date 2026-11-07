// Pathfinder - small in-game overlay showing what the solver is doing.
#pragma once

#include <Geode/Geode.hpp>

namespace pf {

/// Creates the overlay and attaches it to a PlayLayer. Safe to call repeatedly.
void attachHud(gd::PlayLayer* layer);

/// Refreshes the overlay text at most ~10x per second. Called from the physics
/// frame hook; `dt` is the game's frame delta.
void hudTick(float dt);

/// Forgets the attached label. PlayLayer::onExit calls this: the label is a child
/// of the layer, so keeping the pointer would point at freed memory after the
/// level is left, and the next PlayLayer would never get a HUD again.
void detachHud();

} // namespace pf
