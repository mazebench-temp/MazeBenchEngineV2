bool PlayerGateBlockingActor(uint32_t role) {
  return role == kPushableRole || IsWeightlessObjectRole(role) ||
      IsCloneObjectRole(role);
}

// Red gates use MazeBench's proximity rule. A lowered gate rises when a player
// finishes a tick beside it, except while a same-height movable body occupies
// the gate cell. Standing directly on the lowered plate keeps it lowered.
bool RefreshPlayerGates(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height) {
  if (!ActiveWorkspace().scene_has_player_gates) return false;
  bool changed = false;
  for (int32_t gate_index = 0; gate_index < count; ++gate_index) {
    Voxel& gate = voxels[gate_index];
    if (gate.role != kPlayerGateRole ||
        !IsInsideRoom(gate.x, gate.y, width, height)) {
      continue;
    }
    bool same_level_block = false;
    for (int32_t index = 0; index < count; ++index) {
      const Voxel& candidate = voxels[index];
      if (!PlayerGateBlockingActor(candidate.role) ||
          !IsInsideRoom(candidate.x, candidate.y, width, height)) {
        continue;
      }
      same_level_block |= candidate.x == gate.x && candidate.y == gate.y &&
          candidate.z == gate.z;
    }
    bool raised = false;
    for (int32_t index = 0; index < count && !raised; ++index) {
      const Voxel& player = voxels[index];
      if (player.role != kPlayerRole ||
          !IsInsideRoom(player.x, player.y, width, height) ||
          player.z == INT32_MIN) {
        continue;
      }
      const int64_t delta_x = static_cast<int64_t>(player.x) - gate.x;
      const int64_t delta_y = static_cast<int64_t>(player.y) - gate.y;
      const int64_t distance =
          (delta_x < 0 ? -delta_x : delta_x) +
          (delta_y < 0 ? -delta_y : delta_y);
      const bool standing_on_gate = distance == 0 && player.z == gate.z;
      raised = distance <= 1 && !standing_on_gate &&
          (player.z != gate.z || !same_level_block);
    }
    const int32_t next_state = raised ? 1 : 0;
    if (gate.generic_id != next_state) {
      gate.generic_id = next_state;
      changed = true;
    }
  }
  return changed;
}
