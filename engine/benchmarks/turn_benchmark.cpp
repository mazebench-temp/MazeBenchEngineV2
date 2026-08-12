#include "voxelbench/physics.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>

namespace {

uint32_t Role(const char* value) {
  int32_t length = 0;
  while (value[length] != '\0') ++length;
  return voxelbench::hash_role(reinterpret_cast<const uint8_t*>(value), length);
}

}  // namespace

int main() {
  // Compact Sokoban search workload: clone eight voxels, push one box through a
  // flat Ice lane, and consume the resulting player/box coordinates.
  constexpr std::uint64_t kIterations = 100000000;
  voxelbench::Voxel template_voxels[] = {
      {1, 6, 1, Role("player"), -1},
      {1, 5, 1, Role("pushable"), 0},
      {1, 6, 0, Role("floor"), -1},
      {1, 5, 0, Role("floor"), -1},
      {1, 4, 0, Role("ice"), -1},
      {1, 3, 0, Role("ice"), -1},
      {1, 2, 0, Role("ice"), -1},
      {1, 1, 0, Role("solid"), -1},
  };
  volatile std::int64_t checksum = 0;
  const auto started = std::chrono::steady_clock::now();
  for (std::uint64_t iteration = 0; iteration < kIterations; ++iteration) {
    voxelbench::Voxel voxels[8];
    for (int index = 0; index < 8; ++index) voxels[index] = template_voxels[index];
    voxelbench::simulate_turn(voxels, 8, 8, 8, 0);
    checksum = checksum + voxels[0].y + voxels[1].y;
  }
  const auto elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
  const double commands_per_second = static_cast<double>(kIterations) / elapsed;
  std::cout << "workload=flat_single_push_ice_lane"
            << " iterations=" << kIterations
            << " physics_turns_per_second="
            << static_cast<std::uint64_t>(commands_per_second)
            << " elapsed_seconds=" << elapsed << " checksum=" << checksum << '\n';
}
