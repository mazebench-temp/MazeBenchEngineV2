#include "voxelbench/physics.hpp"
#include "voxelbench/search.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>

namespace {

uint32_t Role(const char* value) {
  int32_t length = 0;
  while (value[length] != '\0') ++length;
  return voxelbench::hash_role(
      reinterpret_cast<const uint8_t*>(value), length);
}

}  // namespace

int main() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[80];
  int32_t count = 0;
  for (int32_t y = 0; y < 8; ++y) {
    for (int32_t x = 0; x < 8; ++x) {
      voxels[count++] = {x, y, 0, Role("floor"), -1};
    }
  }
  voxels[count++] = {6, 1, 1, Role("goal"), -1};
  voxels[count++] = {1, 6, 1, Role("player"), -1};
  // A sparse raised 3D wall arrangement forces detours without making the
  // benchmark depend on one trivial Manhattan-distance corridor.
  constexpr int32_t walls[][3] = {
      {2, 1, 1}, {2, 2, 1}, {2, 3, 1}, {2, 5, 1},
      {4, 2, 1}, {4, 4, 1}, {4, 5, 1}, {5, 5, 1},
  };
  for (const auto& wall : walls) {
    voxels[count++] = {wall[0], wall[1], wall[2], Role("solid"), -1};
  }

  constexpr int32_t kIterations = 2000;
  std::uint64_t expanded = 0;
  std::uint64_t generated = 0;
  std::uint64_t solved = 0;
  voxelbench::reset_workspace(&physics_workspace);
  const auto started = std::chrono::steady_clock::now();
  for (int32_t iteration = 0; iteration < kIterations; ++iteration) {
    const auto result = voxelbench::search_shortest(
        &search_workspace,
        &physics_workspace,
        voxels,
        count,
        8,
        8,
        voxelbench::kSearchNodeCapacity);
    expanded += static_cast<std::uint64_t>(result.expanded);
    generated += static_cast<std::uint64_t>(result.generated);
    solved += result.status == voxelbench::SearchStatus::kSolved ? 1 : 0;
  }
  const double seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
  std::cout << "workload=exact_8x8_3d_search"
            << " solves=" << kIterations
            << " solved=" << solved
            << " expanded=" << expanded
            << " generated=" << generated
            << " expanded_nodes_per_second="
            << static_cast<std::uint64_t>(expanded / seconds)
            << " complete_successors_per_second="
            << static_cast<std::uint64_t>(generated / seconds)
            << " elapsed_seconds=" << seconds << '\n';
}
