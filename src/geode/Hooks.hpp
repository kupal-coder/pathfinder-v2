// Pathfinder - GD hooks.
//
// Everything is done through Geode's `$modify`, so there are no hardcoded
// offsets anywhere in this mod: the SDK resolves the functions per platform.
#pragma once

namespace pf {

/// Installs the mod's hooks (currently a no-op marker: the hooks are declared
/// with $modify in Hooks.cpp and installed by the Geode loader at load time).
void installHooks();

} // namespace pf
