constexpr int32_t kMotionReady = 0;
constexpr int32_t kMotionPlayerIce = 1;
constexpr int32_t kMotionObjectIce = 2;
constexpr int32_t kMotionGravity = 3;
constexpr int32_t kMotionComplete = 4;
constexpr uint8_t kResumeNothing = 0;
constexpr uint8_t kResumePlayerIce = 1;
constexpr uint8_t kResumeObjectIce = 2;

void ClearMotionFlags(MotionState* state, int32_t count) {
  for (int32_t index = 0; index < count; ++index) {
    state->horizontal_momentum[index] = 0;
    state->falling[index] = 0;
    state->gravity_armed[index] = 0;
  }
}

uint8_t ReadObjectFlag(
    const uint8_t* flags,
    int32_t object) {
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    if (flags[member] != 0) return 1;
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
  for (int32_t index = 0; index < count; ++index) {
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
  for (int32_t index = 0; index < count; ++index) {
    state->horizontal_momentum[index] = 0;
  }
}

void StoreMandatoryMomentum(
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height) {
  ClearHorizontalMomentum(state, count);
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0 ||
        !ObjectIsActive(voxels, object, width, height)) {
      continue;
    }
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      state->horizontal_momentum[member] = 1;
    }
  }
}

bool StoreSupportedMandatoryMomentum(
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height) {
  ClearHorizontalMomentum(state, count);
  bool stored = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0 ||
        !ObjectIsActive(voxels, object, width, height) ||
        !ObjectIsFullyOnIce(voxels, object, width, height)) {
      continue;
    }
    stored = true;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      state->horizontal_momentum[member] = 1;
    }
  }
  return stored;
}

void RestoreMandatoryMomentum(const MotionState* state) {
  ClearMovingObjects();
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (ReadObjectFlag(state->horizontal_momentum, object) == 0) continue;
    g_mandatory_objects[object] = 1;
    g_moving_objects[object] = 1;
  }
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
  ClearMotionFlags(state, count);
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (ObjectHasDirectSupport(voxels, object, width, height)) {
      g_object_gravity_armed[object] = 1;
      continue;
    }
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const Voxel& voxel = voxels[member];
      if (FindNearestVoxelBelow(
              voxels, count, voxel.x, voxel.y, voxel.z, object, width) >= 0) {
        g_object_gravity_armed[object] = 1;
        break;
      }
    }
  }
  state->player_gravity_armed = FindNearestVoxelBelow(
                                    voxels,
                                    count,
                                    voxels[player_index].x,
                                    voxels[player_index].y,
                                    voxels[player_index].z,
                                    -1,
                                    width) >= 0;
  StoreObjectMotion(state, count);
}

template <bool kEntitiesFirst, int32_t kDirection = -1>
[[gnu::always_inline]] inline bool TrySimulateFlatSingleCubePushKnownEntities(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction,
    int32_t player_index,
    int32_t pushable_index) {
  constexpr int32_t kDx[] = {0, 1, 0, -1};
  constexpr int32_t kDy[] = {-1, 0, 1, 0};
  const int32_t actual_direction = kDirection >= 0 ? kDirection : direction;
  const int32_t dx = kDx[actual_direction];
  const int32_t dy = kDy[actual_direction];
  const Voxel& player = voxels[player_index];
  const Voxel& pushable = voxels[pushable_index];
  if (player.z == INT32_MIN || pushable.z != player.z ||
      pushable.x != player.x + dx || pushable.y != player.y + dy) {
    return false;
  }

  const int32_t maximum_step = dx > 0 ? width - 1 - pushable.x
      : dx < 0 ? pushable.x
      : dy > 0 ? height - 1 - pushable.y
      : pushable.y;
  // A 64-bit lane makes the common editor/search room allocation-free. Huge
  // rooms retain the fully general spatial-index implementation.
  if (maximum_step <= 0 || maximum_step > 63) return false;

  int32_t obstacle_step = INT32_MAX;
  uint64_t ice_supports = 0;
  uint64_t other_supports = 0;
  bool vacated_support_found = false;
  bool vacated_support_is_ice = false;
  bool lower_vacated_voxel = false;
  bool lower_player_voxel = false;
  constexpr int32_t kFirstVoxel = kEntitiesFirst ? 2 : 0;
  for (int32_t index = kFirstVoxel; index < count; ++index) {
    if constexpr (!kEntitiesFirst) {
      if (index == pushable_index) continue;
    }
    const Voxel& voxel = voxels[index];
    if constexpr (kEntitiesFirst) {
      if (voxel.role == kPlayerRole || voxel.role == kPushableRole ||
          voxel.role == kWeightlessPushableRole) {
        return false;
      }
    }
    if (voxel.x == pushable.x && voxel.y == pushable.y) {
      if (voxel.z == pushable.z - 1) {
        vacated_support_found = true;
        vacated_support_is_ice = voxel.role == kIceRole;
      } else if (voxel.z < pushable.z) {
        lower_vacated_voxel = true;
      }
    }
    if (voxel.x == player.x && voxel.y == player.y &&
        voxel.z < player.z) {
      lower_player_voxel = true;
    }

    const bool in_lane = dx != 0
        ? voxel.y == pushable.y
        : voxel.x == pushable.x;
    if (!in_lane) continue;
    const int32_t step = dx != 0
        ? (voxel.x - pushable.x) * dx
        : (voxel.y - pushable.y) * dy;
    if (step <= 0 || step > maximum_step) continue;
    if (voxel.z == pushable.z) {
      if (step < obstacle_step) obstacle_step = step;
    } else if (voxel.z == pushable.z - 1) {
      const uint64_t bit = uint64_t{1} << (step - 1);
      if (voxel.role == kIceRole) {
        ice_supports |= bit;
      } else {
        other_supports |= bit;
      }
    }
  }

  const int32_t last_open_step = obstacle_step == INT32_MAX
      ? maximum_step
      : obstacle_step - 1;
  if (last_open_step <= 0) return false;
  const uint64_t open_lane =
      (uint64_t{1} << last_open_step) - uint64_t{1};
  const uint64_t first_other_candidates = other_supports & open_lane;
  int32_t final_step = last_open_step;
  if (first_other_candidates != 0) {
    final_step = static_cast<int32_t>(
        __builtin_ctzll(first_other_candidates)) + 1;
    const uint64_t prior_lane = final_step == 1
        ? uint64_t{0}
        : (uint64_t{1} << (final_step - 1)) - uint64_t{1};
    if ((ice_supports & prior_lane) != prior_lane) return false;
  } else if ((ice_supports & open_lane) != open_lane) {
    // A missing support begins a fall, whose timing belongs to step_tick.
    return false;
  }

  if (vacated_support_found) {
    // A player entering an Ice-supported vacated cell gains its own momentum
    // and must remain on the synchronized tick path.
    if (vacated_support_is_ice) return false;
  } else if (lower_vacated_voxel || lower_player_voxel) {
    // Falling and ledge timing stays in the general discrete solver.
    return false;
  }

  voxels[player_index].x = pushable.x;
  voxels[player_index].y = pushable.y;
  voxels[pushable_index].x += dx * final_step;
  voxels[pushable_index].y += dy * final_step;
  return true;
}

[[gnu::always_inline]] inline bool TrySimulateFlatSingleCubePush(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  if (voxels == nullptr || count <= 0 || width <= 0 || height <= 0 ||
      direction < 0 || direction > 3) {
    return false;
  }

  // Search states normally keep their two dynamic entities first. Compiling
  // that layout separately removes entity discovery and dead branches from the
  // hot A*/evolutionary-search transition. Editor-sorted arrays use the fully
  // order-independent path below.
  if (count >= 2 && voxels[0].role == kPlayerRole &&
      voxels[1].role == kPushableRole) {
    switch (direction) {
      case 0:
        return TrySimulateFlatSingleCubePushKnownEntities<true, 0>(
            voxels, count, width, height, direction, 0, 1);
      case 1:
        return TrySimulateFlatSingleCubePushKnownEntities<true, 1>(
            voxels, count, width, height, direction, 0, 1);
      case 2:
        return TrySimulateFlatSingleCubePushKnownEntities<true, 2>(
            voxels, count, width, height, direction, 0, 1);
      default:
        return TrySimulateFlatSingleCubePushKnownEntities<true, 3>(
            voxels, count, width, height, direction, 0, 1);
    }
  }

  int32_t player_index = -1;
  int32_t pushable_index = -1;
  for (int32_t index = 0; index < count; ++index) {
    if (voxels[index].role == kPlayerRole) {
      if (player_index >= 0) return false;
      player_index = index;
    } else if (voxels[index].role == kPushableRole) {
      if (pushable_index >= 0) return false;
      pushable_index = index;
    } else if (voxels[index].role == kWeightlessPushableRole) {
      return false;
    }
  }
  if (player_index < 0 || pushable_index < 0) return false;
  return TrySimulateFlatSingleCubePushKnownEntities<false>(
      voxels,
      count,
      width,
      height,
      direction,
      player_index,
      pushable_index);
}
