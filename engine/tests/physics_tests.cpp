#include "voxelbench/physics.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

uint32_t Role(const char* value) {
  int32_t length = 0;
  while (value[length] != '\0') ++length;
  return voxelbench::hash_role(reinterpret_cast<const uint8_t*>(value), length);
}

void TestSimplePush() {
  voxelbench::Voxel voxels[] = {
      {2, 2, -7, Role("player"), -1},
      {2, 1, -7, Role("pushable"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 2, 5, 5, 0) == 0, "push command should run");
  Check(voxels[0].x == 2 && voxels[0].y == 1 && voxels[0].z == -7,
        "player should enter the pushed voxel's old cell without changing Z");
  Check(voxels[1].x == 2 && voxels[1].y == 0 && voxels[1].z == -7,
        "pushable should move one cell without changing Z");
}

void TestPlayerIceSlide() {
  voxelbench::Voxel voxels[] = {
      {2, 4, 1, Role("player"), -1},
      {2, 3, 0, Role("ice"), -1},
      {2, 2, 0, Role("ice"), -1},
      {2, 1, 0, Role("ice"), -1},
      {2, 0, 0, Role("solid"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 5, 5, 5, 0) == 0, "ice command should run");
  Check(voxels[0].x == 2 && voxels[0].y == 0,
        "player should cross the Ice strip and stop on normal floor");
}

void TestPushableIceSlide() {
  voxelbench::Voxel voxels[] = {
      {2, 4, 1, Role("player"), -1},
      {2, 3, 1, Role("pushable"), 17},
      {2, 2, 0, Role("ice"), -1},
      {2, 1, 0, Role("ice"), -1},
      {2, 0, 0, Role("solid"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 5, 5, 5, 0) == 0,
        "push onto Ice command should run");
  Check(voxels[0].y == 3, "player should enter the object's vacated cell");
  Check(voxels[1].y == 0 && voxels[1].generic_id == 17,
        "pushed object should slide off Ice while preserving its generic ID");
}

void TestIceStopsAtObstacle() {
  voxelbench::Voxel voxels[] = {
      {2, 4, 1, Role("player"), -1},
      {2, 3, 0, Role("ice"), -1},
      {2, 2, 0, Role("ice"), -1},
      {2, 1, 1, Role("solid"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 4, 5, 5, 0) == 0,
        "blocked Ice command should run");
  Check(voxels[0].y == 2, "player should stop on Ice immediately before an obstacle");
}

void TestUnknownRoleBlocks() {
  voxelbench::Voxel voxels[] = {
      {1, 2, 1, Role("player"), -1},
      {1, 1, 1, Role("future-role"), 912},
  };
  Check(voxelbench::simulate_turn(voxels, 2, 5, 5, 0) == 0,
        "unknown role command should run");
  Check(voxels[0].y == 2 && voxels[1].generic_id == 912,
        "unknown roles should block and preserve generic IDs");
}

void TestEveryBoundary() {
  constexpr int32_t positions[][3] = {{2, 0, 0}, {4, 2, 1}, {2, 4, 2}, {0, 2, 3}};
  for (const auto& value : positions) {
    voxelbench::Voxel player{value[0], value[1], 1, Role("player"), -1};
    Check(voxelbench::simulate_turn(&player, 1, 5, 5, value[2]) == 0,
          "boundary command should run");
    Check(player.x == value[0] && player.y == value[1],
          "room boundary should stop movement");
  }
}

}  // namespace

int main() {
  TestSimplePush();
  TestPlayerIceSlide();
  TestPushableIceSlide();
  TestIceStopsAtObstacle();
  TestUnknownRoleBlocks();
  TestEveryBoundary();
  if (failures != 0) {
    std::cerr << failures << " C++ physics test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "all 6 C++ physics tests passed\n";
  return EXIT_SUCCESS;
}
