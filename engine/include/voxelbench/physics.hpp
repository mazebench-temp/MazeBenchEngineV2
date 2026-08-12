#pragma once

#include <stdint.h>

namespace voxelbench {

constexpr int32_t kPhysicsAbiVersion = 2;
constexpr int32_t kVoxelCapacity = 65536;
constexpr int32_t kRoleBufferCapacity = 256;

struct Voxel {
  int32_t x;
  int32_t y;
  int32_t z;
  uint32_t role;
  int32_t generic_id;
};

uint32_t hash_role(const uint8_t* bytes, int32_t length);

// Direction codes: 0 = up, 1 = right, 2 = down, 3 = left.
// Returns 0 after a valid command, -1 for invalid input, and -2 when no player
// exists. A command may contain several unit movements when Ice is involved.
int32_t simulate_turn(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction);

}  // namespace voxelbench
