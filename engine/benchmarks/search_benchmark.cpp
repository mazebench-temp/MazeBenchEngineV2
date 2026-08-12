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
  static constexpr const char* kRows[16] = {
      "################",
      "#......#...#...#",
      "#..###.#.#...#.#",
      "#.#..#.#..###..#",
      "#..##..#.#.....#",
      "##.##.#..#.##..#",
      "##..#..#.#..#..#",
      "#.#..#.#..#.@CC#",
      "#...#...#.A##C.#",
      "#..##..##AA..#.#",
      "#.##..#..AA..#.#",
      "#..##...#......#",
      "#..#####.#.B...#",
      "##..###...#BBB.#",
      "###.....#..BX..#",
      "################",
  };
  voxelbench::Voxel voxels[512];
  int32_t count = 0;
  for (int32_t y = 0; y < 16; ++y) {
    for (int32_t x = 0; x < 16; ++x) {
      voxels[count++] = {x, y, 0, Role("floor"), -1};
      const char cell = kRows[y][x];
      if (cell == '#') {
        voxels[count++] = {x, y, 1, Role("solid"), -1};
      } else if (cell == '@') {
        voxels[count++] = {x, y, 1, Role("player"), -1};
      } else if (cell == 'X') {
        voxels[count++] = {x, y, 1, Role("goal"), -1};
      } else if (cell >= 'A' && cell <= 'Z') {
        voxels[count++] = {
            x, y, 1, Role("weightless-pushable"), cell - 'A'};
      }
    }
  }

  constexpr int32_t kIterations = 2000;
  std::uint64_t expanded = 0;
  std::uint64_t generated = 0;
  std::uint64_t solved = 0;
  std::uint64_t moves = 0;
  voxelbench::reset_workspace(&physics_workspace);
  const auto started = std::chrono::steady_clock::now();
  for (int32_t iteration = 0; iteration < kIterations; ++iteration) {
    const auto result = voxelbench::search_shortest(
        &search_workspace,
        &physics_workspace,
        voxels,
        count,
        16,
        16,
        voxelbench::kSearchNodeCapacity);
    expanded += static_cast<std::uint64_t>(result.expanded);
    generated += static_cast<std::uint64_t>(result.generated);
    solved += result.status == voxelbench::SearchStatus::kSolved ? 1 : 0;
    moves += static_cast<std::uint64_t>(result.moves);
  }
  const double seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
  std::cout << "workload=macro_16x16_weightless_317"
            << " solves=" << kIterations
            << " solved=" << solved
            << " average_moves=" << moves / kIterations
            << " expanded=" << expanded
            << " generated=" << generated
            << " exact_solves_per_second="
            << static_cast<std::uint64_t>(kIterations / seconds)
            << " expanded_nodes_per_second="
            << static_cast<std::uint64_t>(expanded / seconds)
            << " complete_successors_per_second="
            << static_cast<std::uint64_t>(generated / seconds)
            << " elapsed_seconds=" << seconds << '\n';
}
