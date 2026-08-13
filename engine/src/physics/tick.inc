TickResult step_tick(
    PhysicsWorkspace* workspace,
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  if (workspace == nullptr || state == nullptr) return TickResult::kInvalid;
  WorkspaceScope workspace_scope(workspace);
  const int64_t cell_count = static_cast<int64_t>(width) * height;
  if (voxels == nullptr || count < 0 || count > kVoxelCapacity || width <= 0 ||
      height <= 0 || cell_count > UINT32_MAX || direction < 0 || direction > 3 ||
      state->version != kMotionStateVersion) {
    return TickResult::kInvalid;
  }
  if (state->phase == kMotionComplete) return TickResult::kComplete;

  bool workspace_has_continuation =
      ActiveWorkspace().continuation_state == state &&
      ActiveWorkspace().continuation_voxels == voxels &&
      ActiveWorkspace().continuation_tick == state->tick;
  if (state->direction < 0) {
    const bool prepared = IsPreparedScene(voxels, count, width, height);
    int32_t player_index = prepared
        ? ActiveWorkspace().prepared_player_index
        : -1;
    if (!prepared) {
      for (int32_t index = 0; index < count; ++index) {
        if (voxels[index].role == kPlayerRole) {
          player_index = index;
          break;
        }
      }
    }
    if (player_index < 0) return TickResult::kNoPlayer;
    if (IsPreparedScene(voxels, count, width, height)) {
      ResetObjectMotion();
    } else {
      BuildObjects(voxels, count);
    }
    BuildSpatialIndex(voxels, count, width, height);
    if (prepared && ActiveWorkspace().assume_quiescent_start) {
      ActiveWorkspace().assume_quiescent_start = false;
      InitializeQuiescentMotion(state, count, player_index, direction);
    } else {
      ActiveWorkspace().assume_quiescent_start = false;
      InitializeMotion(
          state, voxels, count, width, height, player_index, direction);
    }
    workspace_has_continuation = true;
  } else if (state->voxel_count != count || state->player_index < 0 ||
             state->player_index >= count ||
             voxels[state->player_index].role != kPlayerRole) {
    return TickResult::kInvalid;
  }

  const int32_t player_index = state->player_index;
  constexpr int32_t kDx[] = {0, 1, 0, -1};
  constexpr int32_t kDy[] = {-1, 0, 1, 0};
  const int32_t dx = kDx[state->direction];
  const int32_t dy = kDy[state->direction];

  if (!workspace_has_continuation) {
    BuildObjects(voxels, count);
    BuildSpatialIndex(voxels, count, width, height);
    LoadObjectMotion(state);
  }

  auto store_and_finish = [&](int32_t phase,
                              bool advanced,
                              bool gravity_already_checked = false) {
    if (phase == kMotionComplete && !gravity_already_checked &&
        ObjectsNeedGravityTick(voxels, count, width, height)) {
      // A blocked/boundary-ending Ice proposal can happen immediately after a
      // carried body lost support. Completion must still drain that gravity;
      // otherwise the body remains suspended until the next player command.
      phase = kMotionGravity;
      state->reserved[0] = kResumeNothing;
    }
    state->phase = phase;
    if (advanced) ++state->tick;
    if (phase == kMotionComplete) {
      CollectGoalsAtPlayer(
          voxels, count, state->player_index, width, height);
    }
    StoreObjectMotion(state, count);
    if (phase == kMotionComplete) {
      ActiveWorkspace().continuation_state = nullptr;
      ActiveWorkspace().continuation_voxels = nullptr;
      ActiveWorkspace().continuation_tick = -1;
      ClearHorizontalMomentum(state, count);
      return TickResult::kComplete;
    }
    ActiveWorkspace().continuation_state = state;
    ActiveWorkspace().continuation_voxels = voxels;
    ActiveWorkspace().continuation_tick = state->tick;
    return TickResult::kMore;
  };

  auto resolve_player_gravity = [&](bool automatic_ice_motion,
                                    bool started_on_ice,
                                    bool pushed) {
    Voxel& player = voxels[player_index];
    if (!IsInsideRoom(player.x, player.y, width, height)) {
      state->player_falling = 0;
      return false;
    }
    if (player.z != INT32_MIN &&
        FindVoxelAt(voxels, player.x, player.y, player.z - 1, width, height) >= 0) {
      state->player_gravity_armed = 1;
      state->player_falling = 0;
      return false;
    }
    const int32_t lower_support = FindNearestVoxelBelow(
        voxels, count, player.x, player.y, player.z, -1, width);
    if (lower_support < 0) {
      if (!state->player_gravity_armed) return false;
    } else if (!automatic_ice_motion && !started_on_ice && !pushed &&
               !state->player_falling) {
      return false;
    }

    const int32_t previous_z = player.z;
    state->player_gravity_armed = 1;
    state->player_falling = 1;
    if (player.z == INT32_MIN) {
      player.x = -1;
      state->player_falling = 0;
    } else {
      --player.z;
      if (lower_support < 0 && previous_z < LowestOtherActiveZ(
              voxels, count, width, height, -1, player_index, false, true)) {
        // Keep the player visible until its top has passed below every other
        // active voxel. On the following unsupported step, advance once more
        // and remove it from the room.
        player.x = -1;
        state->player_falling = 0;
      }
    }
    BuildSpatialIndex(voxels, count, width, height);
    if (IsInsideRoom(player.x, player.y, width, height) &&
        player.z != INT32_MIN &&
        FindVoxelAt(voxels, player.x, player.y, player.z - 1, width, height) >= 0) {
      state->player_falling = 0;
    }
    return true;
  };

  auto defer_player_gravity = [&](bool automatic_ice_motion,
                                  bool started_on_ice,
                                  bool pushed) {
    const Voxel& player = voxels[player_index];
    if (!IsInsideRoom(player.x, player.y, width, height)) {
      state->player_falling = 0;
      return false;
    }
    if (player.z != INT32_MIN &&
        FindVoxelAt(
            voxels, player.x, player.y, player.z - 1, width, height) >= 0) {
      state->player_gravity_armed = 1;
      state->player_falling = 0;
      return false;
    }
    const int32_t lower_support = FindNearestVoxelBelow(
        voxels, count, player.x, player.y, player.z, -1, width);
    if (lower_support < 0) {
      if (!state->player_gravity_armed) return false;
    } else if (!automatic_ice_motion && !started_on_ice && !pushed &&
               state->player_falling == 0) {
      return false;
    }
    state->player_gravity_armed = 1;
    state->player_falling = 1;
    return true;
  };

  if (state->phase == kMotionGravity) {
    const bool player_has_horizontal_ice_proposal =
        state->reserved[0] == kResumePlayerIce &&
        IsSupportedByIce(voxels, voxels[player_index], width, height);
    int32_t object_supported_by_player = -1;
    const Voxel& player_before_gravity = voxels[player_index];
    if (player_before_gravity.z != INT32_MAX) {
      const int32_t above_player = FindVoxelAt(
          voxels,
          player_before_gravity.x,
          player_before_gravity.y,
          player_before_gravity.z + 1,
          width,
          height);
      if (above_player >= 0) {
        object_supported_by_player = g_voxel_object[above_player];
      }
    }
    const RiderSnapshot rider =
        CapturePlayerRider(voxels, player_index, width, height);
    if (rider.object >= 0 ||
        (object_supported_by_player >= 0 && state->player_falling != 0)) {
      g_ignored_support_voxel = player_index;
    }
    SettleAllObjects(voxels, count, width, height);
    g_ignored_support_voxel = -1;
    const bool objects_advanced = ObjectsAdvancedThisTick();
    const bool rode_falling_object =
        rider.object >= 0 && g_fell_objects[rider.object] != 0;
    bool player_rode_falling_object = false;
    if (rode_falling_object) {
      player_rode_falling_object = MovePlayerWithRider(
          voxels, player_index, rider, width, height);
      BuildSpatialIndex(voxels, count, width, height);
    }
    bool player_slid = false;
    if (!player_rode_falling_object && player_has_horizontal_ice_proposal) {
      Voxel& sliding_player = voxels[player_index];
      const int32_t next_x = sliding_player.x + dx;
      const int32_t next_y = sliding_player.y + dy;
      if (IsInsideRoom(next_x, next_y, width, height) &&
          FindVoxelAt(
              voxels,
              next_x,
              next_y,
              sliding_player.z,
              width,
              height) < 0) {
        TranslateObjectsCarriedByPlayerOneStep(
            voxels, player_index, dx, dy, width, height);
        sliding_player.x = next_x;
        sliding_player.y = next_y;
        player_slid = true;
        BuildSpatialIndex(voxels, count, width, height);
        defer_player_gravity(true, true, false);
        if (!IsSupportedByIce(
                voxels, sliding_player, width, height)) {
          state->reserved[0] = kResumeNothing;
        }
      } else {
        // The body that just fell may still occupy this elevation (for
        // example, a tall polycube). A blocked horizontal proposal spends the
        // player's Ice momentum rather than resuming after the fall finishes.
        state->reserved[0] = kResumeNothing;
      }
    }
    const bool player_fell = player_slid
        ? false
        : resolve_player_gravity(true, false, false);
    const bool player_advanced =
        player_rode_falling_object || player_slid || player_fell;
    if (player_fell && object_supported_by_player >= 0 &&
        ObjectIsActive(
            voxels, object_supported_by_player, width, height) &&
        !ObjectHasDirectSupport(
            voxels, object_supported_by_player, width, height)) {
      g_object_gravity_armed[object_supported_by_player] = 1;
      g_object_falling[object_supported_by_player] = 1;
    }
    const bool gravity_continues =
        state->player_falling != 0 ||
        ObjectsNeedGravityTick(voxels, count, width, height, false);
    if (!gravity_continues && state->reserved[0] == kResumeCommand) {
      state->reserved[0] = kResumeNothing;
      return store_and_finish(
          kMotionReady, objects_advanced || player_advanced);
    }
    if (!gravity_continues && state->reserved[0] == kResumeObjectIce) {
      state->reserved[0] = kResumeNothing;
      return store_and_finish(
          kMotionObjectIce, objects_advanced || player_advanced);
    }
    if (!gravity_continues && state->reserved[0] == kResumePlayerIce &&
        IsSupportedByIce(voxels, voxels[player_index], width, height)) {
      state->reserved[0] = kResumeNothing;
      return store_and_finish(
          kMotionPlayerIce, objects_advanced || player_advanced);
    }
    if (!gravity_continues) state->reserved[0] = kResumeNothing;
    return store_and_finish(
        gravity_continues ? kMotionGravity : kMotionComplete,
        objects_advanced || player_advanced,
        true);
  }

  if (state->phase == kMotionObjectIce) {
    RestoreMandatoryMomentum(state);
    const bool player_has_ice_momentum =
        IsSupportedByIce(voxels, voxels[player_index], width, height);
    const RiderSnapshot rider =
        CapturePlayerRider(voxels, player_index, width, height);
    g_ignored_support_voxel = player_index;
    const bool translated = TranslateMovingObjects(
        voxels,
        count,
        player_index,
        dx,
        dy,
        width,
        height,
        true,
        false,
        player_has_ice_momentum || rider.object >= 0);
    g_ignored_support_voxel = -1;
    if (!translated) {
      MarkObjectsForDeferredGravity(voxels, width, height, false);
      defer_player_gravity(false, false, true);
      const bool gravity_continues =
          state->player_falling != 0 ||
          ObjectsNeedGravityTick(voxels, count, width, height, false);
      if (gravity_continues) {
        state->reserved[0] = kResumeNothing;
      }
      return store_and_finish(
          gravity_continues ? kMotionGravity : kMotionComplete,
          ObjectsAdvancedThisTick(),
          true);
    }

    const bool player_rode_object =
        MovePlayerWithRider(voxels, player_index, rider, width, height);
    bool player_moved_after_translation = player_rode_object;
    if (!player_rode_object && player_has_ice_momentum) {
      // A deliberate push can put both the body and the player onto Ice. From
      // then on their horizontal proposals happen in the same animation tick:
      // the body vacates its trailing cell and the player slides into it.
      // Keeping these proposals synchronized also prevents the player from
      // spuriously pushing the body an extra cell after its own delayed slide.
      Voxel& sliding_player = voxels[player_index];
      const int32_t next_x = sliding_player.x + dx;
      const int32_t next_y = sliding_player.y + dy;
      if (IsInsideRoom(next_x, next_y, width, height) &&
          FindVoxelAt(
              voxels,
              next_x,
              next_y,
              sliding_player.z,
              width,
              height) < 0) {
        sliding_player.x = next_x;
        sliding_player.y = next_y;
        player_moved_after_translation = true;
      }
    }
    if (player_moved_after_translation) {
      BuildSpatialIndex(voxels, count, width, height);
    }
    if (MovingObjectsAreFullyOnIce(voxels, width, height)) {
      StoreMandatoryMomentum(state, voxels, count, width, height);
      return store_and_finish(kMotionObjectIce, true);
    }

    const bool object_momentum_remains = StoreSupportedMandatoryMomentum(
        state, voxels, count, width, height);
    MarkObjectsForDeferredGravity(voxels, width, height, false);
    defer_player_gravity(false, false, true);
    const bool gravity_continues =
        state->player_falling != 0 ||
        ObjectsNeedGravityTick(voxels, count, width, height, false);
    if (object_momentum_remains) {
      // Bodies that remain on Ice keep moving while bodies that have left
      // support fall during the same tick. TranslateMovingObjects advances
      // gravity for those unmoved bodies, preserving one unit per axis.
      state->reserved[0] = kResumeNothing;
      return store_and_finish(kMotionObjectIce, true);
    }
    state->reserved[0] = gravity_continues && IsSupportedByIce(
        voxels, voxels[player_index], width, height)
        ? kResumePlayerIce
        : kResumeNothing;
    return store_and_finish(
        gravity_continues ? kMotionGravity : kMotionComplete,
        true,
        true);
  }

  const bool automatic_ice_motion = state->phase == kMotionPlayerIce;
  const bool records_initial_tick =
      state->phase == kMotionReady && state->tick == 0;
  Voxel& player = voxels[player_index];
  const int32_t player_from_x = player.x;
  const int32_t player_from_y = player.y;
  const int32_t player_from_z = player.z;
  const bool started_on_ice = IsSupportedByIce(voxels, player, width, height);
  const int32_t target_x = player.x + dx;
  const int32_t target_y = player.y + dy;
  if (!IsInsideRoom(target_x, target_y, width, height)) {
    return store_and_finish(kMotionComplete, records_initial_tick);
  }

  const int32_t target_index =
      FindVoxelAt(voxels, target_x, target_y, player.z, width, height);
  bool pushed = false;
  bool object_keeps_sliding = false;
  bool rode_pushed_object = false;
  RiderSnapshot rider;
  if (target_index >= 0) {
    const int32_t object = g_voxel_object[target_index];
    if (automatic_ice_motion || object < 0) {
      return store_and_finish(kMotionComplete, records_initial_tick);
    }
    const int32_t target_support = player.z == INT32_MIN ? -1 : FindVoxelAt(
        voxels, target_x, target_y, player.z - 1, width, height);
    const bool target_support_stays = target_support >= 0 &&
        g_voxel_object[target_support] != object;
    if (!target_support_stays) {
      const int32_t current_support = player.z == INT32_MIN ? -1 : FindVoxelAt(
          voxels, player.x, player.y, player.z - 1, width, height);
      const bool may_leave_support = started_on_ice ||
          (current_support >= 0 &&
           (voxels[current_support].role == kFloorRole ||
            g_voxel_object[current_support] == object));
      if (!may_leave_support) {
        // Pushing a body must not bypass the normal ledge rule. Floor is the
        // deliberate walk-off surface; a player standing on a wall or another
        // non-Ice support cannot push into the body's unsupported old cell.
        return store_and_finish(kMotionComplete, records_initial_tick);
      }
    }
    rider = CapturePlayerRider(voxels, player_index, width, height);
    ClearMovingObjects();
    g_mandatory_objects[object] = 1;
    g_moving_objects[object] = 1;
    g_ignored_support_voxel = player_index;
    if (!TranslateMovingObjects(
            voxels,
            count,
            player_index,
            dx,
            dy,
            width,
            height,
            true,
            true,
            true)) {
      g_ignored_support_voxel = -1;
      return store_and_finish(kMotionComplete, records_initial_tick);
    }
    g_ignored_support_voxel = -1;
    pushed = true;
    object_keeps_sliding =
        MovingObjectsAreFullyOnIce(voxels, width, height);
    if (object_keeps_sliding) {
      StoreMandatoryMomentum(state, voxels, count, width, height);
    }
    rode_pushed_object =
        MovePlayerWithRider(voxels, player_index, rider, width, height);
  } else if (player.z != INT32_MIN) {
    const int32_t direct_support = FindVoxelAt(
        voxels, target_x, target_y, player.z - 1, width, height);
    if (direct_support < 0) {
      const int32_t lower_support = FindNearestVoxelBelow(
          voxels, count, target_x, target_y, player.z, -1, width);
      const int32_t current_support = FindVoxelAt(
          voxels, player.x, player.y, player.z - 1, width, height);
      const bool may_leave_support = automatic_ice_motion || started_on_ice ||
          (current_support >= 0 && voxels[current_support].role == kFloorRole &&
           lower_support < 0);
      if (!may_leave_support) {
        return store_and_finish(kMotionComplete, records_initial_tick);
      }
    }
  }

  if (!pushed) {
    ClearMovingObjects();
    if (!TranslateMovingObjects(
            voxels,
            count,
            player_index,
            dx,
            dy,
            width,
            height,
            true,
            true,
            true)) {
      return store_and_finish(kMotionComplete, records_initial_tick);
    }
  }

  if (!rode_pushed_object) {
    player.x = target_x;
    player.y = target_y;
  } else {
    // A rider follows its carrier, not the target cell that was computed
    // before the carrier's translation.
    (void)player_from_x;
    (void)player_from_y;
    (void)player_from_z;
  }
  BuildSpatialIndex(voxels, count, width, height);
  if (pushed) g_ignored_support_voxel = player_index;
  MarkObjectsForDeferredGravity(voxels, width, height, false);
  g_ignored_support_voxel = -1;

  if (object_keeps_sliding) {
    return store_and_finish(kMotionObjectIce, true);
  }

  const bool player_gravity_pending =
      defer_player_gravity(automatic_ice_motion, started_on_ice, pushed);
  if (player_gravity_pending) {
    const bool gravity_continues =
        state->player_falling != 0 ||
        ObjectsNeedGravityTick(voxels, count, width, height, false);
    state->reserved[0] =
        pushed && IsSupportedByIce(voxels, player, width, height)
        ? kResumePlayerIce
        : kResumeNothing;
    return store_and_finish(
        gravity_continues ? kMotionGravity : kMotionComplete,
        true,
        true);
  }

  if (IsSupportedByIce(voxels, player, width, height)) {
    return store_and_finish(kMotionPlayerIce, true);
  }
  if (!automatic_ice_motion && !pushed && player.z != INT32_MIN) {
    const int32_t run_up_x = player.x + dx;
    const int32_t run_up_y = player.y + dy;
    if (IsInsideRoom(run_up_x, run_up_y, width, height) &&
        FindVoxelAt(voxels, run_up_x, run_up_y, player.z, width, height) < 0) {
      const int32_t run_up_support = FindVoxelAt(
          voxels, run_up_x, run_up_y, player.z - 1, width, height);
      if (run_up_support >= 0 && voxels[run_up_support].role == kIceRole) {
        return store_and_finish(kMotionPlayerIce, true);
      }
    }
  }
  const bool gravity_continues =
      ObjectsNeedGravityTick(voxels, count, width, height, false);
  state->reserved[0] = gravity_continues &&
      IsSupportedByIce(voxels, player, width, height)
      ? kResumePlayerIce
      : kResumeNothing;
  return store_and_finish(
      gravity_continues ? kMotionGravity : kMotionComplete,
      true,
      true);
}
