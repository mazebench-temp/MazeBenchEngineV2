#include "voxelbench/physics.hpp"

namespace {

voxelbench::Voxel g_voxels[voxelbench::kVoxelCapacity];
uint8_t g_role_buffer[voxelbench::kRoleBufferCapacity];

}  // namespace

extern "C" {

int32_t physics_abi_version() {
  return voxelbench::kPhysicsAbiVersion;
}

int32_t voxel_stride() {
  return static_cast<int32_t>(sizeof(voxelbench::Voxel) / sizeof(int32_t));
}

int32_t voxel_capacity() {
  return voxelbench::kVoxelCapacity;
}

voxelbench::Voxel* voxel_buffer() {
  return g_voxels;
}

int32_t role_buffer_capacity() {
  return voxelbench::kRoleBufferCapacity;
}

uint8_t* role_buffer() {
  return g_role_buffer;
}

uint32_t role_code(int32_t length) {
  if (length < 0 || length > voxelbench::kRoleBufferCapacity) return 0;
  return voxelbench::hash_role(g_role_buffer, length);
}

int32_t simulate_turn(
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  return voxelbench::simulate_turn(g_voxels, count, width, height, direction);
}

}  // extern "C"
