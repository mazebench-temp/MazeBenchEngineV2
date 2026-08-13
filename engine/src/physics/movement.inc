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
    g_object_direction[object] = 0xff;
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
    if (support_object < 0 && !IsIceRole(voxels[support].role)) return -1;
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
    int32_t dz,
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
          voxels, next_x, next_y, voxel.z + dz, width, height);
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
            0,
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
    bool player_moves_horizontally,
    bool immediate_support_loss = false) {
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
    if (!(immediate_support_loss && g_component_objects[object] != 0) &&
        ObjectHasStableDirectSupport(voxels, object, width, height)) {
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
    int32_t dz,
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
          voxels, next_x, next_y, voxel.z + dz, width, height);
      if (occupant >= 0) {
        if (occupant == player_index) {
          if (!player_moves_horizontally) return false;
        } else if (g_voxel_object[occupant] != 0) {
          return false;
        }
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
      voxels[member].z += dz;
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
          voxels, next_x, next_y, voxel.z + dz, width, height);
      if (occupant < 0) continue;
      if (occupant == player_index) {
        if (player_moves_horizontally) continue;
        return false;
      }
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
        dz,
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
              voxels, next_x, next_y, voxel.z + dz, width, height);
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
      voxels[member].z += dz;
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
          voxels[member].z -= dz;
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
  return support >= 0 && IsIceRole(voxels[support].role);
}

int32_t SlopeSupportingVoxel(
    Voxel* voxels,
    const Voxel& voxel,
    int32_t width,
    int32_t height) {
  if (voxel.z == INT32_MIN) return -1;
  const int32_t support = FindVoxelAt(
      voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
  return support >= 0 && IsIceSlopeRole(voxels[support].role)
      ? support
      : -1;
}

int32_t RedirectSlopeDirection(
    Voxel* voxels,
    const Voxel& voxel,
    int32_t direction,
    int32_t width,
    int32_t height) {
  const int32_t support = SlopeSupportingVoxel(
      voxels, voxel, width, height);
  if (support < 0) return direction;
  const int32_t slope_direction = IceSlopeDirection(voxels[support].role);
  if (direction != slope_direction &&
      direction != OppositeDirection(slope_direction)) {
    return OppositeDirection(slope_direction);
  }
  if (direction != slope_direction) return direction;

  constexpr int32_t kDx[] = {0, 1, 0, -1};
  constexpr int32_t kDy[] = {-1, 0, 1, 0};
  const int32_t next_x = voxel.x + kDx[direction];
  const int32_t next_y = voxel.y + kDy[direction];
  if (!IsInsideRoom(next_x, next_y, width, height)) {
    return OppositeDirection(direction);
  }
  const int32_t ascending_slope = FindVoxelAt(
      voxels, next_x, next_y, voxel.z, width, height);
  if (ascending_slope >= 0 &&
      IceSlopeDirection(voxels[ascending_slope].role) == direction) {
    return direction;
  }
  const int32_t same_height_occupant = FindVoxelAt(
      voxels, next_x, next_y, voxel.z, width, height);
  if (same_height_occupant >= 0) {
    const int32_t occupant_object = g_voxel_object[same_height_occupant];
    if (occupant_object >= 0 &&
        g_mandatory_objects[occupant_object] != 0 &&
        g_object_direction[occupant_object] == direction) {
      // A leading momentum body vacates this high-edge cell in the same
      // direction. The follower is part of the convoy, not obstructed by it.
      return direction;
    }
    if (occupant_object >= 0 && g_object_falling[occupant_object] != 0 &&
        g_moving_objects[occupant_object] == 0) {
      // A simultaneous fall vacates the high-edge cell. The following body
      // continues uphill into that source instead of reflecting from a body
      // which is no longer there at the end of this tick.
      return direction;
    }
  }
  // The high edge may open into empty space. Momentum carries the body out at
  // its current elevation and gravity resolves on the following tick. Only a
  // real obstruction (or the room boundary above) reflects it downhill.
  return same_height_occupant < 0
      ? direction
      : OppositeDirection(direction);
}

int32_t SlopeVerticalDelta(
    Voxel* voxels,
    const Voxel& voxel,
    int32_t direction,
    int32_t width,
    int32_t height) {
  constexpr int32_t kDx[] = {0, 1, 0, -1};
  constexpr int32_t kDy[] = {-1, 0, 1, 0};
  const int32_t next_x = voxel.x + kDx[direction];
  const int32_t next_y = voxel.y + kDy[direction];
  if (IsInsideRoom(next_x, next_y, width, height)) {
    const int32_t ascending_slope = FindVoxelAt(
        voxels, next_x, next_y, voxel.z, width, height);
    if (ascending_slope >= 0 &&
        IceSlopeDirection(voxels[ascending_slope].role) == direction) {
      return 1;
    }
  }
  const int32_t support = SlopeSupportingVoxel(
      voxels, voxel, width, height);
  if (support >= 0 &&
      direction == OppositeDirection(IceSlopeDirection(voxels[support].role))) {
    // A descending wedge followed immediately by an ascending wedge at the
    // same base elevation keeps the rider level between their touching faces.
    if (IsInsideRoom(next_x, next_y, width, height) && voxel.z != INT32_MIN) {
      const int32_t next_support = FindVoxelAt(
          voxels, next_x, next_y, voxel.z - 1, width, height);
      if (next_support >= 0 &&
          IceSlopeDirection(voxels[next_support].role) == direction) {
        return 0;
      }
    }
    return -1;
  }
  return 0;
}

int32_t RedirectMovingObjectSlopeDirection(
    Voxel* voxels,
    int32_t direction,
    int32_t width,
    int32_t height) {
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0) continue;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const int32_t redirected = RedirectSlopeDirection(
          voxels, voxels[member], direction, width, height);
      if (redirected != direction) return redirected;
    }
  }
  return direction;
}

int32_t MovingObjectSlopeVerticalDelta(
    Voxel* voxels,
    int32_t direction,
    int32_t width,
    int32_t height) {
  int32_t delta = 0;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0) continue;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const int32_t member_delta = SlopeVerticalDelta(
          voxels, voxels[member], direction, width, height);
      if (member_delta == 0) continue;
      if (delta != 0 && delta != member_delta) return 0;
      delta = member_delta;
    }
  }
  return delta;
}

bool SceneHasIceSlopes(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height) {
  const WorkspaceData& workspace = ActiveWorkspace();
  if (workspace.prepared_voxels == voxels &&
      workspace.prepared_count == count &&
      workspace.prepared_width == width &&
      workspace.prepared_height == height) {
    return workspace.prepared_has_ice_slopes;
  }
  for (int32_t index = 0; index < count; ++index) {
    if (IsIceSlopeRole(voxels[index].role)) return true;
  }
  return false;
}

void PrepareSlopeObjectProposal(
    Voxel* voxels,
    int32_t object,
    int32_t width,
    int32_t height,
    bool allow_redirect) {
  constexpr int8_t kDx[] = {0, 1, 0, -1};
  constexpr int8_t kDy[] = {-1, 0, 1, 0};
  int32_t direction = g_object_direction[object] <= 3
      ? g_object_direction[object]
      : 0;
  if (allow_redirect) {
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const int32_t redirected = RedirectSlopeDirection(
          voxels, voxels[member], direction, width, height);
      if (redirected != direction) {
        direction = redirected;
        g_object_redirected[object] = 1;
        break;
      }
    }
  }
  g_object_direction[object] = static_cast<uint8_t>(direction);
  g_object_dx[object] = kDx[direction];
  g_object_dy[object] = kDy[direction];
  int32_t dz = 0;
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    const int32_t member_dz = SlopeVerticalDelta(
        voxels, voxels[member], direction, width, height);
    if (member_dz == 0) continue;
    if (dz != 0 && dz != member_dz) {
      dz = 0;
      break;
    }
    dz = member_dz;
  }
  g_object_dz[object] = static_cast<int8_t>(dz);
}

bool ObjectProposalVacatesCell(
    Voxel* voxels,
    int32_t object,
    int32_t x,
    int32_t y,
    int32_t z) {
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    if (voxels[member].x + g_object_dx[object] == x &&
        voxels[member].y + g_object_dy[object] == y &&
        voxels[member].z + g_object_dz[object] == z) {
      return false;
    }
  }
  return true;
}

bool ObjectProposalsSwap(
    Voxel* voxels,
    int32_t first,
    int32_t second) {
  for (int32_t first_member = g_object_head[first]; first_member >= 0;
       first_member = g_next_object_voxel[first_member]) {
    const int32_t first_x = voxels[first_member].x;
    const int32_t first_y = voxels[first_member].y;
    const int32_t first_z = voxels[first_member].z;
    const int32_t first_next_x = first_x + g_object_dx[first];
    const int32_t first_next_y = first_y + g_object_dy[first];
    const int32_t first_next_z = first_z + g_object_dz[first];
    for (int32_t second_member = g_object_head[second]; second_member >= 0;
         second_member = g_next_object_voxel[second_member]) {
      const int32_t second_x = voxels[second_member].x;
      const int32_t second_y = voxels[second_member].y;
      const int32_t second_z = voxels[second_member].z;
      if (first_next_x == second_x && first_next_y == second_y &&
          first_next_z == second_z &&
          second_x + g_object_dx[second] == first_x &&
          second_y + g_object_dy[second] == first_y &&
          second_z + g_object_dz[second] == first_z) {
        return true;
      }
    }
  }
  return false;
}

// Directional slope motion is transactional at the object level. Each rigid
// polycube owns its direction and vertical ramp delta; only a head-on contact
// with a reflected body propagates that reflection through a touching convoy.
// This keeps separated bodies independent while preventing them from swapping
// through one another when the lead body bounces off a ramp obstruction.
bool TranslateSlopeMomentumObjects(
    Voxel* voxels,
    int32_t count,
    int32_t player_index,
    int32_t width,
    int32_t height,
    bool allow_push,
    bool player_moves_horizontally,
    bool& player_was_pushed) {
  player_was_pushed = false;
  g_moving_objects_fell = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_fell_objects[object] = 0;
    g_object_redirected[object] = 0;
    if (g_mandatory_objects[object] != 0) {
      g_moving_objects[object] = 1;
    }
  }

  // A deliberate push discovers the whole touching weightless chain using
  // each body's own slope-adjusted destination.
  bool added = true;
  while (added) {
    added = false;
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_mandatory_objects[object] == 0 ||
          !ObjectIsActive(voxels, object, width, height)) {
        continue;
      }
      PrepareSlopeObjectProposal(voxels, object, width, height, true);
      for (int32_t member = g_object_head[object]; member >= 0;
           member = g_next_object_voxel[member]) {
        const int32_t next_x = voxels[member].x + g_object_dx[object];
        const int32_t next_y = voxels[member].y + g_object_dy[object];
        const int32_t next_z = voxels[member].z + g_object_dz[object];
        if (!IsInsideRoom(next_x, next_y, width, height)) continue;
        const int32_t occupant = FindVoxelAt(
            voxels, next_x, next_y, next_z, width, height);
        if (occupant < 0 || occupant == player_index) continue;
        const int32_t other = g_voxel_object[occupant];
        if (other < 0 || other == object ||
            g_mandatory_objects[other] != 0) {
          continue;
        }
        if (g_object_falling[other] != 0) {
          // A falling body vacates its source vertically; it must not be
          // recruited into the horizontal push closure.
          continue;
        }
        if (allow_push && g_object_kind[object] == kWeightlessObject &&
            g_object_kind[other] == kWeightlessObject &&
            ObjectIsFullyOnIce(voxels, other, width, height)) {
          g_mandatory_objects[other] = 1;
          g_moving_objects[other] = 1;
          g_object_direction[other] = g_object_direction[object];
          added = true;
        }
      }
    }
  }

  // A slope reflection propagates through exact head-on contacts. Recompute
  // after every propagation because adjacent bodies can be on different ramp
  // elevations even though their horizontal speed is identical.
  bool propagated = true;
  while (propagated) {
    propagated = false;
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_mandatory_objects[object] != 0) {
        PrepareSlopeObjectProposal(voxels, object, width, height, true);
      }
    }
    for (int32_t first = 0; first < g_object_count; ++first) {
      if (g_mandatory_objects[first] == 0 ||
          g_object_redirected[first] == 0) {
        continue;
      }
      for (int32_t second = 0; second < g_object_count; ++second) {
        if (first == second || g_mandatory_objects[second] == 0 ||
            g_object_direction[first] == g_object_direction[second] ||
            !ObjectProposalsSwap(voxels, first, second)) {
          continue;
        }
        g_object_direction[second] = g_object_direction[first];
        g_object_redirected[second] = 1;
        PrepareSlopeObjectProposal(voxels, second, width, height, false);
        propagated = true;
      }
    }
  }
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] != 0) {
      PrepareSlopeObjectProposal(voxels, object, width, height, false);
    }
  }

  int32_t player_push_dx = 0;
  int32_t player_push_dy = 0;
  bool has_slope_elevation_proposal = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] != 0 && g_object_dz[object] != 0) {
      has_slope_elevation_proposal = true;
      break;
    }
  }
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0 ||
        !ObjectIsActive(voxels, object, width, height)) {
      continue;
    }
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const int32_t next_x = voxels[member].x + g_object_dx[object];
      const int32_t next_y = voxels[member].y + g_object_dy[object];
      const int32_t next_z = voxels[member].z + g_object_dz[object];
      if (!IsInsideRoom(next_x, next_y, width, height)) return false;
      const int32_t occupant = FindVoxelAt(
          voxels, next_x, next_y, next_z, width, height);
      if (occupant < 0 || g_voxel_object[occupant] == object) continue;
      if (occupant == player_index) {
        if (player_moves_horizontally) continue;
        if (!has_slope_elevation_proposal) return false;
        if ((player_push_dx != 0 || player_push_dy != 0) &&
            (player_push_dx != g_object_dx[object] ||
             player_push_dy != g_object_dy[object])) {
          return false;
        }
        player_push_dx = g_object_dx[object];
        player_push_dy = g_object_dy[object];
        continue;
      }
      const int32_t other = g_voxel_object[occupant];
      if (other >= 0 && g_mandatory_objects[other] != 0 &&
          ObjectProposalVacatesCell(
              voxels, other, next_x, next_y, next_z)) {
        continue;
      }
      if (other >= 0 && g_object_falling[other] != 0 &&
          g_moving_objects[other] == 0 &&
          !FallingObjectRefillsCell(
              voxels, next_x, next_y, next_z, width, height)) {
        continue;
      }
      return false;
    }
  }

  // Two independent bodies may not claim the same destination.
  for (int32_t first = 0; first < g_object_count; ++first) {
    if (g_mandatory_objects[first] == 0) continue;
    for (int32_t second = first + 1; second < g_object_count; ++second) {
      if (g_mandatory_objects[second] == 0) continue;
      for (int32_t first_member = g_object_head[first]; first_member >= 0;
           first_member = g_next_object_voxel[first_member]) {
        for (int32_t second_member = g_object_head[second]; second_member >= 0;
             second_member = g_next_object_voxel[second_member]) {
          if (voxels[first_member].x + g_object_dx[first] ==
                  voxels[second_member].x + g_object_dx[second] &&
              voxels[first_member].y + g_object_dy[first] ==
                  voxels[second_member].y + g_object_dy[second] &&
              voxels[first_member].z + g_object_dz[first] ==
                  voxels[second_member].z + g_object_dz[second]) {
            return false;
          }
        }
      }
    }
  }

  if (player_push_dx != 0 || player_push_dy != 0) {
    const Voxel& player = voxels[player_index];
    const int32_t next_x = player.x + player_push_dx;
    const int32_t next_y = player.y + player_push_dy;
    if (!IsInsideRoom(next_x, next_y, width, height)) return false;
    const int32_t occupant = FindVoxelAt(
        voxels, next_x, next_y, player.z, width, height);
    if (occupant >= 0) {
      const int32_t other = g_voxel_object[occupant];
      if (other < 0 || g_mandatory_objects[other] == 0 ||
          !ObjectProposalVacatesCell(
              voxels, other, next_x, next_y, player.z)) {
        return false;
      }
    }
  }

  int32_t representative_dx = 0;
  int32_t representative_dy = 0;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0) continue;
    representative_dx = g_object_dx[object];
    representative_dy = g_object_dy[object];
    break;
  }

  // If a turning body slides sideways out from under a stationary stack, the
  // unsupported stack's downward proposal belongs to this same synchronized
  // tick. Propagate support loss upward before the gravity resolver so every
  // member of the stack descends one cell while its former carrier departs.
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_component_objects[object] = 0;
  }
  bool support_loss_changed = true;
  while (support_loss_changed) {
    support_loss_changed = false;
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_mandatory_objects[object] != 0 ||
          g_component_objects[object] != 0 ||
          !ObjectIsActive(voxels, object, width, height)) {
        continue;
      }
      bool had_departing_support = false;
      bool has_stable_support = false;
      for (int32_t member = g_object_head[object]; member >= 0;
           member = g_next_object_voxel[member]) {
        const Voxel& voxel = voxels[member];
        const int32_t support = FindVoxelAt(
            voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
        if (support < 0) continue;
        const int32_t support_object = g_voxel_object[support];
        if (support_object == object) continue;
        if (support_object >= 0 && g_mandatory_objects[support_object] != 0 &&
            ObjectProposalVacatesCell(
                voxels,
                support_object,
                voxel.x,
                voxel.y,
                voxel.z - 1)) {
          had_departing_support = true;
          continue;
        }
        if (support_object >= 0 &&
            g_component_objects[support_object] != 0) {
          had_departing_support = true;
          continue;
        }
        has_stable_support = true;
        break;
      }
      if (had_departing_support && !has_stable_support) {
        g_component_objects[object] = 1;
        g_object_falling[object] = 1;
        support_loss_changed = true;
      }
    }
  }
  AdvanceUnmovedGravityOneStep(
      voxels,
      count,
      width,
      height,
      player_index,
      representative_dx,
      representative_dy,
      player_moves_horizontally || player_push_dx != 0 || player_push_dy != 0,
      true);

  bool moved = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0) continue;
    moved = true;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      voxels[member].x += g_object_dx[object];
      voxels[member].y += g_object_dy[object];
      voxels[member].z += g_object_dz[object];
    }
  }
  if (!moved) {
    // A latent-momentum body may have advanced only along gravity and landed
    // on a lower slope. Report that synchronized movement to the caller so it
    // can promote the saved direction back to active Ice momentum instead of
    // completing the command on the landing frame.
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_fell_objects[object] != 0) return true;
    }
    return false;
  }
  if (player_push_dx != 0 || player_push_dy != 0) {
    voxels[player_index].x += player_push_dx;
    voxels[player_index].y += player_push_dy;
    player_was_pushed = true;
  }
  BuildSpatialIndex(voxels, count, width, height);
  MarkObjectsForDeferredGravity(voxels, width, height, false);
  return true;
}

bool VoxelWillLandOnSlope(
    Voxel* voxels,
    int32_t count,
    const Voxel& voxel,
    int32_t ignored_object,
    int32_t width) {
  const int32_t support = FindNearestVoxelBelow(
      voxels, count, voxel.x, voxel.y, voxel.z, ignored_object, width);
  return support >= 0 && IsIceSlopeRole(voxels[support].role);
}

bool MovingObjectWillLandOnSlope(
    Voxel* voxels,
    int32_t count,
    int32_t width) {
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_mandatory_objects[object] == 0) continue;
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
