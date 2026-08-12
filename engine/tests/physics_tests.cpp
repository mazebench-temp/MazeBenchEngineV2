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

void TestPlayerAndPushedBodySlideTogetherOnIce() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {2, 4, 1, Role("player"), -1},
      {2, 3, 1, Role("weightless-pushable"), 0},
      {2, 4, 0, Role("floor"), -1},
      {2, 3, 0, Role("ice"), -1},
      {2, 2, 0, Role("ice"), -1},
      {2, 1, 0, Role("ice"), -1},
      {2, 0, 0, Role("solid"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 7, 5, 5, 0) ==
            voxelbench::TickResult::kMore,
        "pushing from Floor onto Ice should retain object momentum");
  Check(state.tick == 1 && voxels[0].y == 3 && voxels[1].y == 2,
        "the initial push should move the player and body one cell");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 7, 5, 5, 0) ==
            voxelbench::TickResult::kMore,
        "the player and pushed body should keep sliding together");
  Check(state.tick == 2 && voxels[0].y == 2 && voxels[1].y == 1,
        "the second Ice tick should advance both horizontal proposals");
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

void TestTickTraceAndWorkspaceIsolation() {
  static voxelbench::PhysicsWorkspace first_workspace;
  static voxelbench::PhysicsWorkspace second_workspace;
  static voxelbench::MotionState first_state;
  static voxelbench::MotionState second_state;
  voxelbench::Voxel first[] = {
      {2, 4, 1, Role("player"), -1},
      {2, 3, 0, Role("ice"), -1},
      {2, 2, 0, Role("ice"), -1},
      {2, 1, 0, Role("ice"), -1},
      {2, 0, 0, Role("solid"), -1},
  };
  voxelbench::Voxel second[] = {
      {1, 2, -3, Role("player"), -1},
      {2, 2, -4, Role("solid"), -1},
  };
  voxelbench::reset_workspace(&first_workspace);
  voxelbench::reset_workspace(&second_workspace);
  voxelbench::reset_motion_state(&first_state);
  voxelbench::reset_motion_state(&second_state);

  Check(voxelbench::step_tick(
            &first_workspace, &first_state, first, 5, 5, 5, 0) ==
            voxelbench::TickResult::kMore,
        "the first Ice tick should leave horizontal momentum");
  Check(first_state.tick == 1 && first[0].y == 3,
        "step_tick should advance exactly one Ice cell");
  Check(voxelbench::step_tick(
            &second_workspace, &second_state, second, 2, 5, 5, 1) ==
            voxelbench::TickResult::kComplete,
        "an independent workspace should complete its own command");
  Check(second_state.tick == 1 && second[0].x == 2 && second[0].y == 2,
        "the second workspace should not inherit the first command");

  while (voxelbench::step_tick(
             &first_workspace, &first_state, first, 5, 5, 5, 0) ==
         voxelbench::TickResult::kMore) {
  }
  Check(first_state.tick == 4 && first[0].y == 0,
        "the resumed Ice trace should contain four committed ticks");
  Check(first_state.version == voxelbench::kMotionStateVersion,
        "motion state should carry its serializable format version");
}

void TestPlayerGetsVisibleRowZeroVoidFrame() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 1, 1, Role("player"), -1},
      {1, 1, 0, Role("floor"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 2, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "walking off Floor should begin a visible void fall");
  Check(state.tick == 1 && voxels[0].x == 1 && voxels[0].y == 0 &&
            voxels[0].z == 1,
        "the first void frame should contain only the horizontal step");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 2, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "the first gravity tick should retain the player on row zero");
  Check(state.tick == 2 && voxels[0].x == 1 && voxels[0].z == 0,
        "the first downward frame should place the player on row zero");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 2, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "the first row below zero should remain a visible tick");
  Check(state.tick == 3 && voxels[0].x == 1 && voxels[0].z == -1,
        "the player should remain visible at row minus one");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 2, 3, 3, 0) ==
            voxelbench::TickResult::kComplete,
        "the next unsupported fall should complete the command");
  Check(state.tick == 4 && voxels[0].x == -1 && voxels[0].z == -2,
        "the player should disappear after its visible row-minus-one tick");
}

void TestPushableGetsVisibleRowZeroVoidFrame() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 2, 1, Role("player"), -1},
      {1, 1, 1, Role("pushable"), -1},
      {1, 2, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 4, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "a pushable leaving support should continue through gravity ticks");
  Check(state.tick == 1 && voxels[1].x == 1 && voxels[1].y == 0 &&
            voxels[1].z == 1,
        "the pushed body's horizontal tick should not also change Z");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 4, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "the first pushable gravity tick should reach row zero");
  Check(state.tick == 2 && voxels[1].x == 1 && voxels[1].z == 0,
        "the pushed body should remain visible on row zero");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 4, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "the unsupported pushable should remain visible below row zero");
  Check(state.tick == 3 && voxels[1].x == 1 && voxels[1].z == -1,
        "the pushed body should have a visible row-minus-one frame");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 4, 3, 3, 0) ==
            voxelbench::TickResult::kComplete,
        "the pushable should disappear on the following unsupported fall");
  Check(state.tick == 4 && voxels[1].x == -1 && voxels[1].z == -2,
        "the pushed body should disappear after its row-minus-one frame");
}

void TestTallPolycubeDisappearsOnlyAfterItsTopPassesRowZero() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 2, 1, Role("player"), -1},
      {1, 1, 1, Role("weightless-pushable"), 0},
      {1, 1, 2, Role("weightless-pushable"), 0},
      {1, 2, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 5, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "the tall polycube push should start a gravity trace");
  Check(voxels[1].y == 0 && voxels[1].z == 1 && voxels[2].z == 2,
        "the horizontal polycube tick should preserve both heights");
  for (int32_t tick = 0; tick < 3; ++tick) {
    Check(voxelbench::step_tick(
              &workspace, &state, voxels, 5, 3, 3, 0) ==
              voxelbench::TickResult::kMore,
          "the tall polycube should remain visible while any cube reaches row zero");
  }
  Check(voxels[1].x == 1 && voxels[1].z == -2 &&
            voxels[2].x == 1 && voxels[2].z == -1,
        "the whole polycube should still be visible with its top at row minus one");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 5, 3, 3, 0) ==
            voxelbench::TickResult::kComplete,
        "the tall polycube should disappear on its following unsupported tick");
  Check(voxels[1].x == -1 && voxels[2].x == -1,
        "all members of the tall polycube should disappear together");
}

void TestPolycubeAbyssUsesLowestOtherWorldGeometry() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 2, 1, Role("player"), -1},
      {1, 1, 1, Role("weightless-pushable"), 0},
      {1, 1, 2, Role("weightless-pushable"), 0},
      {1, 2, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
      {0, 2, -5, Role("solid"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 6, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "the deep-world polycube push should start a gravity trace");
  for (int32_t tick = 0; tick < 8; ++tick) {
    Check(voxelbench::step_tick(
              &workspace, &state, voxels, 6, 3, 3, 0) ==
              voxelbench::TickResult::kMore,
          "negative world geometry should extend the visible abyss trace");
  }
  Check(voxels[1].x == 1 && voxels[1].z == -7 &&
            voxels[2].x == 1 && voxels[2].z == -6,
        "the polycube should remain visible after its top passes row minus five");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 6, 3, 3, 0) ==
            voxelbench::TickResult::kComplete,
        "the next unsupported fall should remove the deep-world polycube");
  Check(voxels[1].x == -1 && voxels[2].x == -1,
        "the deep-world polycube should disappear as one rigid body");
}

void TestPlayerAbyssUsesLowestOtherWorldGeometry() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 1, 1, Role("player"), -1},
      {1, 1, 0, Role("floor"), -1},
      {0, 1, -3, Role("solid"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 3, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "walking into a deep world void should start a gravity trace");
  for (int32_t tick = 0; tick < 5; ++tick) {
    Check(voxelbench::step_tick(
              &workspace, &state, voxels, 3, 3, 3, 0) ==
              voxelbench::TickResult::kMore,
          "the player should remain visible through the world's lowest row");
  }
  Check(voxels[0].x == 1 && voxels[0].z == -4,
        "the player should have a visible frame below row minus three");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 3, 3, 3, 0) ==
            voxelbench::TickResult::kComplete,
        "the player should disappear on the following unsupported fall");
  Check(voxels[0].x == -1 && voxels[0].z == -5,
        "the player should be removed below all other world geometry");
}

void TestFallingRiderDoesNotExtendItsCarriersAbyss() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 2, 2, Role("player"), -1},
      {1, 2, 1, Role("weightless-pushable"), 0},
      {1, 1, 1, Role("weightless-pushable"), 0},
      {1, 1, 2, Role("weightless-pushable"), 0},
      {1, 2, 0, Role("floor"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 5, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "pushing a ridden polycube off support should start its fall");
  for (int32_t tick = 0; tick < 3; ++tick) {
    Check(voxelbench::step_tick(
              &workspace, &state, voxels, 5, 3, 3, 0) ==
              voxelbench::TickResult::kMore,
          "a ridden polycube should retain its visible void frames");
  }
  Check(voxels[0].x == 1 && voxels[0].z == -1 && voxels[3].z == -1,
        "the player and carrier should descend together below row zero");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 5, 3, 3, 0) ==
            voxelbench::TickResult::kComplete,
        "the falling rider must not keep its carrier alive forever");
  Check(voxels[0].x < 0 && voxels[1].x < 0 && voxels[2].x < 0 &&
            voxels[3].x < 0,
        "the rider and its carrier should leave the room together");
}

void TestObjectAboveDescendingPlayerFallsInSameTick() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 1, 1, Role("player"), -1},
      {1, 1, 2, Role("weightless-pushable"), 0},
      {1, 1, 0, Role("ice"), -1},
      {1, 0, -1, Role("floor"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 4, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "the player and its passenger should first move horizontally together");
  Check(voxels[0].y == 0 && voxels[0].z == 1 &&
            voxels[1].y == 0 && voxels[1].z == 2,
        "horizontal passenger motion should not also change height");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 4, 3, 3, 0) ==
            voxelbench::TickResult::kComplete,
        "the player and its passenger should complete one synchronized fall");
  Check(voxels[0].z == 0 && voxels[1].z == 1,
        "the passenger should fall with and remain above the player");
}

}  // namespace

int main() {
  TestSimplePush();
  TestPlayerIceSlide();
  TestPushableIceSlide();
  TestPlayerAndPushedBodySlideTogetherOnIce();
  TestIceStopsAtObstacle();
  TestUnknownRoleBlocks();
  TestEveryBoundary();
  TestTickTraceAndWorkspaceIsolation();
  TestPlayerGetsVisibleRowZeroVoidFrame();
  TestPushableGetsVisibleRowZeroVoidFrame();
  TestTallPolycubeDisappearsOnlyAfterItsTopPassesRowZero();
  TestPolycubeAbyssUsesLowestOtherWorldGeometry();
  TestPlayerAbyssUsesLowestOtherWorldGeometry();
  TestFallingRiderDoesNotExtendItsCarriersAbyss();
  TestObjectAboveDescendingPlayerFallsInSameTick();
  if (failures != 0) {
    std::cerr << failures << " C++ physics test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "all 15 C++ physics tests passed\n";
  return EXIT_SUCCESS;
}
