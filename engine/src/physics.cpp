#include "voxelbench/physics.hpp"

#include <limits.h>

namespace voxelbench {
namespace {

#include "physics/workspace.inc"
#include "physics/objects.inc"
#include "physics/movement.inc"
#include "physics/command_state.inc"

}  // namespace

#include "physics/public_core.inc"
#include "physics/tick.inc"
#include "physics/simulate.inc"

}  // namespace voxelbench
