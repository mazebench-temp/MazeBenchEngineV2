int32_t simulate_command(
    PhysicsWorkspace* workspace,
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction,
    TickObserver observer,
    void* context) {
  if (state == nullptr) return -1;
  reset_motion_state(state);
  constexpr int32_t kMaximumTicks = 1000000;
  for (int32_t iteration = 0; iteration < kMaximumTicks; ++iteration) {
    const int32_t before_tick = state->tick;
    const TickResult result = step_tick(
        workspace, state, voxels, count, width, height, direction);
    if (result == TickResult::kInvalid) return -1;
    if (result == TickResult::kNoPlayer) return -2;
    if (state->tick != before_tick && observer != nullptr) {
      observer(voxels, count, state, context);
    }
    if (result == TickResult::kComplete) return 0;
  }
  return -1;
}

int32_t simulate_turn(
    PhysicsWorkspace* workspace,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  if (workspace != nullptr && TrySimulateFlatSingleCubePush(
          voxels, count, width, height, direction)) {
    return 0;
  }
  MotionState state;
  return simulate_command(
      workspace, &state, voxels, count, width, height, direction);
}

int32_t simulate_turn(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  if (TrySimulateFlatSingleCubePush(
          voxels, count, width, height, direction)) {
    return 0;
  }
  static thread_local PhysicsWorkspace workspace;
  static thread_local bool initialized = false;
  if (!initialized) {
    reset_workspace(&workspace);
    initialized = true;
  }
  return simulate_turn(&workspace, voxels, count, width, height, direction);
}
