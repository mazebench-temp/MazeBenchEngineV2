#pragma once

#include <stdint.h>

namespace voxelbench {

constexpr int32_t kPhysicsAbiVersion = 3;
constexpr int32_t kVoxelCapacity = 65536;
constexpr int32_t kRoleBufferCapacity = 256;
constexpr uint32_t kMotionStateVersion = 1;
constexpr int32_t kPhysicsWorkspaceBytes = 5 * 1024 * 1024;

struct PhysicsWorkspace {
  alignas(8) uint8_t storage[kPhysicsWorkspaceBytes];
};

struct Voxel {
  int32_t x;
  int32_t y;
  int32_t z;
  uint32_t role;
  int32_t generic_id;
};

// Serializable continuation state for a command that spans several animation
// ticks. The per-voxel flags deliberately use voxel indices: voxel storage
// order is stable during a command, while rigid-body indices are rebuilt in a
// workspace and are not part of the save format.
struct MotionState {
  uint32_t version;
  int32_t phase;
  int32_t direction;
  int32_t tick;
  int32_t voxel_count;
  int32_t player_index;
  uint8_t player_gravity_armed;
  uint8_t player_falling;
  uint8_t reserved[2];
  uint8_t horizontal_momentum[kVoxelCapacity];
  uint8_t falling[kVoxelCapacity];
  uint8_t gravity_armed[kVoxelCapacity];
};

enum class TickResult : int32_t {
  kNoPlayer = -2,
  kInvalid = -1,
  kComplete = 0,
  kMore = 1,
};

uint32_t hash_role(const uint8_t* bytes, int32_t length);

void reset_motion_state(MotionState* state);
void reset_workspace(PhysicsWorkspace* workspace);

// Advances at most one animation tick. Direction is read only when starting a
// new command; subsequent calls resume the command stored in `state`.
TickResult step_tick(
    PhysicsWorkspace* workspace,
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction);

using TickObserver = void (*)(
    const Voxel* voxels,
    int32_t count,
    const MotionState* state,
    void* context);

// Repeatedly calls step_tick until the command is quiescent. The observer is
// optional and receives every post-tick frame, including the final frame.
int32_t simulate_command(
    PhysicsWorkspace* workspace,
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction,
    TickObserver observer = nullptr,
    void* context = nullptr);

// Direction codes: 0 = up, 1 = right, 2 = down, 3 = left.
// Returns 0 after a valid command, -1 for invalid input, and -2 when no player
// exists. A command may contain several unit movements when Ice is involved.
int32_t simulate_turn(
    PhysicsWorkspace* workspace,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction);

// Compatibility convenience API. Search workers should use the overload that
// accepts their own PhysicsWorkspace.
int32_t simulate_turn(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction);

}  // namespace voxelbench
