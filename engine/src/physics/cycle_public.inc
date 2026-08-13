TickResult step_command_tick(
    PhysicsWorkspace* workspace,
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  if (workspace == nullptr || state == nullptr || voxels == nullptr) {
    return TickResult::kInvalid;
  }
  CommandCycleTracker& tracker = g_command_cycle_tracker;
  if (tracker.pending_rollback && tracker.source_state == state &&
      tracker.source_voxels == voxels && tracker.count == count) {
    RestoreCycleBaseline(workspace, tracker, state, voxels);
    return TickResult::kComplete;
  }
  if (state->direction < 0) {
    BeginCommandCycleTracking(
        workspace, state, voxels, count, width, height, direction);
  }

  const int32_t before_tick = state->tick;
  const TickResult result = step_tick(
      workspace, state, voxels, count, width, height, direction);
  if (!tracker.enabled || tracker.source_state != state ||
      tracker.source_voxels != voxels) {
    return result;
  }
  if (state->tick != before_tick) {
    const int32_t cycle_start = FindOrRecordCycleState(tracker, voxels, state);
    if (cycle_start >= 0) {
      state->cycle_start_tick = cycle_start;
      state->cycle_repeat_tick = state->tick;
      tracker.pending_rollback = true;
      return TickResult::kMore;
    }
  }
  if (result == TickResult::kComplete || result == TickResult::kInvalid ||
      result == TickResult::kNoPlayer) {
    tracker.enabled = false;
  }
  return result;
}
