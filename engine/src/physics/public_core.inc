void reset_workspace(PhysicsWorkspace* workspace) {
  if (workspace == nullptr) return;
  WorkspaceData* data = WorkspaceStorage(workspace);
  data->hash_generation = 1;
  data->object_count = 0;
  data->weightless_object_count = 0;
  data->dense_columns = false;
  data->linear_spatial = false;
  data->spatial_voxel_count = 0;
  data->moving_objects_fell = false;
  data->ignored_support_voxel = -1;
  data->continuation_state = nullptr;
  data->continuation_voxels = nullptr;
  data->continuation_tick = -1;
  for (int32_t index = 0; index < kHashCapacity; ++index) {
    data->hash_stamps[index] = 0;
  }
}

uint32_t hash_role(const uint8_t* bytes, int32_t length) {
  if (bytes == nullptr || length < 0) return 0;
  uint32_t hash = 2166136261u;
  for (int32_t index = 0; index < length; ++index) {
    hash = (hash ^ bytes[index]) * 16777619u;
  }
  return hash;
}

void reset_motion_state(MotionState* state) {
  if (state == nullptr) return;
  state->version = kMotionStateVersion;
  state->phase = kMotionReady;
  state->direction = -1;
  state->tick = 0;
  state->voxel_count = 0;
  state->player_index = -1;
  state->player_gravity_armed = 0;
  state->player_falling = 0;
  state->reserved[0] = 0;
  state->reserved[1] = 0;
}
