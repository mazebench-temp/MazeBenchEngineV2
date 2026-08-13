bool AddMandatoryObject(int32_t object, int32_t& queue_end) {
  if (object < 0 || g_mandatory_objects[object] != 0) return false;
  g_mandatory_objects[object] = 1;
  g_moving_objects[object] = 1;
  g_carried_objects[object] = 0;
  g_object_queue[queue_end++] = object;
  return true;
}

void ClearMovingObjects() {
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_moving_objects[object] = 0;
    g_mandatory_objects[object] = 0;
    g_carried_objects[object] = 0;
    g_rejected_carriers[object] = 0;
    g_fell_objects[object] = 0;
    g_carrier_parent[object] = -1;
  }
}

int32_t MovingSupportBelowObject(
    Voxel* voxels,
    int32_t object,
    int32_t player_index,
    bool carry_from_player,
    int32_t width,
    int32_t height) {
  int32_t moving_support = -1;
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    const Voxel& voxel = voxels[member];
    if (voxel.z == INT32_MIN) continue;
    const int32_t support = FindVoxelAt(
        voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
    if (support < 0) continue;
    if (g_voxel_object[support] == object) continue;
    if (carry_from_player && support == player_index) {
      moving_support = -2;
      continue;
    }
    const int32_t support_object = g_voxel_object[support];
    if (support_object >= 0 && support_object != object &&
        g_moving_objects[support_object] != 0) {
      if (moving_support == -1) moving_support = support_object;
      continue;
    }
    // Ordinary terrain provides an immovable foothold, so one moving contact
    // cannot carry the body. Ice has no such anchoring friction, and contact
    // with a different movable object is resolved by the support graph.
    if (support_object < 0 && voxels[support].role != kIceRole) return -1;
  }
  return moving_support;
}

void BuildCarriedObjects(
    Voxel* voxels,
    int32_t player_index,
    bool carry_from_player,
    int32_t width,
    int32_t height) {
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_moving_objects[object] = g_mandatory_objects[object];
    g_carried_objects[object] = 0;
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_moving_objects[object] != 0 || g_rejected_carriers[object] != 0 ||
          !ObjectIsActive(voxels, object, width, height)) {
        continue;
      }
      const int32_t carrier = MovingSupportBelowObject(
          voxels, object, player_index, carry_from_player, width, height);
      if (carrier != -1) {
        g_moving_objects[object] = 1;
        g_carried_objects[object] = 1;
        g_carrier_parent[object] = carrier;
        changed = true;
      }
    }
  }
}

bool ResolveCarriedCollisions(
    Voxel* voxels,
    int32_t player_index,
    int32_t dx,
    int32_t dy,
    int32_t width,
    int32_t height,
    bool allow_push,
    bool& added_mandatory) {
  bool changed = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_carried_objects[object] == 0) continue;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const Voxel& voxel = voxels[member];
      const int32_t next_x = voxel.x + dx;
      const int32_t next_y = voxel.y + dy;
      if (!IsInsideRoom(next_x, next_y, width, height)) {
        g_rejected_carriers[object] = 1;
        changed = true;
        break;
      }
      const int32_t occupant = FindVoxelAt(
          voxels, next_x, next_y, voxel.z, width, height);
      if (occupant < 0 || occupant == player_index ||
          g_voxel_object[occupant] == object) {
        continue;
      }
      const int32_t other = g_voxel_object[occupant];
      if (other >= 0 && g_moving_objects[other] != 0) continue;
      if (other >= 0 && g_object_falling[other] != 0) {
        // A falling occupant vacates this source cell in the synchronized
        // vertical part of the tick.
        continue;
      }
      if (allow_push && g_object_kind[object] == kWeightlessObject &&
          other >= 0 && g_object_kind[other] == kWeightlessObject &&
          ObjectIsFullyOnIce(voxels, other, width, height)) {
        g_mandatory_objects[other] = 1;
        g_moving_objects[other] = 1;
        g_carried_objects[other] = 0;
        added_mandatory = true;
        changed = true;
        continue;
      }
      g_rejected_carriers[object] = 1;
      changed = true;
      break;
    }
  }
  return changed;
}

bool ObjectHasCarrierBelow(
    Voxel* voxels,
    int32_t object,
    int32_t carrier) {
  if (carrier < 0) return false;
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    const Voxel& voxel = voxels[member];
    for (int32_t support = g_object_head[carrier]; support >= 0;
         support = g_next_object_voxel[support]) {
      if (voxels[support].x == voxel.x && voxels[support].y == voxel.y &&
          voxels[support].z < voxel.z) {
        return true;
      }
    }
  }
  return false;
}
bool FallingObjectRefillsCell(
    Voxel* voxels,
    int32_t x,
    int32_t y,
    int32_t z,
    int32_t width,
    int32_t height) {
  if (z == INT32_MAX) return false;
  const int32_t above = FindVoxelAt(
      voxels, x, y, z + 1, width, height);
  if (above < 0) return false;
  const int32_t object = g_voxel_object[above];
  return object >= 0 && g_object_falling[object] != 0 &&
      g_moving_objects[object] == 0;
}

void TranslateObjectsCarriedByPlayerOneStep(
    Voxel* voxels,
    int32_t player_index,
    int32_t dx,
    int32_t dy,
    int32_t width,
    int32_t height) {
  ClearMovingObjects();
  for (;;) {
    BuildCarriedObjects(
        voxels, player_index, true, width, height);
    bool added_mandatory = false;
    if (!ResolveCarriedCollisions(
            voxels,
            player_index,
            dx,
            dy,
            width,
            height,
            false,
            added_mandatory)) {
      break;
    }
  }
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_carried_objects[object] == 0 ||
        g_rejected_carriers[object] != 0) {
      continue;
    }
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      voxels[member].x += dx;
      voxels[member].y += dy;
    }
  }
}

void AdvanceUnmovedGravityOneStep(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t player_index,
    int32_t dx,
    int32_t dy,
    bool player_moves_horizontally) {
  bool found_pending = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_pending_fall[object] = 0;
    if (g_moving_objects[object] != 0 ||
        (g_carrier_parent[object] == -2 &&
         g_rejected_carriers[object] == 0) ||
        !ObjectIsActive(voxels, object, width, height) ||
        g_object_gravity_armed[object] == 0) {
      continue;
    }
    if (ObjectHasStableDirectSupport(voxels, object, width, height)) {
      g_object_falling[object] = 0;
      continue;
    }
    if (g_object_falling[object] == 0) {
      // Support loss starts a fall. Its first one-voxel displacement is
      // proposed on the next synchronized movement tick.
      g_object_falling[object] = 1;
      continue;
    }
    g_pending_fall[object] = 1;
    found_pending = true;
  }
  if (!found_pending) return;

  // Falling bodies form simultaneous vertical proposals. A body may enter a
  // cell vacated by another falling body, but never one retained this tick.
  bool changed = true;
  while (changed) {
    changed = false;
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_pending_fall[object] == 0) continue;
      bool blocked = false;
      for (int32_t member = g_object_head[object]; member >= 0;
           member = g_next_object_voxel[member]) {
        if (voxels[member].z == INT32_MIN) {
          blocked = true;
          break;
        }
        const int32_t target_x = voxels[member].x;
        const int32_t target_y = voxels[member].y;
        const int32_t target_z = voxels[member].z - 1;
        const int32_t horizontal_source = FindVoxelAt(
            voxels,
            target_x - dx,
            target_y - dy,
            target_z,
            width,
            height);
        if ((horizontal_source >= 0 &&
             g_voxel_object[horizontal_source] >= 0 &&
             g_moving_objects[g_voxel_object[horizontal_source]] != 0) ||
            (player_moves_horizontally && player_index >= 0 &&
             voxels[player_index].x + dx == target_x &&
             voxels[player_index].y + dy == target_y &&
             voxels[player_index].z == target_z)) {
          // Horizontal motion wins a same-cell race. The descending body
          // remains above the incoming support and is caught this tick.
          blocked = true;
          break;
        }
        const int32_t occupant = FindVoxelAt(
            voxels,
            target_x,
            target_y,
            target_z,
            width,
            height);
        if (occupant < 0) continue;
        const int32_t other = g_voxel_object[occupant];
        if (other == object ||
            (other >= 0 &&
             (g_pending_fall[other] != 0 ||
              g_moving_objects[other] != 0)) ||
            (occupant == player_index && player_moves_horizontally)) {
          continue;
        }
        blocked = true;
        break;
      }
      if (blocked) {
        g_pending_fall[object] = 0;
        g_object_falling[object] = 0;
        changed = true;
      }
    }
  }

  bool moved = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_pending_fall[object] == 0) continue;
    bool has_lower_support = false;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      if (FindNearestVoxelBelow(
              voxels,
              count,
              voxels[member].x,
              voxels[member].y,
              voxels[member].z,
              object,
              width) >= 0) {
        has_lower_support = true;
        break;
      }
    }
    const bool disappears = !has_lower_support &&
        ObjectHasPassedEverythingElse(voxels, count, object, width, height);
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      --voxels[member].z;
      if (disappears) voxels[member].x = -1;
    }
    if (disappears) g_object_falling[object] = 0;
    moved = true;
  }
  if (!moved) return;
  BuildSpatialIndex(voxels, count, width, height);
  // Clear momentum for bodies that landed on support which will remain after
  // the horizontal half of this tick. Propagate from the bottom upward so a
  // newly stable body can support another descending body in the same frame.
  bool settled = true;
  while (settled) {
    settled = false;
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_pending_fall[object] == 0) continue;
      bool has_stationary_support = false;
      for (int32_t member = g_object_head[object]; member >= 0;
           member = g_next_object_voxel[member]) {
        const Voxel& voxel = voxels[member];
        const int32_t support = FindVoxelAt(
            voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
        if (support < 0 || g_voxel_object[support] == object) continue;
        if (support == player_index && player_moves_horizontally) continue;
        const int32_t support_object = g_voxel_object[support];
        if (support_object >= 0 &&
            (g_moving_objects[support_object] != 0 ||
             g_pending_fall[support_object] != 0)) {
          continue;
        }
        has_stationary_support = true;
        break;
      }
      if (has_stationary_support) {
        g_pending_fall[object] = 0;
        g_object_falling[object] = 0;
        settled = true;
      }
    }
  }
}

void MarkObjectsForDeferredGravity(
    Voxel* voxels,
    int32_t width,
    int32_t height,
    bool moving_only) {
  if (g_object_count == 1) {
    g_component_objects[0] = 0;
    if (!ObjectIsActive(voxels, 0, width, height) ||
        g_object_gravity_armed[0] == 0 ||
        (moving_only && g_moving_objects[0] == 0)) {
      return;
    }
    g_object_falling[0] =
        ObjectHasDirectSupport(voxels, 0, width, height) ? 0 : 1;
    return;
  }

  // Horizontal translation and gravity are separate ticks. First find which
  // translated bodies are connected to stationary support, including through
  // another translated body. Unsupported cycles remain ungrounded.
  auto eligible = [&](int32_t object) {
    return ObjectIsActive(voxels, object, width, height) &&
        g_object_gravity_armed[object] != 0 &&
        (!moving_only || g_moving_objects[object] != 0);
  };
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_component_objects[object] = 0;
    if (!eligible(object)) continue;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const Voxel& voxel = voxels[member];
      if (voxel.z == INT32_MIN) continue;
      const int32_t support = FindVoxelAt(
          voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
      if (support < 0 || support == g_ignored_support_voxel) continue;
      const int32_t support_object = g_voxel_object[support];
      if (support_object != object &&
          (support_object < 0 || !eligible(support_object))) {
        g_component_objects[object] = 1;
        break;
      }
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (!eligible(object) || g_component_objects[object] != 0) {
        continue;
      }
      for (int32_t member = g_object_head[object]; member >= 0;
           member = g_next_object_voxel[member]) {
        const Voxel& voxel = voxels[member];
        if (voxel.z == INT32_MIN) continue;
        const int32_t support = FindVoxelAt(
            voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
        if (support < 0 || support == g_ignored_support_voxel) continue;
        const int32_t support_object = g_voxel_object[support];
        if (support_object >= 0 && support_object != object &&
            g_component_objects[support_object] != 0) {
          g_component_objects[object] = 1;
          changed = true;
          break;
        }
      }
    }
  }

  for (int32_t object = 0; object < g_object_count; ++object) {
    if (!eligible(object)) continue;
    g_object_falling[object] = g_component_objects[object] == 0 ? 1 : 0;
  }
}

bool TranslateMovingObjects(
    Voxel* voxels,
    int32_t count,
    int32_t player_index,
    int32_t dx,
    int32_t dy,
    int32_t width,
    int32_t height,
    bool carry_from_player,
    bool allow_push,
    bool player_moves_horizontally) {
  g_moving_objects_fell = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_fell_objects[object] = 0;
  }

  if (g_object_count == 1 && g_mandatory_objects[0] != 0 &&
      ObjectIsActive(voxels, 0, width, height)) {
    // The overwhelmingly common Sokoban push has no carrier graph, object
    // closure, or inter-body race to resolve. Preserve the exact general-path
    // collision rules while avoiding several redundant graph passes per Ice
    // cell. Complex scenes continue through the solver below.
    for (int32_t member = g_object_head[0]; member >= 0;
         member = g_next_object_voxel[member]) {
      const Voxel& voxel = voxels[member];
      const int32_t next_x = voxel.x + dx;
      const int32_t next_y = voxel.y + dy;
      if (!IsInsideRoom(next_x, next_y, width, height)) return false;
      const int32_t occupant = FindVoxelAt(
          voxels, next_x, next_y, voxel.z, width, height);
      if (occupant >= 0 && occupant != player_index &&
          g_voxel_object[occupant] != 0) {
        return false;
      }
    }
    g_moving_objects[0] = 1;
    g_carried_objects[0] = 0;
    g_rejected_carriers[0] = 0;
    g_carrier_parent[0] = -1;
    for (int32_t member = g_object_head[0]; member >= 0;
         member = g_next_object_voxel[member]) {
      voxels[member].x += dx;
      voxels[member].y += dy;
    }
    BuildSpatialIndex(voxels, count, width, height);
    MarkObjectsForDeferredGravity(voxels, width, height, false);
    return true;
  }

  int32_t queue_begin = 0;
  int32_t queue_end = 0;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] != 0 &&
        ObjectIsActive(voxels, object, width, height)) {
      g_object_queue[queue_end++] = object;
    }
  }
  while (queue_begin < queue_end) {
    const int32_t object = g_object_queue[queue_begin++];
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const Voxel& voxel = voxels[member];
      const int32_t next_x = voxel.x + dx;
      const int32_t next_y = voxel.y + dy;
      if (!IsInsideRoom(next_x, next_y, width, height)) return false;
      const int32_t occupant = FindVoxelAt(
          voxels, next_x, next_y, voxel.z, width, height);
      if (occupant < 0 || occupant == player_index) continue;
      const int32_t other_object = g_voxel_object[occupant];
      if (other_object == object ||
          (other_object >= 0 && g_mandatory_objects[other_object] != 0)) {
        continue;
      }
      if (other_object >= 0 && g_object_falling[other_object] != 0 &&
          g_moving_objects[other_object] == 0) {
        // The occupant's synchronized downward proposal vacates this cell.
        // A vertical stack can refill it in the same tick, however, in which
        // case the trailing horizontal body must stop before gravity commits.
        if (FallingObjectRefillsCell(
                voxels, next_x, next_y, voxel.z, width, height)) {
          return false;
        }
        continue;
      }
      if (allow_push &&
          g_object_kind[object] == kWeightlessObject && other_object >= 0 &&
          g_object_kind[other_object] == kWeightlessObject) {
        AddMandatoryObject(other_object, queue_end);
        continue;
      }
      return false;
    }
  }

  for (;;) {
    BuildCarriedObjects(
        voxels, player_index, carry_from_player, width, height);
    bool added_mandatory = false;
    const bool changed = ResolveCarriedCollisions(
        voxels,
        player_index,
        dx,
        dy,
        width,
        height,
        allow_push,
        added_mandatory);
    if (!changed) break;
    if (added_mandatory) {
      // Newly pushed passengers must receive the same collision closure as
      // the original deliberate push before the substep can commit.
      queue_begin = 0;
      queue_end = 0;
      for (int32_t object = 0; object < g_object_count; ++object) {
        if (g_mandatory_objects[object] != 0 &&
            ObjectIsActive(voxels, object, width, height)) {
          g_object_queue[queue_end++] = object;
        }
      }
      while (queue_begin < queue_end) {
        const int32_t object = g_object_queue[queue_begin++];
        for (int32_t member = g_object_head[object]; member >= 0;
             member = g_next_object_voxel[member]) {
          const Voxel& voxel = voxels[member];
          const int32_t next_x = voxel.x + dx;
          const int32_t next_y = voxel.y + dy;
          if (!IsInsideRoom(next_x, next_y, width, height)) return false;
          const int32_t occupant = FindVoxelAt(
              voxels, next_x, next_y, voxel.z, width, height);
          if (occupant < 0 || occupant == player_index) continue;
          const int32_t other = g_voxel_object[occupant];
          if (other == object ||
              (other >= 0 && g_mandatory_objects[other] != 0)) {
            continue;
          }
          if (allow_push &&
              g_object_kind[object] == kWeightlessObject && other >= 0 &&
              g_object_kind[other] == kWeightlessObject) {
            AddMandatoryObject(other, queue_end);
            continue;
          }
          return false;
        }
      }
    }
  }

  bool found_moving = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    found_moving |= g_moving_objects[object] != 0;
  }
  if (!found_moving) {
    AdvanceUnmovedGravityOneStep(
        voxels,
        count,
        width,
        height,
        player_index,
        dx,
        dy,
        player_moves_horizontally);
    return true;
  }

  // Commit already-armed vertical proposals before horizontal translation.
  // Collision validation above treats those source cells as vacating, while
  // the gravity resolver cancels any destination contested by a horizontal
  // proposal. Both axes therefore advance once in the same animation tick.
  AdvanceUnmovedGravityOneStep(
      voxels,
      count,
      width,
      height,
      player_index,
      dx,
      dy,
      player_moves_horizontally);

  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_moving_objects[object] == 0) continue;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      voxels[member].x += dx;
      voxels[member].y += dy;
    }
  }
  BuildSpatialIndex(voxels, count, width, height);
  // A stationary body can lose support when a different body translates out
  // from beneath it. Mark every armed body here so that its downward proposal
  // participates in the next synchronized tick, just like a translated body
  // which leaves the edge of its own support.
  MarkObjectsForDeferredGravity(voxels, width, height, false);

  bool changed = true;
  while (changed) {
    changed = false;
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_carried_objects[object] == 0 || g_rejected_carriers[object] != 0) {
        continue;
      }
      const int32_t carrier = g_carrier_parent[object];
      if (carrier >= 0 && g_fell_objects[carrier] == 0 &&
          g_rejected_carriers[carrier] == 0) {
        continue;
      }
      if (carrier == -2) continue;
      if (!ObjectHasCarrierBelow(voxels, object, carrier)) {
        g_rejected_carriers[object] = 1;
        for (int32_t member = g_object_head[object]; member >= 0;
             member = g_next_object_voxel[member]) {
          voxels[member].x -= dx;
          voxels[member].y -= dy;
        }
      }
      g_carried_objects[object] = 0;
      g_moving_objects[object] = g_mandatory_objects[object];
      BuildSpatialIndex(voxels, count, width, height);
      changed = true;
    }
  }
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_fell_objects[object] != 0) g_mandatory_objects[object] = 0;
  }
  MarkObjectsForDeferredGravity(voxels, width, height, false);
  return true;
}

bool MovingObjectsAreFullyOnIce(
    Voxel* voxels,
    int32_t width,
    int32_t height) {
  bool found_active = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0 ||
        !ObjectIsActive(voxels, object, width, height)) {
      continue;
    }
    found_active = true;
    if (!ObjectIsFullyOnIce(voxels, object, width, height)) return false;
  }
  return found_active;
}

bool IsSupportedByIce(
    Voxel* voxels,
    const Voxel& voxel,
    int32_t width,
    int32_t height) {
  if (voxel.z == INT32_MIN) return false;
  const int32_t support = FindVoxelAt(
      voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
  return support >= 0 && voxels[support].role == kIceRole;
}

void SettleAllObjects(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height) {
  ClearMovingObjects();
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_moving_objects[object] = 1;
  }
  SettleFlaggedObjects(voxels, count, width, height);
}

bool ObjectsNeedGravityTick(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    bool rebuild_spatial_index = true) {
  // Carrier rejection can restore a body after an earlier translation built
  // the index. Rebuild here so the final quiescence decision never observes a
  // ghost support at a body's former coordinate.
  if (rebuild_spatial_index) {
    BuildSpatialIndex(voxels, count, width, height);
  }
  bool needs_tick = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (!ObjectIsActive(voxels, object, width, height)) continue;
    // A vertically interlocked component can give every member apparent
    // direct support even while the entire component is falling. The settle
    // pass owns the decision to clear this flag when the component lands.
    if (g_object_gravity_armed[object] != 0 &&
        g_object_falling[object] != 0) {
      if (ObjectHasStableDirectSupport(
              voxels, object, width, height)) {
        g_object_falling[object] = 0;
        continue;
      }
      needs_tick = true;
      continue;
    }
    if (ObjectHasDirectSupport(voxels, object, width, height)) {
      g_object_falling[object] = 0;
      g_object_gravity_armed[object] = 1;
      continue;
    }
    // A command is not quiescent while an active body is unsupported. This
    // also catches a carried body that was rejected after its support moved:
    // it must begin falling in the current command, not remain suspended until
    // the next input happens to initialize gravity again.
    g_object_gravity_armed[object] = 1;
    g_object_falling[object] = 1;
    needs_tick = true;
  }
  return needs_tick;
}

bool ObjectsAdvancedThisTick() {
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_fell_objects[object] != 0) return true;
  }
  return false;
}
