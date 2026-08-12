int32_t CreateObject(uint8_t kind) {
  const int32_t object = g_object_count++;
  g_object_head[object] = -1;
  g_object_kind[object] = kind;
  g_object_gravity_armed[object] = 0;
  g_object_falling[object] = 0;
  return object;
}

void AddVoxelToObject(int32_t voxel, int32_t object) {
  g_voxel_object[voxel] = object;
  g_next_object_voxel[voxel] = g_object_head[object];
  g_object_head[object] = voxel;
}

void BuildObjects(Voxel* voxels, int32_t count) {
  g_object_count = 0;
  g_weightless_object_count = 0;
  ResetHash();
  for (int32_t index = 0; index < count; ++index) {
    g_voxel_object[index] = -1;
    g_next_object_voxel[index] = -1;
    if (voxels[index].role == kPushableRole) {
      AddVoxelToObject(index, CreateObject(kNormalObject));
      continue;
    }
    if (voxels[index].role != kWeightlessPushableRole) continue;

    const uint64_t object_key =
        (static_cast<uint64_t>(voxels[index].role) << 32u) |
        static_cast<uint32_t>(voxels[index].generic_id);
    int32_t object = HashFind(object_key);
    if (object < 0) {
      object = CreateObject(kWeightlessObject);
      ++g_weightless_object_count;
      HashInsert(object_key, object);
    }
    AddVoxelToObject(index, object);
  }
}

bool ObjectIsActive(Voxel* voxels, int32_t object, int32_t width, int32_t height) {
  const int32_t head = g_object_head[object];
  return head >= 0 && IsInsideRoom(voxels[head].x, voxels[head].y, width, height);
}

int32_t FindNearestVoxelBelow(
    Voxel* voxels,
    int32_t count,
    int32_t x,
    int32_t y,
    int32_t z,
    int32_t excluded_object,
    int32_t width) {
  int32_t best = -1;
  int32_t best_z = INT32_MIN;
  if (g_dense_columns) {
    const int32_t cell = y * width + x;
    for (int32_t index = g_column_heads[cell]; index >= 0;
         index = g_next_column_voxel[index]) {
      if (index == g_ignored_support_voxel) continue;
      if (excluded_object >= 0 && g_voxel_object[index] == excluded_object) continue;
      const int32_t candidate_z = voxels[index].z;
      if (candidate_z < z && candidate_z > best_z) {
        best = index;
        best_z = candidate_z;
      }
    }
    return best;
  }

  for (int32_t index = 0; index < count; ++index) {
    if (index == g_ignored_support_voxel) continue;
    if ((excluded_object >= 0 && g_voxel_object[index] == excluded_object) ||
        voxels[index].x != x ||
        voxels[index].y != y) {
      continue;
    }
    const int32_t candidate_z = voxels[index].z;
    if (candidate_z < z && candidate_z > best_z) {
      best = index;
      best_z = candidate_z;
    }
  }
  return best;
}

int32_t FindNearestVoxelBelowComponent(
    Voxel* voxels,
    int32_t count,
    int32_t x,
    int32_t y,
    int32_t z,
  int32_t width) {
  int32_t best = -1;
  int32_t best_z = INT32_MIN;
  if (g_dense_columns) {
    for (int32_t index = g_column_heads[y * width + x]; index >= 0;
         index = g_next_column_voxel[index]) {
      if (index == g_ignored_support_voxel) continue;
      const int32_t object = g_voxel_object[index];
      if (object >= 0 && g_component_objects[object] != 0) continue;
      const int32_t candidate_z = voxels[index].z;
      if (candidate_z < z && candidate_z > best_z) {
        best = index;
        best_z = candidate_z;
      }
    }
    return best;
  }

  for (int32_t index = 0; index < count; ++index) {
    if (index == g_ignored_support_voxel || voxels[index].x != x ||
        voxels[index].y != y) {
      continue;
    }
    const int32_t object = g_voxel_object[index];
    if (object >= 0 && g_component_objects[object] != 0) continue;
    const int32_t candidate_z = voxels[index].z;
    if (candidate_z < z && candidate_z > best_z) {
      best = index;
      best_z = candidate_z;
    }
  }
  return best;
}

bool ObjectHasDirectSupport(
    Voxel* voxels,
    int32_t object,
    int32_t width,
    int32_t height) {
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    const Voxel& voxel = voxels[member];
    if (voxel.z == INT32_MIN) continue;
    const int32_t support = FindVoxelAt(
        voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
    if (support >= 0 && support != g_ignored_support_voxel &&
        g_voxel_object[support] != object) {
      return true;
    }
  }
  return false;
}

bool ObjectHasStableDirectSupport(
    Voxel* voxels,
    int32_t object,
    int32_t width,
    int32_t height) {
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    const Voxel& voxel = voxels[member];
    if (voxel.z == INT32_MIN) continue;
    const int32_t support = FindVoxelAt(
        voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
    if (support < 0 || support == g_ignored_support_voxel) continue;
    const int32_t support_object = g_voxel_object[support];
    if (support_object != object &&
        (support_object < 0 || g_object_falling[support_object] == 0)) {
      return true;
    }
  }
  return false;
}

bool ObjectIsFullyOnIce(
    Voxel* voxels,
    int32_t object,
    int32_t width,
    int32_t height) {
  bool found_bottom = false;
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    const Voxel& voxel = voxels[member];
    if (voxel.z == INT32_MIN) return false;
    const int32_t support = FindVoxelAt(
        voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
    if (support >= 0 && g_voxel_object[support] == object) continue;
    // An overhanging part of a rigid polycube contributes no friction. Ice
    // motion continues while at least one exposed cell is on Ice and every
    // other actual support is Ice too.
    if (support < 0) continue;
    if (voxels[support].role != kIceRole) return false;
    found_bottom = true;
  }
  return found_bottom;
}

int32_t ObjectHighestZ(Voxel* voxels, int32_t object) {
  int32_t highest = INT32_MIN;
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    if (voxels[member].z > highest) highest = voxels[member].z;
  }
  return highest;
}

int32_t LowestOtherActiveZ(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t excluded_object,
    int32_t excluded_voxel,
    bool exclude_component,
    bool exclude_falling_objects) {
  int32_t lowest = INT32_MAX;
  for (int32_t index = 0; index < count; ++index) {
    const Voxel& voxel = voxels[index];
    if (index == excluded_voxel || voxel.z == INT32_MIN ||
        !IsInsideRoom(voxel.x, voxel.y, width, height)) {
      continue;
    }
    const int32_t object = g_voxel_object[index];
    if (voxel.role == kPlayerRole && voxel.z != INT32_MIN) {
      const int32_t support = FindVoxelAt(
          voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
      const int32_t support_object =
          support >= 0 ? g_voxel_object[support] : -1;
      if (support_object >= 0 &&
          ((excluded_object >= 0 && support_object == excluded_object) ||
           (exclude_component &&
            g_component_objects[support_object] != 0) ||
           (exclude_falling_objects &&
            g_object_falling[support_object] != 0))) {
        // A rider descends as part of its supporting body's fall cohort and
        // must not make that body's abyss boundary recede forever.
        continue;
      }
    }
    if ((excluded_object >= 0 && object == excluded_object) ||
        (exclude_component && object >= 0 &&
         g_component_objects[object] != 0) ||
        (exclude_falling_objects && object >= 0 &&
         g_object_falling[object] != 0)) {
      continue;
    }
    if (voxel.z < lowest) lowest = voxel.z;
  }
  // Preserve the historical row-zero abyss when a body is literally the
  // room's only remaining geometry. Otherwise the authored room determines
  // its own abyss depth, including geometry on negative rows.
  return lowest == INT32_MAX ? 0 : lowest;
}

bool ObjectHasPassedEverythingElse(
    Voxel* voxels,
    int32_t count,
    int32_t object,
    int32_t width,
    int32_t height) {
  return ObjectHighestZ(voxels, object) < LowestOtherActiveZ(
      voxels, count, width, height, object, -1, false, true);
}

bool SettleObject(
    Voxel* voxels,
    int32_t count,
    int32_t object,
    int32_t width,
    int32_t height) {
  if (!ObjectIsActive(voxels, object, width, height) ||
      ObjectHasDirectSupport(voxels, object, width, height)) {
    if (ObjectIsActive(voxels, object, width, height)) {
      g_object_gravity_armed[object] = 1;
      g_object_falling[object] = 0;
    }
    return false;
  }

  int64_t drop = INT64_MAX;
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    const Voxel& voxel = voxels[member];
    const int32_t support = FindNearestVoxelBelow(
        voxels, count, voxel.x, voxel.y, voxel.z, object, width);
    if (support < 0) continue;
    const int64_t candidate = static_cast<int64_t>(voxel.z) - voxels[support].z - 1;
    if (candidate < drop) drop = candidate;
  }

  if (drop == INT64_MAX && g_object_gravity_armed[object] == 0) {
    // The editor permits intentionally floating constructions at any signed
    // elevation. Gravity begins once a body has support or something below
    // it to fall toward; merely loading a floating body does not delete it.
    return false;
  }
  g_moving_objects_fell = true;
  g_fell_objects[object] = 1;
  g_object_gravity_armed[object] = 1;
  if (drop == INT64_MAX) {
    const bool disappears = ObjectHasPassedEverythingElse(
        voxels, count, object, width, height);
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      if (voxels[member].z != INT32_MIN) --voxels[member].z;
      if (disappears) voxels[member].x = -1;
    }
    g_object_falling[object] = disappears ? 0 : 1;
    return true;
  }
  if (drop <= 0) return false;
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    --voxels[member].z;
  }
  g_object_falling[object] = 1;
  return true;
}

bool ComponentContainsSupportCycle(
    Voxel* voxels,
    int32_t width,
    int32_t height) {
  int32_t component_count = 0;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_component_objects[object] == 0) continue;
    g_component_indegree[object] = 0;
    ++component_count;
  }
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_component_objects[object] == 0) continue;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const Voxel& voxel = voxels[member];
      if (voxel.z == INT32_MIN) continue;
      const int32_t support = FindVoxelAt(
          voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
      if (support < 0) continue;
      const int32_t support_object = g_voxel_object[support];
      if (support_object >= 0 && support_object != object &&
          g_component_objects[support_object] != 0) {
        ++g_component_indegree[support_object];
      }
    }
  }

  int32_t queue_begin = 0;
  int32_t queue_end = 0;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_component_objects[object] != 0 && g_component_indegree[object] == 0) {
      g_object_queue[queue_end++] = object;
    }
  }
  int32_t removed = 0;
  while (queue_begin < queue_end) {
    const int32_t object = g_object_queue[queue_begin++];
    ++removed;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const Voxel& voxel = voxels[member];
      if (voxel.z == INT32_MIN) continue;
      const int32_t support = FindVoxelAt(
          voxels, voxel.x, voxel.y, voxel.z - 1, width, height);
      if (support < 0) continue;
      const int32_t support_object = g_voxel_object[support];
      if (support_object >= 0 && support_object != object &&
          g_component_objects[support_object] != 0 &&
          --g_component_indegree[support_object] == 0) {
        g_object_queue[queue_end++] = support_object;
      }
    }
  }
  return removed != component_count;
}

int32_t BuildVerticalComponent(
    Voxel* voxels,
    int32_t first_object,
    int32_t width,
    int32_t height) {
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_component_objects[object] = 0;
  }
  int32_t queue_begin = 0;
  int32_t queue_end = 0;
  g_component_objects[first_object] = 1;
  g_object_queue[queue_end++] = first_object;
  while (queue_begin < queue_end) {
    const int32_t object = g_object_queue[queue_begin++];
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const Voxel& voxel = voxels[member];
      for (int32_t dz = -1; dz <= 1; dz += 2) {
        if ((dz < 0 && voxel.z == INT32_MIN) ||
            (dz > 0 && voxel.z == INT32_MAX)) {
          continue;
        }
        const int32_t neighbor = FindVoxelAt(
            voxels, voxel.x, voxel.y, voxel.z + dz, width, height);
        if (neighbor < 0) continue;
        const int32_t other = g_voxel_object[neighbor];
        if (other >= 0 && other != object &&
            g_object_kind[other] == kWeightlessObject &&
            g_component_objects[other] == 0) {
          g_component_objects[other] = 1;
          g_object_queue[queue_end++] = other;
        }
      }
    }
  }
  return queue_end;
}

bool SettleSupportComponent(
    Voxel* voxels,
    int32_t count,
    int32_t first_object,
    int32_t width,
    int32_t height) {
  if (BuildVerticalComponent(voxels, first_object, width, height) <= 1 ||
      !ComponentContainsSupportCycle(voxels, width, height)) {
    return false;
  }

  int64_t drop = INT64_MAX;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_component_objects[object] == 0) continue;
    g_object_gravity_armed[object] = 1;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      const Voxel& voxel = voxels[member];
      const int32_t support = FindNearestVoxelBelowComponent(
          voxels, count, voxel.x, voxel.y, voxel.z, width);
      if (support < 0) continue;
      const int64_t candidate =
          static_cast<int64_t>(voxel.z) - voxels[support].z - 1;
      if (candidate < drop) drop = candidate;
    }
  }

  if (drop == INT64_MAX) {
    int32_t highest = INT32_MIN;
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_component_objects[object] == 0) continue;
      const int32_t object_highest = ObjectHighestZ(voxels, object);
      if (object_highest > highest) highest = object_highest;
    }
    const bool disappears = highest < LowestOtherActiveZ(
        voxels, count, width, height, -1, -1, true, true);
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_component_objects[object] == 0) continue;
      g_fell_objects[object] = 1;
      g_object_falling[object] = disappears ? 0 : 1;
      for (int32_t member = g_object_head[object]; member >= 0;
           member = g_next_object_voxel[member]) {
        if (voxels[member].z != INT32_MIN) --voxels[member].z;
        if (disappears) voxels[member].x = -1;
      }
    }
    g_moving_objects_fell = true;
    return true;
  }
  if (drop <= 0) return false;
  g_moving_objects_fell = true;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_component_objects[object] == 0) continue;
    g_fell_objects[object] = 1;
    g_object_falling[object] = 1;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      --voxels[member].z;
    }
  }
  return true;
}

void SettleFlaggedObjects(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height) {
  // A support can itself fall. Revisit the collection until the lower-to-upper
  // cascade reaches a fixed point; each pass moves or removes at least one body.
  for (int32_t pass = 0; pass <= g_object_count; ++pass) {
    bool changed = false;
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_moving_objects[object] == 0 ||
          g_fell_objects[object] != 0 ||
          g_weightless_object_count <= 1 ||
          g_object_kind[object] != kWeightlessObject ||
          !ObjectIsActive(voxels, object, width, height)) {
        continue;
      }
      if (SettleSupportComponent(voxels, count, object, width, height)) {
        changed = true;
        BuildSpatialIndex(voxels, count, width, height);
      }
    }
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_moving_objects[object] == 0 || g_fell_objects[object] != 0 ||
          g_carried_objects[object] != 0) {
        continue;
      }
      if (SettleObject(voxels, count, object, width, height)) {
        changed = true;
        BuildSpatialIndex(voxels, count, width, height);
      }
    }
    if (!changed) return;
  }
}
