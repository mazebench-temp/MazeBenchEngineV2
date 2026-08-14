#include "voxelbench/physics.hpp"
#include "voxelbench/search.hpp"

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>

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

struct ObserverTrace {
  int32_t calls = 0;
  int32_t ticks[4]{};
  int32_t goal_x[4]{};
};

void CaptureObserverFrame(
    const voxelbench::Voxel* voxels,
    int32_t count,
    const voxelbench::MotionState* state,
    void* context) {
  ObserverTrace* trace = static_cast<ObserverTrace*>(context);
  if (trace->calls >= 4) return;
  const int32_t frame = trace->calls++;
  trace->ticks[frame] = state->tick;
  trace->goal_x[frame] = -1;
  for (int32_t index = 0; index < count; ++index) {
    if (voxels[index].role == Role("goal")) {
      trace->goal_x[frame] = voxels[index].x;
      break;
    }
  }
}

void TestSimplePush() {
  voxelbench::Voxel voxels[] = {
      {2, 2, -7, Role("player"), -1},
      {2, 1, -7, Role("pushable"), -1},
      {2, 2, -8, Role("floor"), -1},
      {2, 1, -8, Role("floor"), -1},
      {2, 0, -8, Role("floor"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 5, 5, 5, 0) == 0, "push command should run");
  Check(voxels[0].x == 2 && voxels[0].y == 1 && voxels[0].z == -7,
        "player should enter the pushed voxel's old cell without changing Z");
  Check(voxels[1].x == 2 && voxels[1].y == 0 && voxels[1].z == -7,
        "pushable should move one cell without changing Z");
}

void TestPlayerIceSlide() {
  voxelbench::Voxel voxels[] = {
      {2, 4, 1, Role("player"), -1},
      {2, 4, 0, Role("floor"), -1},
      {2, 3, 0, Role("ice"), -1},
      {2, 2, 0, Role("ice"), -1},
      {2, 1, 0, Role("ice"), -1},
      {2, 0, 0, Role("solid"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 6, 5, 5, 0) == 0, "ice command should run");
  Check(voxels[0].x == 2 && voxels[0].y == 0,
        "player should cross the Ice strip and stop on normal floor");
}

void TestPushableIceSlide() {
  voxelbench::Voxel voxels[] = {
      {2, 4, 1, Role("player"), -1},
      {2, 3, 1, Role("pushable"), 17},
      {2, 4, 0, Role("floor"), -1},
      {2, 3, 0, Role("floor"), -1},
      {2, 2, 0, Role("ice"), -1},
      {2, 1, 0, Role("ice"), -1},
      {2, 0, 0, Role("solid"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 7, 5, 5, 0) == 0,
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
      {2, 4, 0, Role("floor"), -1},
      {2, 3, 0, Role("ice"), -1},
      {2, 2, 0, Role("ice"), -1},
      {2, 1, 1, Role("solid"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 5, 5, 5, 0) == 0,
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
    voxelbench::Voxel voxels[] = {
        {value[0], value[1], 1, Role("player"), -1},
        {value[0], value[1], 0, Role("floor"), -1},
    };
    Check(voxelbench::simulate_turn(voxels, 2, 5, 5, value[2]) == 0,
          "boundary command should run");
    Check(voxels[0].x == value[0] && voxels[0].y == value[1],
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
      {2, 4, 0, Role("floor"), -1},
      {2, 3, 0, Role("ice"), -1},
      {2, 2, 0, Role("ice"), -1},
      {2, 1, 0, Role("ice"), -1},
      {2, 0, 0, Role("solid"), -1},
  };
  voxelbench::Voxel second[] = {
      {1, 2, -3, Role("player"), -1},
      {1, 2, -4, Role("floor"), -1},
      {2, 2, -4, Role("solid"), -1},
  };
  voxelbench::reset_workspace(&first_workspace);
  voxelbench::reset_workspace(&second_workspace);
  voxelbench::reset_motion_state(&first_state);
  voxelbench::reset_motion_state(&second_state);

  Check(voxelbench::step_tick(
            &first_workspace, &first_state, first, 6, 5, 5, 0) ==
            voxelbench::TickResult::kMore,
        "the first Ice tick should leave horizontal momentum");
  Check(first_state.tick == 1 && first[0].y == 3,
        "step_tick should advance exactly one Ice cell");
  Check(voxelbench::step_tick(
            &second_workspace, &second_state, second, 3, 5, 5, 1) ==
            voxelbench::TickResult::kComplete,
        "an independent workspace should complete its own command");
  Check(second_state.tick == 1 && second[0].x == 2 && second[0].y == 2,
        "the second workspace should not inherit the first command");

  while (voxelbench::step_tick(
             &first_workspace, &first_state, first, 6, 5, 5, 0) ==
         voxelbench::TickResult::kMore) {
  }
  Check(first_state.tick == 4 && first[0].y == 0,
        "the resumed Ice trace should contain four committed ticks");
  Check(first_state.version == voxelbench::kMotionStateVersion,
        "motion state should carry its serializable format version");
}

void TestObserverReceivesNoMovementCompletionFrame() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 3, 1, Role("player"), -1},
      {1, 3, 0, Role("floor"), -1},
      {1, 2, 0, Role("ice"), -1},
      {1, 1, 0, Role("ice"), -1},
      {1, 1, 1, Role("wall"), -1},
      {1, 2, 1, Role("goal"), -1},
  };
  ObserverTrace trace;
  voxelbench::reset_workspace(&workspace);
  Check(voxelbench::simulate_command(
            &workspace,
            &state,
            voxels,
            6,
            4,
            4,
            0,
            CaptureObserverFrame,
            &trace) == 0,
        "a blocked Ice completion should simulate successfully");
  Check(trace.calls == 2 && trace.ticks[0] == 1 && trace.ticks[1] == 1,
        "the observer should receive both the movement and no-movement final frame");
  Check(trace.goal_x[0] == 1 && trace.goal_x[1] < 0,
        "the final observer frame must include end-of-command gem collection");
}

void TestPreparedMotionStateClearsImmutableSuffix() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 1, 1, Role("player"), -1},
      {1, 1, 0, Role("floor"), -1},
      {1, 0, 0, Role("floor"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  Check(voxelbench::prepare_scene(&workspace, voxels, 3, 3, 3, 1),
        "the motion-state fixture should prepare its dynamic prefix");
  for (int32_t index = 0; index < 3; ++index) {
    state.horizontal_momentum[index] = 0xff;
    state.falling[index] = 0xff;
    state.gravity_armed[index] = 0xff;
  }
  Check(voxelbench::simulate_command(
            &workspace, &state, voxels, 3, 3, 3, 0) == 0,
        "the prepared motion-state command should complete");
  bool suffix_is_clear = true;
  for (int32_t index = 1; index < 3; ++index) {
    suffix_is_clear = suffix_is_clear &&
        state.horizontal_momentum[index] == 0 &&
        state.falling[index] == 0 && state.gravity_armed[index] == 0;
  }
  Check(suffix_is_clear,
        "serialized motion flags for immutable prepared voxels must be deterministic");
}

void TestPreparedSceneRejectsMovableStaticSuffix() {
  static voxelbench::PhysicsWorkspace workspace;
  voxelbench::Voxel voxels[] = {
      {1, 2, 1, Role("player"), -1},
      {1, 1, 1, Role("weightless-pushable"), 0},
      {1, 2, 0, Role("floor"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  Check(!voxelbench::prepare_scene(&workspace, voxels, 3, 4, 4, 1),
        "prepared scenes must reject movable voxels in the immutable suffix");
}

void TestPreparedIndexesDoNotLeakToDifferentSceneShape() {
  static voxelbench::PhysicsWorkspace workspace;
  voxelbench::Voxel voxels[18];
  int32_t count = 0;
  voxels[count++] = {2, 3, 1, Role("player"), -1};
  voxels[count++] = {2, 3, 0, Role("floor"), -1};
  voxels[count++] = {2, 2, 0, Role("floor"), -1};
  for (int32_t y = 0; y < 5 && count < 17; ++y) {
    for (int32_t x = 0; x < 5 && count < 17; ++x) {
      if ((x == 2 && y == 3) || (x == 2 && y == 2)) continue;
      voxels[count++] = {x, y, 0, Role("floor"), -1};
    }
  }
  voxels[17] = {2, 2, 1, Role("solid"), -1};
  voxelbench::reset_workspace(&workspace);
  Check(voxelbench::prepare_scene(&workspace, voxels, 18, 5, 5, 1),
        "the full scene should prepare successfully");
  Check(voxelbench::simulate_turn(&workspace, voxels, 17, 5, 5, 0) == 0,
        "a differently sized scene using the same buffer should run normally");
  Check(voxels[0].x == 2 && voxels[0].y == 2,
        "an excluded stale wall must not remain in the prepared spatial index");
}

void TestInvalidQuiescentCallDoesNotLeakItsAssumption() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 2, 2, Role("player"), -1},
      {1, 2, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  Check(voxelbench::prepare_scene(&workspace, voxels, 3, 4, 4, 1),
        "the floating-player scene should prepare successfully");
  Check(voxelbench::simulate_quiescent_turn(
            &workspace, voxels, 3, 4, 4, 9) == -1,
        "an invalid quiescent command should be rejected");
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 3, 4, 4, 0) ==
            voxelbench::TickResult::kMore,
        "the next ordinary command should begin by settling the player");
  Check(voxels[0].x == 1 && voxels[0].y == 2 && voxels[0].z == 1,
        "an invalid quiescent call must not skip initial gravity later");
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

void TestExactSearchFindsShortestCommands() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[] = {
      {1, 2, 1, Role("player"), -1},
      {1, 2, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
      {1, 0, 0, Role("floor"), -1},
      {1, 0, 1, Role("goal"), -1},
  };
  voxelbench::reset_workspace(&physics_workspace);
  const auto result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, 5, 3, 3, 1000);
  Check(result.status == voxelbench::SearchStatus::kSolved,
        "exact search should solve a supported corridor");
  Check(result.moves == 2 && result.solution_length == 2,
        "exact search should prove the two-command optimum");
  Check(result.solution[0] == 0 && result.solution[1] == 0,
        "exact search should reconstruct both Up commands");
}

void TestFlatIceSearchKeepsExactCommandSemantics() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[] = {
      {2, 4, 1, Role("player"), -1},
      {2, 4, 0, Role("floor"), -1},
      {2, 3, 0, Role("ice"), -1},
      {2, 2, 0, Role("ice"), -1},
      {2, 1, 0, Role("ice"), -1},
      {2, 0, 0, Role("floor"), -1},
      {2, 0, 1, Role("goal"), -1},
  };
  voxelbench::reset_workspace(&physics_workspace);
  const auto result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, 7, 5, 5, 1000);
  Check(result.status == voxelbench::SearchStatus::kSolved && result.moves == 1,
        "flat Ice search should preserve a complete slide as one command");
  Check(result.solution_length == 1 && result.solution[0] == 0,
        "flat Ice search should reconstruct the exact Up slide");
}

void TestGeneralSearchSupportsBoardsWiderThanSixteen() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[22];
  int32_t count = 0;
  for (int32_t x = 0; x < 20; ++x) {
    voxels[count++] = {x, 1, 0, Role("floor"), -1};
  }
  voxels[count++] = {1, 1, 1, Role("player"), -1};
  voxels[count++] = {18, 1, 1, Role("goal"), -1};
  voxelbench::reset_workspace(&physics_workspace);
  const auto result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, count, 20, 4, 1000);
  Check(result.status == voxelbench::SearchStatus::kSolved && result.moves == 17,
        "generalized search should support footprints wider than sixteen cells");
  Check(result.solution_length == 17,
        "wide-board search should reconstruct every exact command");
}

void TestGeneralSearchCollapsesWalkingBeforePushes() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[] = {
      {1, 1, 1, Role("player"), -1},
      {4, 1, 1, Role("pushable"), -1},
      {0, 1, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
      {2, 1, 0, Role("floor"), -1},
      {3, 1, 0, Role("floor"), -1},
      {4, 1, 0, Role("floor"), -1},
      {5, 1, 0, Role("floor"), -1},
      {6, 1, 0, Role("floor"), -1},
      {5, 1, 1, Role("goal"), -1},
  };
  voxelbench::reset_workspace(&physics_workspace);
  const auto result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, 10, 7, 3, 1000);
  Check(result.status == voxelbench::SearchStatus::kSolved && result.moves == 4,
        "generalized search should preserve the exact walk-plus-push command cost");
  Check(result.solution_length == 4 && result.solution[0] == 1 &&
            result.solution[1] == 1 && result.solution[2] == 1 &&
            result.solution[3] == 1,
        "generalized search should reconstruct walking commands before both pushes");
  Check(result.expanded <= 3,
        "ordinary corridor walking should not occupy global search nodes");
}

void TestGeneralSearchMatchesMazeBenchEngine3LongRoom() {
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
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
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
  voxelbench::reset_workspace(&physics_workspace);
  const auto result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, count, 16, 16, 50000);
  Check(result.status == voxelbench::SearchStatus::kSolved && result.moves == 317,
        "generalized exact search should retain MazeBenchEngine3's 317-command optimum");
  Check(result.expanded < 1000,
        "collapsed local reachability should avoid global footstep-state explosion");
}

void TestSearchInitializesFreshWorkspace() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  std::unique_ptr<voxelbench::SearchWorkspace> search_workspace(
      new voxelbench::SearchWorkspace);
  std::memset(
      search_workspace->storage, 0x5a, sizeof(search_workspace->storage));
  voxelbench::Voxel voxels[] = {
      {1, 2, 1, Role("player"), -1},
      {1, 2, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
      {1, 0, 0, Role("floor"), -1},
      {1, 0, 1, Role("goal"), -1},
  };
  voxelbench::reset_workspace(&physics_workspace);
  const auto result = voxelbench::search_shortest(
      search_workspace.get(), &physics_workspace, voxels, 5, 3, 3, 1000);
  Check(result.status == voxelbench::SearchStatus::kSolved && result.moves == 2,
        "a freshly constructed search workspace must initialize its hash stamps");
}

void TestSearchPrunesPlayerGameOverBranches() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[] = {
      {1, 1, 1, Role("player"), -1},
      {1, 1, 0, Role("floor"), -1},
      {0, 0, 1, Role("goal"), -1},
      {2, 2, 1, Role("goal"), -1},
  };
  voxelbench::reset_workspace(&physics_workspace);
  const auto result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, 4, 3, 3, 100);
  Check(result.status == voxelbench::SearchStatus::kUnsolved,
        "a room whose commands all kill the player should be unsolved");
  Check(result.expanded == 1 && result.generated == 0,
        "player disappearance must be pruned before dead states are inserted");
}

void TestPlayerCollectsGemOnlyAtCommandEnd() {
  voxelbench::Voxel voxels[] = {
      {1, 2, 1, Role("player"), -1},
      {1, 2, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
      {1, 1, 1, Role("goal"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 4, 3, 3, 0) == 0,
        "walking into a gem should complete normally");
  Check(voxels[0].x == 1 && voxels[0].y == 1 && voxels[0].z == 1,
        "the gem must not block the player");
  Check(voxels[3].x < 0,
        "the gem should disappear when the player ends the command on it");
}

void TestBoxMayOverlapGemWithoutCollectingIt() {
  voxelbench::Voxel voxels[] = {
      {1, 2, 1, Role("player"), -1},
      {1, 1, 1, Role("pushable"), -1},
      {1, 2, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
      {1, 0, 0, Role("floor"), -1},
      {1, 0, 1, Role("goal"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 6, 3, 3, 0) == 0,
        "a box should be pushable into a gem");
  Check(voxels[1].x == 1 && voxels[1].y == 0 && voxels[1].z == 1,
        "the box should overlap the non-rigid gem");
  Check(voxels[5].x == 1 && voxels[5].y == 0,
        "a box must not collect the gem");
}

void TestSlidingAcrossGemDoesNotCollectIt() {
  voxelbench::Voxel voxels[] = {
      {1, 3, 1, Role("player"), -1},
      {1, 3, 0, Role("floor"), -1},
      {1, 2, 0, Role("ice"), -1},
      {1, 1, 0, Role("ice"), -1},
      {1, 0, 0, Role("floor"), -1},
      {1, 2, 1, Role("goal"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 6, 4, 4, 0) == 0,
        "sliding through a gem should complete normally");
  Check(voxels[0].y == 0,
        "the player should continue past a gem encountered mid-command");
  Check(voxels[5].x == 1 && voxels[5].y == 2,
        "a gem crossed before command end must remain collectible");
}

void TestPlayerSettlesBeforeHorizontalInput() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 2, 3, Role("player"), -1},
      {1, 2, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 3, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "a suspended player should begin settling before input");
  Check(state.tick == 1 && voxels[0].y == 2 && voxels[0].z == 2,
        "the first pre-command tick should contain gravity only");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 3, 3, 3, 0) ==
            voxelbench::TickResult::kMore,
        "the player should finish settling before the command runs");
  Check(state.tick == 2 && voxels[0].y == 2 && voxels[0].z == 1,
        "the player should land without horizontal displacement");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 3, 3, 3, 0) ==
            voxelbench::TickResult::kComplete,
        "the requested command should run after settling");
  Check(state.tick == 3 && voxels[0].y == 1 && voxels[0].z == 1,
        "horizontal movement should start only from the settled state");
}

void TestPolycubeSettlesAsOneBodyBeforeHorizontalInput() {
  static voxelbench::PhysicsWorkspace workspace;
  static voxelbench::MotionState state;
  voxelbench::Voxel voxels[] = {
      {1, 3, 1, Role("player"), -1},
      {1, 1, 3, Role("weightless-pushable"), 4},
      {2, 1, 3, Role("weightless-pushable"), 4},
      {1, 3, 0, Role("floor"), -1},
      {2, 3, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
      {2, 1, 0, Role("floor"), -1},
  };
  voxelbench::reset_workspace(&workspace);
  voxelbench::reset_motion_state(&state);
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 7, 4, 4, 1) ==
            voxelbench::TickResult::kMore,
        "a suspended polycube should begin settling before input");
  Check(voxels[0].x == 1 && voxels[1].z == 2 && voxels[2].z == 2,
        "the polycube should descend rigidly while the player waits");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 7, 4, 4, 1) ==
            voxelbench::TickResult::kMore,
        "the polycube should land before horizontal input");
  Check(voxels[0].x == 1 && voxels[1].z == 1 && voxels[2].z == 1,
        "all polycube members should land in the same tick");
  Check(voxelbench::step_tick(
            &workspace, &state, voxels, 7, 4, 4, 1) ==
            voxelbench::TickResult::kComplete,
        "the command should resume once every body is settled");
  Check(voxels[0].x == 2 && voxels[0].y == 3,
        "the player should move only after the polycube lands");
}

void TestSearchRequiresACommandToCollectStartingGem() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[] = {
      {0, 0, 1, Role("player"), -1},
      {0, 0, 0, Role("floor"), -1},
      {0, 0, 1, Role("goal"), -1},
  };
  voxelbench::reset_workspace(&physics_workspace);
  const auto result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, 3, 1, 1, 100);
  Check(result.status == voxelbench::SearchStatus::kSolved && result.moves == 1,
        "search should treat the gem as collected only after a command ends");
}

void TestPushCannotWalkPlayerOffWallSupport() {
  voxelbench::Voxel voxels[] = {
      {1, 2, 2, Role("player"), -1},
      {1, 1, 1, Role("weightless-pushable"), 8},
      {1, 1, 2, Role("weightless-pushable"), 8},
      {1, 2, 1, Role("wall"), -1},
      {1, 1, 0, Role("floor"), -1},
      {1, 0, 0, Role("floor"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 6, 3, 3, 0) == 0,
        "a blocked wall-supported push should complete normally");
  Check(voxels[0].y == 2 && voxels[0].z == 2 &&
            voxels[1].y == 1 && voxels[2].y == 1,
        "pushing must not let the player walk off a non-Floor support");
}

void TestSearchCollectsEveryGem() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[] = {
      {1, 3, 1, Role("player"), -1},
      {1, 3, 0, Role("floor"), -1},
      {1, 2, 0, Role("floor"), -1},
      {1, 1, 0, Role("floor"), -1},
      {1, 0, 0, Role("floor"), -1},
      {1, 2, 1, Role("goal"), -1},
      {1, 0, 1, Role("goal"), -1},
  };
  voxelbench::reset_workspace(&physics_workspace);
  const auto result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, 7, 4, 4, 1000);
  Check(result.status == voxelbench::SearchStatus::kSolved && result.moves == 3,
        "search should retain partial collection state until every gem is gone");
  Check(result.solution_length == 3 && result.solution[0] == 0 &&
            result.solution[1] == 0 && result.solution[2] == 0,
        "the multi-gem shortest path should collect both corridor gems");
}

void TestSearchStoresLargePolycubeAsOneEntity() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[48];
  int32_t count = 0;
  voxels[count++] = {0, 2, 1, Role("player"), -1};
  voxels[count++] = {0, 2, 0, Role("floor"), -1};
  voxels[count++] = {0, 1, 0, Role("floor"), -1};
  voxels[count++] = {0, 0, 0, Role("floor"), -1};
  voxels[count++] = {0, 0, 1, Role("goal"), -1};
  for (int32_t y = 3; y <= 6; ++y) {
    for (int32_t x = 2; x <= 6; ++x) {
      voxels[count++] = {x, y, 0, Role("floor"), -1};
      voxels[count++] = {x, y, 1, Role("weightless-pushable"), 37};
    }
  }
  voxelbench::reset_workspace(&physics_workspace);
  const auto result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, count, 8, 8, 1000);
  Check(result.status == voxelbench::SearchStatus::kSolved && result.moves == 2,
        "a polycube larger than the old moving-voxel cap should remain searchable");
}

void TestGeneralSearchChecksRaisedPolycubeCollisions() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[] = {
      {0, 0, 1, Role("player"), -1},
      {1, 0, 1, Role("weightless-pushable"), 4},
      {2, 0, 2, Role("weightless-pushable"), 4},
      {1, 0, 1, Role("goal"), -1},
      {3, 0, 1, Role("wall"), -1},
      {0, 0, 0, Role("floor"), -1},
      {1, 0, 0, Role("floor"), -1},
      {2, 0, 0, Role("floor"), -1},
      {3, 0, 0, Role("floor"), -1},
      {3, 0, 2, Role("wall"), -1},
  };
  voxelbench::reset_workspace(&physics_workspace);
  auto result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, 10, 4, 1, 1000);
  Check(result.status == voxelbench::SearchStatus::kUnsolved,
        "generalized search must collide raised polycube members with raised walls");

  result = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, 9, 4, 1, 1000);
  Check(result.status == voxelbench::SearchStatus::kSolved && result.moves == 1,
        "a raised polycube member may pass above a shorter wall");
}

void TestCappedSearchDoesNotClaimAnOptimalProof() {
  static voxelbench::PhysicsWorkspace physics_workspace;
  static voxelbench::SearchWorkspace search_workspace;
  voxelbench::Voxel voxels[45];
  int32_t count = 0;
  for (int32_t y = 0; y < 5; ++y) {
    for (int32_t x = 0; x < 5; ++x) {
      voxels[count++] = {x, y, 0, Role("floor"), -1};
    }
  }
  for (int32_t x = 0; x < 5; ++x) {
    voxels[count++] = {x, 0, 1, Role("wall"), -1};
    voxels[count++] = {x, 4, 1, Role("wall"), -1};
  }
  for (int32_t y = 1; y < 4; ++y) {
    voxels[count++] = {0, y, 1, Role("wall"), -1};
    voxels[count++] = {4, y, 1, Role("wall"), -1};
  }
  voxels[count++] = {1, 2, 1, Role("weightless-pushable"), 0};
  voxels[count++] = {2, 2, 1, Role("weightless-pushable"), 0};
  voxels[count++] = {1, 3, 1, Role("player"), -1};
  voxels[count++] = {1, 1, 1, Role("goal"), -1};

  voxelbench::reset_workspace(&physics_workspace);
  const auto screened = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, count, 5, 5, 5);
  Check(screened.status == voxelbench::SearchStatus::kSolvedUnproven &&
            screened.moves == 6,
        "a route found after state pruning must be marked solved-but-unproven");

  const auto proved = voxelbench::search_shortest(
      &search_workspace, &physics_workspace, voxels, count, 5, 5, 10000);
  Check(proved.status == voxelbench::SearchStatus::kSolved && proved.moves == 6,
        "the same route should become exact when the full frontier is retained");
}

void TestSlopeCarrierMovesStationaryRider() {
  voxelbench::Voxel voxels[] = {
      {0, 2, 1, Role("player"), -1},
      {1, 1, 2, Role("weightless-pushable"), 0},
      {1, 1, 3, Role("weightless-pushable"), 1},
      {1, 1, 1, Role("ice-slope-down"), -1},
      {1, 1, 0, Role("floor"), -1},
      {1, 0, 0, Role("wall"), -1},
      {0, 2, 0, Role("floor"), -1},
      {0, 1, 0, Role("floor"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 8, 3, 3, 0) == 0,
        "a command matching the slope's downhill direction should run");
  Check(voxels[1].y == 0 && voxels[1].z == 1,
        "the bottom body should descend the slope");
  Check(voxels[2].y == 0 && voxels[2].z == 2,
        "a stationary body resting on the slope mover should ride with it");
}

void TestSlopeAndFlatIceBridgeNeedsDeliberatePush() {
  voxelbench::Voxel voxels[] = {
      {0, 2, 1, Role("player"), -1},
      {1, 1, 2, Role("weightless-pushable"), 0},
      {2, 1, 2, Role("weightless-pushable"), 0},
      {2, 1, 1, Role("weightless-pushable"), 0},
      {1, 1, 1, Role("ice-slope-down"), -1},
      {2, 1, 0, Role("ice"), -1},
      {1, 1, 0, Role("floor"), -1},
      {0, 2, 0, Role("floor"), -1},
      {0, 1, 0, Role("floor"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 9, 3, 3, 0) == 0,
        "a command beside the mixed-support polycube should run");
  Check(voxels[1].y == 1 && voxels[2].y == 1 && voxels[3].y == 1,
        "a polycube bridging slope and flat Ice should remain stable without a push");
}

void TestOpposingSlopeLandingCancelsStoredMomentum() {
  voxelbench::Voxel voxels[] = {
      {1, 5, 3, Role("player"), -1},
      {1, 4, 3, Role("weightless-pushable"), 0},
      {1, 3, 3, Role("weightless-pushable"), 0},
      {1, 4, 2, Role("wall"), -1},
      {1, 5, 2, Role("wall"), -1},
      {1, 3, 0, Role("ice-slope-down"), -1},
      {1, 2, 0, Role("ice-slope-up"), -1},
      {1, 1, 1, Role("ice-slope-up"), -1},
  };
  Check(voxelbench::simulate_turn(voxels, 8, 3, 7, 0) == 0,
        "a push that drops a body onto opposing slopes should run");
  Check(voxels[1].y == 3 && voxels[1].z == 1 &&
            voxels[2].y == 2 && voxels[2].z == 1,
        "opposing slope supports should cancel stored pre-fall momentum");
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
  TestObserverReceivesNoMovementCompletionFrame();
  TestPreparedMotionStateClearsImmutableSuffix();
  TestPreparedSceneRejectsMovableStaticSuffix();
  TestPreparedIndexesDoNotLeakToDifferentSceneShape();
  TestInvalidQuiescentCallDoesNotLeakItsAssumption();
  TestPlayerGetsVisibleRowZeroVoidFrame();
  TestPushableGetsVisibleRowZeroVoidFrame();
  TestTallPolycubeDisappearsOnlyAfterItsTopPassesRowZero();
  TestPolycubeAbyssUsesLowestOtherWorldGeometry();
  TestPlayerAbyssUsesLowestOtherWorldGeometry();
  TestFallingRiderDoesNotExtendItsCarriersAbyss();
  TestObjectAboveDescendingPlayerFallsInSameTick();
  TestExactSearchFindsShortestCommands();
  TestFlatIceSearchKeepsExactCommandSemantics();
  TestGeneralSearchSupportsBoardsWiderThanSixteen();
  TestGeneralSearchCollapsesWalkingBeforePushes();
  TestGeneralSearchMatchesMazeBenchEngine3LongRoom();
  TestSearchInitializesFreshWorkspace();
  TestSearchPrunesPlayerGameOverBranches();
  TestPlayerCollectsGemOnlyAtCommandEnd();
  TestBoxMayOverlapGemWithoutCollectingIt();
  TestSlidingAcrossGemDoesNotCollectIt();
  TestPlayerSettlesBeforeHorizontalInput();
  TestPolycubeSettlesAsOneBodyBeforeHorizontalInput();
  TestSearchRequiresACommandToCollectStartingGem();
  TestPushCannotWalkPlayerOffWallSupport();
  TestSearchCollectsEveryGem();
  TestSearchStoresLargePolycubeAsOneEntity();
  TestGeneralSearchChecksRaisedPolycubeCollisions();
  TestCappedSearchDoesNotClaimAnOptimalProof();
  TestSlopeCarrierMovesStationaryRider();
  TestSlopeAndFlatIceBridgeNeedsDeliberatePush();
  TestOpposingSlopeLandingCancelsStoredMomentum();
  if (failures != 0) {
    std::cerr << failures << " C++ physics test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "all 41 C++ physics/search tests passed\n";
  return EXIT_SUCCESS;
}
