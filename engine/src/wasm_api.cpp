#include "voxelbench/physics.hpp"

namespace {

voxelbench::Voxel g_voxels[voxelbench::kVoxelCapacity];
voxelbench::PhysicsWorkspace g_workspace;
voxelbench::MotionState g_motion_state;
uint8_t g_role_buffer[voxelbench::kRoleBufferCapacity];
bool g_initialized = false;

void EnsureInitialized() {
  if (g_initialized) return;
  voxelbench::reset_workspace(&g_workspace);
  voxelbench::reset_motion_state(&g_motion_state);
  g_initialized = true;
}

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

uint8_t* motion_state_buffer() {
  EnsureInitialized();
  return reinterpret_cast<uint8_t*>(&g_motion_state);
}

int32_t motion_state_size() {
  return static_cast<int32_t>(sizeof(g_motion_state));
}

void reset_command() {
  EnsureInitialized();
  voxelbench::reset_motion_state(&g_motion_state);
}

int32_t step_command_tick(
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  EnsureInitialized();
  return static_cast<int32_t>(voxelbench::step_tick(
      &g_workspace,
      &g_motion_state,
      g_voxels,
      count,
      width,
      height,
      direction));
}

int32_t command_tick() {
  EnsureInitialized();
  return g_motion_state.tick;
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
  EnsureInitialized();
  return voxelbench::simulate_turn(
      &g_workspace, g_voxels, count, width, height, direction);
}

}  // extern "C"
