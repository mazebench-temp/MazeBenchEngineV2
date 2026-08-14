constexpr int32_t kMotionReady = 0;
constexpr int32_t kMotionPlayerIce = 1;
constexpr int32_t kMotionObjectIce = 2;
constexpr int32_t kMotionGravity = 3;
constexpr int32_t kMotionComplete = 4;
constexpr uint8_t kResumeNothing = 0;
constexpr uint8_t kResumePlayerIce = 1;
constexpr uint8_t kResumeObjectIce = 2;
constexpr uint8_t kResumeCommand = 3;

uint8_t ReadObjectFlag(
    const uint8_t* flags,
    int32_t object) {
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    if (flags[member] != 0) return 1;
  }
  return 0;
}

uint8_t ReadObjectMomentum(
    const uint8_t* flags,
    int32_t object) {
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    if (flags[member] != 0) return flags[member];
  }
  return 0;
}

void LoadObjectMotion(const MotionState* state) {
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_object_falling[object] = ReadObjectFlag(state->falling, object);
    g_object_gravity_armed[object] =
        ReadObjectFlag(state->gravity_armed, object);
  }
}

void StoreObjectMotion(MotionState* state, int32_t count) {
  const int32_t motion_count = MotionVoxelCount(count);
  for (int32_t index = 0; index < motion_count; ++index) {
    state->falling[index] = 0;
    state->gravity_armed[index] = 0;
  }
  for (int32_t object = 0; object < g_object_count; ++object) {
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      state->falling[member] = g_object_falling[object];
      state->gravity_armed[member] = g_object_gravity_armed[object];
    }
  }
}

void ClearHorizontalMomentum(MotionState* state, int32_t count) {
  const int32_t motion_count = MotionVoxelCount(count);
  for (int32_t index = 0; index < motion_count; ++index) {
    state->horizontal_momentum[index] = 0;
  }
}

void ClearImmutableMotionSuffix(MotionState* state, int32_t count) {
  // Prepared scenes keep immutable terrain after the dynamic prefix. Physics
  // never reads motion for that suffix, but MotionState is public and
  // serializable, so initialize the exposed bytes once per command.
  const int32_t motion_count = MotionVoxelCount(count);
  for (int32_t index = motion_count; index < count; ++index) {
    state->horizontal_momentum[index] = 0;
    state->falling[index] = 0;
    state->gravity_armed[index] = 0;
  }
}

void StoreMandatoryMomentum(
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t fallback_direction) {
  ClearHorizontalMomentum(state, count);
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0 ||
        !ObjectIsActive(voxels, object, width, height)) {
      continue;
    }
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const int32_t direction = g_object_direction[object] <= 3
          ? g_object_direction[object]
          : fallback_direction;
      state->horizontal_momentum[member] =
          static_cast<uint8_t>(direction + 1);
    }
  }
}

bool StoreSupportedMandatoryMomentum(
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t fallback_direction) {
  // Snapshot the incoming serialized momentum before replacing it. A body
  // can land on a lower slope during the gravity half of this very tick; it
  // is no longer marked falling or mandatory by then, but its saved direction
  // must resume on the newly reached ramp.
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_object_redirected[object] = ReadObjectMomentum(
        state->horizontal_momentum, object);
  }
  ClearHorizontalMomentum(state, count);
  bool stored = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (!ObjectIsActive(voxels, object, width, height)) {
      continue;
    }
    const bool supported_momentum =
        (g_inherited_support_motion[object] != 0 ||
         g_mandatory_objects[object] != 0 ||
         g_object_redirected[object] != 0) &&
        (g_inherited_support_motion[object] != 0 ||
         ObjectIsFullyOnIce(voxels, object, width, height));
    bool supported_by_momentum_body = false;
    if (!supported_momentum && g_object_redirected[object] != 0) {
      for (int32_t member = g_object_head[object]; member >= 0;
           member = g_next_object_voxel[member]) {
        const Voxel& voxel = voxels[member];
        const int32_t support = FindVoxelAt(
            voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
        if (support < 0) continue;
        const int32_t support_object = g_voxel_object[support];
        if (support_object >= 0 && support_object != object &&
            g_object_redirected[support_object] != 0) {
          supported_by_momentum_body = true;
          break;
        }
      }
    }
    bool falling_toward_slope = false;
    if (!supported_momentum && g_object_falling[object] != 0) {
      for (int32_t member = g_object_head[object]; member >= 0;
           member = g_next_object_voxel[member]) {
        if (VoxelWillLandOnSlope(
                voxels, count, voxels[member], object, width)) {
          falling_toward_slope = true;
          break;
        }
      }
    }
    if (!supported_momentum && !falling_toward_slope &&
        !supported_by_momentum_body) {
      continue;
    }
    stored |= supported_momentum;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const int32_t direction = g_object_direction[object] <= 3
          ? g_object_direction[object]
          : fallback_direction;
      state->horizontal_momentum[member] = static_cast<uint8_t>(
          direction + (supported_momentum ? 1 : 5));
    }
  }
  return stored;
}

void RestoreMandatoryMomentum(
    const MotionState* state,
    Voxel* voxels,
    int32_t width,
    int32_t height) {
  ClearMovingObjects();
  for (int32_t object = 0; object < g_object_count; ++object) {
    const uint8_t momentum = ReadObjectMomentum(
        state->horizontal_momentum, object);
    if (momentum == 0) continue;
    g_mandatory_objects[object] = 1;
    g_moving_objects[object] = 1;
    g_object_direction[object] = static_cast<uint8_t>((momentum - 1) & 3);
    if ((momentum > 4 &&
         !ObjectIsFullyOnIce(voxels, object, width, height)) ||
        g_object_falling[object] != 0) {
      // Momentum remains serialized while a body falls toward a lower slope,
      // but it is not a horizontal proposal until the body lands.
      g_mandatory_objects[object] = 0;
      g_moving_objects[object] = 0;
    }
  }
}

void CancelConflictingLandedSlopeMomentum(
    MotionState* state,
    Voxel* voxels,
    int32_t width,
    int32_t height) {
  // A rigid body landing across slopes with different downhill directions is
  // stable. Its pre-fall direction cannot choose one supporting wedge over
  // another. A later deliberate push may still move the body transactionally.
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0 ||
        !ObjectHasConflictingSlopeSupports(
            voxels, object, width, height)) {
      continue;
    }
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      state->horizontal_momentum[member] = 0;
    }
    g_mandatory_objects[object] = 0;
    g_moving_objects[object] = 0;
  }
}

bool StoredMomentumWillLandOnSlope(
    const MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width) {
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (ReadObjectMomentum(state->horizontal_momentum, object) == 0 ||
        g_object_falling[object] == 0) {
      continue;
    }
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      if (VoxelWillLandOnSlope(
              voxels, count, voxels[member], object, width)) {
        return true;
      }
    }
  }
  return false;
}

struct RiderSnapshot {
  int32_t object = -1;
  int32_t member = -1;
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
};

RiderSnapshot CapturePlayerRider(
    Voxel* voxels,
    int32_t player_index,
    int32_t width,
    int32_t height) {
  RiderSnapshot rider;
  const Voxel& player = voxels[player_index];
  if (player.z == INT32_MIN) return rider;
  const int32_t support = FindVoxelAt(
      voxels, player.x, player.y, player.z - 1, width, height);
  if (support < 0 || g_voxel_object[support] < 0) return rider;
  rider.object = g_voxel_object[support];
  rider.member = g_object_head[rider.object];
  rider.x = voxels[rider.member].x;
  rider.y = voxels[rider.member].y;
  rider.z = voxels[rider.member].z;
  return rider;
}

bool MovePlayerWithRider(
    Voxel* voxels,
    int32_t player_index,
    const RiderSnapshot& rider,
    int32_t width,
    int32_t height) {
  if (rider.object < 0 || g_moving_objects[rider.object] == 0 ||
      !ObjectIsActive(voxels, rider.object, width, height)) {
    return false;
  }
  voxels[player_index].x += voxels[rider.member].x - rider.x;
  voxels[player_index].y += voxels[rider.member].y - rider.y;
  voxels[player_index].z += voxels[rider.member].z - rider.z;
  return true;
}

void InitializeMotion(
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t player_index,
    int32_t direction) {
  state->version = kMotionStateVersion;
  state->phase = kMotionReady;
  state->direction = direction;
  state->tick = 0;
  state->voxel_count = count;
  state->player_index = player_index;
  state->player_gravity_armed = 0;
  state->player_falling = 0;
  state->reserved[0] = kResumeNothing;
  state->reserved[1] = 0;
  // StoreObjectMotion below replaces the falling/gravity arrays completely;
  // only horizontal momentum needs an independent reset here.
  ClearHorizontalMomentum(state, count);
  bool needs_initial_gravity = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (ObjectHasDirectSupport(voxels, object, width, height)) {
      g_object_gravity_armed[object] = 1;
      continue;
    }
    if (ObjectIsActive(voxels, object, width, height)) {
      // Dynamic bodies are never allowed to begin a command suspended in the
      // editor volume. Arm them even over the abyss so they settle or leave
      // the world before the requested horizontal input is evaluated.
      g_object_gravity_armed[object] = 1;
      g_object_falling[object] = 1;
      needs_initial_gravity = true;
    }
  }
  const Voxel& player = voxels[player_index];
  const bool player_active =
      IsInsideRoom(player.x, player.y, width, height) &&
      player.z != INT32_MIN;
  const bool player_supported = player_active && FindVoxelAt(
      voxels, player.x, player.y, player.z - 1, width, height) >= 0;
  state->player_gravity_armed = player_active ? 1 : 0;
  if (player_active && !player_supported) {
    state->player_falling = 1;
    needs_initial_gravity = true;
  }
  if (needs_initial_gravity) {
    state->phase = kMotionGravity;
    state->reserved[0] = kResumeCommand;
  }
  StoreObjectMotion(state, count);
  ClearImmutableMotionSuffix(state, count);
}

void InitializeQuiescentMotion(
    MotionState* state,
    int32_t count,
    int32_t player_index,
    int32_t direction) {
  state->version = kMotionStateVersion;
  state->phase = kMotionReady;
  state->direction = direction;
  state->tick = 0;
  state->voxel_count = count;
  state->player_index = player_index;
  state->player_gravity_armed = 1;
  state->player_falling = 0;
  state->reserved[0] = kResumeNothing;
  state->reserved[1] = 0;
  // StoreObjectMotion below replaces the falling/gravity arrays completely.
  ClearHorizontalMomentum(state, count);
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_object_gravity_armed[object] = 1;
    g_object_falling[object] = 0;
  }
  StoreObjectMotion(state, count);
  // This state is private to simulate_quiescent_turn and is never serialized;
  // unlike the public step_tick path, its immutable suffix need not be stored.
}

void CollectGoalsAtPlayer(
    Voxel* voxels,
    int32_t count,
    int32_t player_index,
    int32_t width,
    int32_t height) {
  if (player_index < 0 || player_index >= count) return;
  const Voxel& player = voxels[player_index];
  if (IsPreparedScene(voxels, count, width, height)) {
    const WorkspaceData& workspace = ActiveWorkspace();
    for (int32_t goal = 0; goal < workspace.prepared_goal_count; ++goal) {
      Voxel& voxel = voxels[workspace.prepared_goal_indices[goal]];
      if (voxel.x == player.x && voxel.y == player.y && voxel.z == player.z) {
        voxel.x = -1;
      }
    }
    return;
  }
  for (int32_t index = 0; index < count; ++index) {
    if (voxels[index].role == kGoalRole && voxels[index].x == player.x &&
        voxels[index].y == player.y && voxels[index].z == player.z) {
      voxels[index].x = -1;
    }
  }
}

// Exact accelerator for a quiescent command whose only possible moving body is
// the player. It deliberately declines pushes and carried-body interactions;
// those continue through the same full command kernel. Keeping this decision
// in physics (rather than in search) gives every caller identical semantics for
// walking, elevated supports, Ice, gravity, abyss removal, and collectibles.
// Returns 1 when handled, 0 when the full kernel is required, and a negative
// public simulation error for invalid input/no player.
int32_t TrySimulatePassiveQuiescentTurn(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction,
    bool rebuild_spatial_index) {
  if (!IsPreparedScene(voxels, count, width, height)) return 0;
  // Directional slopes can change both the motion vector and elevation. They
  // stay on the generalized command kernel, including exact cycle handling.
  if (ActiveWorkspace().prepared_has_ice_slopes) return 0;
  const int32_t player_index = ActiveWorkspace().prepared_player_index;
  if (player_index < 0 || player_index >= count ||
      voxels[player_index].role != kPlayerRole) {
    return -2;
  }
  Voxel& player = voxels[player_index];
  if (!IsInsideRoom(player.x, player.y, width, height) ||
      player.z == INT32_MIN) {
    return -2;
  }

  if (rebuild_spatial_index) {
    ResetObjectMotion();
    BuildSpatialIndex(voxels, count, width, height);
  } else if (!ActiveWorkspace().prepared_index_active) {
    return 0;
  }
  g_ignored_support_voxel = player_index;
  constexpr int32_t kDx[4] = {0, 1, 0, -1};
  constexpr int32_t kDy[4] = {-1, 0, 1, 0};
  const int32_t dx = kDx[direction];
  const int32_t dy = kDy[direction];
  int32_t x = player.x;
  int32_t y = player.y;
  int32_t z = player.z;
  bool automatic_ice_motion = false;

  auto support_at = [&](int32_t support_x, int32_t support_y, int32_t support_z) {
    const int32_t support = FindVoxelAt(
        voxels, support_x, support_y, support_z, width, height);
    return support == player_index ? -1 : support;
  };
  auto has_carried_body = [&]() {
    if (z == INT32_MAX) return false;
    const int32_t above = FindVoxelAt(voxels, x, y, z + 1, width, height);
    return above >= 0 && g_voxel_object[above] >= 0;
  };
  auto fall_until_settled = [&]() {
    for (;;) {
      if (z == INT32_MIN) {
        x = -1;
        return;
      }
      if (support_at(x, y, z - 1) >= 0) return;
      const int32_t lower_support = FindNearestVoxelBelow(
          voxels, count, x, y, z, -1, width);
      const int32_t previous_z = z;
      --z;
      if (lower_support < 0 && previous_z < LowestOtherActiveZ(
              voxels, count, width, height, -1, player_index, false, true)) {
        x = -1;
        return;
      }
    }
  };

  for (;;) {
    if (has_carried_body()) {
      g_ignored_support_voxel = -1;
      return 0;
    }
    const bool started_on_ice = [&]() {
      const int32_t support = support_at(x, y, z - 1);
      return support >= 0 && IsIceRole(voxels[support].role);
    }();
    const int32_t target_x = x + dx;
    const int32_t target_y = y + dy;
    if (!IsInsideRoom(target_x, target_y, width, height)) break;

    const int32_t target = FindVoxelAt(
        voxels, target_x, target_y, z, width, height);
    if (target >= 0 && target != player_index) {
      const int32_t target_object = g_voxel_object[target];
      if (!automatic_ice_motion && target_object >= 0) {
        // Reject only collisions that no carrier/chain rule can rescue. This
        // avoids entering the full multi-body kernel for pushes directly into
        // room boundaries or immutable terrain while leaving every ambiguous
        // dynamic interaction to that kernel.
        bool definitely_blocked = false;
        for (int32_t member = g_object_head[target_object]; member >= 0;
             member = g_next_object_voxel[member]) {
          const int32_t member_x = voxels[member].x + dx;
          const int32_t member_y = voxels[member].y + dy;
          if (!IsInsideRoom(member_x, member_y, width, height)) {
            definitely_blocked = true;
            break;
          }
          const int32_t occupant = FindVoxelAt(
              voxels,
              member_x,
              member_y,
              voxels[member].z,
              width,
              height);
          if (occupant >= 0 && occupant != player_index &&
              g_voxel_object[occupant] < 0) {
            definitely_blocked = true;
            break;
          }
        }
        if (!definitely_blocked) {
          g_ignored_support_voxel = -1;
          return 0;
        }
        break;
      }
      break;
    }

    const int32_t direct_support = support_at(target_x, target_y, z - 1);
    if (direct_support < 0) {
      const int32_t lower_support = FindNearestVoxelBelow(
          voxels, count, target_x, target_y, z, -1, width);
      const int32_t current_support = support_at(x, y, z - 1);
      const bool may_leave_support = automatic_ice_motion || started_on_ice ||
          (current_support >= 0 &&
           voxels[current_support].role == kFloorRole && lower_support < 0);
      if (!may_leave_support) break;
    }

    x = target_x;
    y = target_y;
    if (direct_support < 0) {
      fall_until_settled();
      break;
    }

    const bool now_on_ice = IsIceRole(voxels[direct_support].role);
    if (now_on_ice) {
      automatic_ice_motion = true;
      continue;
    }
    if (automatic_ice_motion) break;

    // A normal first step gets Ice momentum when the next unobstructed cell is
    // supported by Ice, matching the full kernel's one-cell run-up rule.
    const int32_t run_up_x = x + dx;
    const int32_t run_up_y = y + dy;
    if (IsInsideRoom(run_up_x, run_up_y, width, height)) {
      const int32_t run_up_target = FindVoxelAt(
          voxels, run_up_x, run_up_y, z, width, height);
      const int32_t run_up_support = support_at(run_up_x, run_up_y, z - 1);
      if ((run_up_target < 0 || run_up_target == player_index) &&
          run_up_support >= 0 &&
          IsIceRole(voxels[run_up_support].role)) {
        automatic_ice_motion = true;
        continue;
      }
    }
    break;
  }

  player.x = x;
  player.y = y;
  player.z = z;
  CollectGoalsAtPlayer(voxels, count, player_index, width, height);
  g_ignored_support_voxel = -1;
  return 1;
}
