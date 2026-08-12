#include "voxelbench/physics.hpp"

#include <limits.h>

namespace voxelbench {
namespace {

constexpr uint32_t HashRoleLiteral(const char* value, uint32_t hash = 2166136261u) {
  return *value == '\0'
      ? hash
      : HashRoleLiteral(value + 1, (hash ^ static_cast<uint8_t>(*value)) * 16777619u);
}

constexpr uint32_t kPlayerRole = HashRoleLiteral("player");
constexpr uint32_t kPushableRole = HashRoleLiteral("pushable");
constexpr uint32_t kWeightlessPushableRole = HashRoleLiteral("weightless-pushable");
constexpr uint32_t kIceRole = HashRoleLiteral("ice");

constexpr int32_t kHashCapacity = 131072;
constexpr int32_t kHashMask = kHashCapacity - 1;
constexpr int32_t kDenseColumnCapacity = 4096;
constexpr int32_t kLinearSpatialThreshold = 16;
constexpr uint8_t kNormalObject = 0;
constexpr uint8_t kWeightlessObject = 1;

// These fixed workspaces keep the freestanding WebAssembly ABI allocation-free.
// Stamps make rebuilding the spatial index O(voxel count), without clearing its
// 131k slots between the many unit translations in an Ice move.
uint64_t g_hash_keys[kHashCapacity];
int32_t g_hash_values[kHashCapacity];
uint32_t g_hash_stamps[kHashCapacity];
uint32_t g_hash_generation = 1;

int32_t g_voxel_object[kVoxelCapacity];
int32_t g_next_object_voxel[kVoxelCapacity];
int32_t g_object_head[kVoxelCapacity];
uint8_t g_object_kind[kVoxelCapacity];
uint8_t g_moving_objects[kVoxelCapacity];
uint8_t g_mandatory_objects[kVoxelCapacity];
uint8_t g_carried_objects[kVoxelCapacity];
uint8_t g_rejected_carriers[kVoxelCapacity];
uint8_t g_fell_objects[kVoxelCapacity];
int32_t g_carrier_parent[kVoxelCapacity];
uint8_t g_object_gravity_armed[kVoxelCapacity];
uint8_t g_component_objects[kVoxelCapacity];
int32_t g_component_indegree[kVoxelCapacity];
int32_t g_object_queue[kVoxelCapacity];
int32_t g_column_heads[kDenseColumnCapacity];
int32_t g_next_column_voxel[kVoxelCapacity];
int32_t g_object_count = 0;
int32_t g_weightless_object_count = 0;
bool g_dense_columns = false;
bool g_linear_spatial = false;
int32_t g_spatial_voxel_count = 0;
bool g_moving_objects_fell = false;
int32_t g_ignored_support_voxel = -1;

bool IsInsideRoom(int32_t x, int32_t y, int32_t width, int32_t height) {
  return x >= 0 && x < width && y >= 0 && y < height;
}

uint64_t Mix64(uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27u;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31u);
}

void ResetHash() {
  ++g_hash_generation;
  if (g_hash_generation != 0) return;
  for (int32_t index = 0; index < kHashCapacity; ++index) {
    g_hash_stamps[index] = 0;
  }
  g_hash_generation = 1;
}

int32_t HashFind(uint64_t key) {
  int32_t slot = static_cast<int32_t>(Mix64(key) & static_cast<uint64_t>(kHashMask));
  for (;;) {
    if (g_hash_stamps[slot] != g_hash_generation) return -1;
    if (g_hash_keys[slot] == key) return g_hash_values[slot];
    slot = (slot + 1) & kHashMask;
  }
}

void HashInsert(uint64_t key, int32_t value) {
  int32_t slot = static_cast<int32_t>(Mix64(key) & static_cast<uint64_t>(kHashMask));
  while (g_hash_stamps[slot] == g_hash_generation && g_hash_keys[slot] != key) {
    slot = (slot + 1) & kHashMask;
  }
  g_hash_stamps[slot] = g_hash_generation;
  g_hash_keys[slot] = key;
  g_hash_values[slot] = value;
}

uint64_t SpatialKey(int32_t x, int32_t y, int32_t z, int32_t width) {
  const uint64_t cell = static_cast<uint64_t>(static_cast<uint32_t>(y)) *
                            static_cast<uint64_t>(static_cast<uint32_t>(width)) +
                        static_cast<uint32_t>(x);
  return (static_cast<uint64_t>(static_cast<uint32_t>(z)) << 32u) |
         static_cast<uint32_t>(cell);
}

void BuildSpatialIndex(Voxel* voxels, int32_t count, int32_t width, int32_t height) {
  g_spatial_voxel_count = count;
  g_linear_spatial = count <= kLinearSpatialThreshold;
  if (g_linear_spatial) {
    g_dense_columns = false;
    return;
  }
  ResetHash();
  const int64_t cell_count = static_cast<int64_t>(width) * height;
  g_dense_columns = cell_count > 0 && cell_count <= kDenseColumnCapacity;
  if (g_dense_columns) {
    for (int32_t cell = 0; cell < static_cast<int32_t>(cell_count); ++cell) {
      g_column_heads[cell] = -1;
    }
  }
  for (int32_t index = 0; index < count; ++index) {
    g_next_column_voxel[index] = -1;
    const Voxel& voxel = voxels[index];
    if (!IsInsideRoom(voxel.x, voxel.y, width, height)) continue;
    HashInsert(SpatialKey(voxel.x, voxel.y, voxel.z, width), index);
    if (g_dense_columns) {
      const int32_t cell = voxel.y * width + voxel.x;
      g_next_column_voxel[index] = g_column_heads[cell];
      g_column_heads[cell] = index;
    }
  }
}

int32_t FindVoxelAt(
    Voxel* voxels,
    int32_t x,
    int32_t y,
    int32_t z,
    int32_t width,
    int32_t height) {
  if (!IsInsideRoom(x, y, width, height)) return -1;
  if (g_linear_spatial) {
    for (int32_t index = 0; index < g_spatial_voxel_count; ++index) {
      if (voxels[index].x == x && voxels[index].y == y &&
          voxels[index].z == z) {
        return index;
      }
    }
    return -1;
  }
  return HashFind(SpatialKey(x, y, z, width));
}

int32_t CreateObject(uint8_t kind) {
  const int32_t object = g_object_count++;
  g_object_head[object] = -1;
  g_object_kind[object] = kind;
  g_object_gravity_armed[object] = 0;
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
  if (drop == INT64_MAX) {
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      voxels[member].x = -1;
    }
    return true;
  }
  if (drop <= 0) return false;
  g_moving_objects_fell = true;
  g_fell_objects[object] = 1;
  g_object_gravity_armed[object] = 1;
  for (int32_t member = g_object_head[object]; member >= 0;
       member = g_next_object_voxel[member]) {
    voxels[member].z = static_cast<int32_t>(static_cast<int64_t>(voxels[member].z) - drop);
  }
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
    for (int32_t object = 0; object < g_object_count; ++object) {
      if (g_component_objects[object] == 0) continue;
      for (int32_t member = g_object_head[object]; member >= 0;
           member = g_next_object_voxel[member]) {
        voxels[member].x = -1;
      }
    }
    return true;
  }
  if (drop <= 0) return false;
  g_moving_objects_fell = true;
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_component_objects[object] == 0) continue;
    g_fell_objects[object] = 1;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      voxels[member].z =
          static_cast<int32_t>(static_cast<int64_t>(voxels[member].z) - drop);
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
      if (g_moving_objects[object] == 0 || g_carried_objects[object] != 0) continue;
      if (SettleObject(voxels, count, object, width, height)) {
        changed = true;
        BuildSpatialIndex(voxels, count, width, height);
      }
    }
    if (!changed) return;
  }
}

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

bool TranslateMovingObjects(
    Voxel* voxels,
    int32_t count,
    int32_t player_index,
    int32_t dx,
    int32_t dy,
    int32_t width,
    int32_t height,
    bool carry_from_player) {
  g_moving_objects_fell = false;
  for (int32_t object = 0; object < g_object_count; ++object) {
    g_fell_objects[object] = 0;
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
      if (carry_from_player &&
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
        carry_from_player,
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
          if (g_object_kind[object] == kWeightlessObject && other >= 0 &&
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
  if (!found_moving) return true;

  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_moving_objects[object] == 0) continue;
    for (int32_t member = g_object_head[object]; member >= 0;
         member = g_next_object_voxel[member]) {
      voxels[member].x += dx;
      voxels[member].y += dy;
    }
  }
  BuildSpatialIndex(voxels, count, width, height);
  SettleFlaggedObjects(voxels, count, width, height);

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
      BuildSpatialIndex(voxels, count, width, height);
      if (SettleObject(voxels, count, object, width, height)) {
        BuildSpatialIndex(voxels, count, width, height);
      }
      changed = true;
    }
  }
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_fell_objects[object] != 0) g_mandatory_objects[object] = 0;
  }
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

bool PushAndSlide(
    Voxel* voxels,
    int32_t count,
    int32_t player_index,
    int32_t first_object,
    int32_t dx,
    int32_t dy,
    int32_t width,
    int32_t height) {
  ClearMovingObjects();
  g_mandatory_objects[first_object] = 1;
  g_moving_objects[first_object] = 1;
  g_ignored_support_voxel = player_index;
  if (!TranslateMovingObjects(
          voxels, count, player_index, dx, dy, width, height, true)) {
    g_ignored_support_voxel = -1;
    return false;
  }

  while (MovingObjectsAreFullyOnIce(voxels, width, height)) {
    if (!TranslateMovingObjects(
            voxels, count, player_index, dx, dy, width, height, false)) {
      break;
    }
  }
  g_ignored_support_voxel = -1;
  return true;
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

}  // namespace

uint32_t hash_role(const uint8_t* bytes, int32_t length) {
  if (bytes == nullptr || length < 0) return 0;
  uint32_t hash = 2166136261u;
  for (int32_t index = 0; index < length; ++index) {
    hash = (hash ^ bytes[index]) * 16777619u;
  }
  return hash;
}

int32_t simulate_turn(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  const int64_t cell_count = static_cast<int64_t>(width) * height;
  if (voxels == nullptr || count < 0 || count > kVoxelCapacity || width <= 0 ||
      height <= 0 || cell_count > UINT32_MAX || direction < 0 || direction > 3) {
    return -1;
  }

  int32_t player_index = -1;
  for (int32_t index = 0; index < count; ++index) {
    if (voxels[index].role == kPlayerRole) {
      player_index = index;
      break;
    }
  }
  if (player_index < 0) return -2;

  BuildObjects(voxels, count);
  BuildSpatialIndex(voxels, count, width, height);

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

  constexpr int32_t kDx[] = {0, 1, 0, -1};
  constexpr int32_t kDy[] = {-1, 0, 1, 0};
  const int32_t dx = kDx[direction];
  const int32_t dy = kDy[direction];
  bool automatic_ice_motion = false;
  bool player_gravity_armed = FindNearestVoxelBelow(
                                  voxels,
                                  count,
                                  voxels[player_index].x,
                                  voxels[player_index].y,
                                  voxels[player_index].z,
                                  -1,
                                  width) >= 0;

  for (;;) {
    Voxel& player = voxels[player_index];
    const int32_t player_from_x = player.x;
    const int32_t player_from_y = player.y;
    const int32_t player_from_z = player.z;
    const bool started_on_ice = IsSupportedByIce(voxels, player, width, height);
    const int32_t target_x = player.x + dx;
    const int32_t target_y = player.y + dy;
    if (!IsInsideRoom(target_x, target_y, width, height)) return 0;

    const int32_t target_index = FindVoxelAt(
        voxels, target_x, target_y, player.z, width, height);
    bool pushed = false;
    bool rode_pushed_object = false;
    bool player_fell = false;
    int32_t rider_delta_x = 0;
    int32_t rider_delta_y = 0;
    int32_t rider_delta_z = 0;
    if (target_index >= 0) {
      const int32_t object = g_voxel_object[target_index];
      int32_t riding_object = -1;
      int32_t riding_member = -1;
      int32_t riding_from_x = 0;
      int32_t riding_from_y = 0;
      int32_t riding_from_z = 0;
      if (player.z != INT32_MIN) {
        const int32_t support = FindVoxelAt(
            voxels, player.x, player.y, player.z - 1, width, height);
        if (support >= 0 && g_voxel_object[support] >= 0) {
          riding_object = g_voxel_object[support];
          riding_member = g_object_head[riding_object];
          riding_from_x = voxels[riding_member].x;
          riding_from_y = voxels[riding_member].y;
          riding_from_z = voxels[riding_member].z;
        }
      }
      // Momentum from Ice stops behind a body. Only the deliberate first unit
      // of a command may initiate a push.
      if (automatic_ice_motion || object < 0 ||
          !PushAndSlide(
              voxels, count, player_index, object, dx, dy, width, height)) {
        return 0;
      }
      pushed = true;
      if (riding_object >= 0 && g_moving_objects[riding_object] != 0 &&
          ObjectIsActive(voxels, riding_object, width, height)) {
        rider_delta_x = voxels[riding_member].x - riding_from_x;
        rider_delta_y = voxels[riding_member].y - riding_from_y;
        rider_delta_z = voxels[riding_member].z - riding_from_z;
        rode_pushed_object = true;
      }
    } else if (player.z != INT32_MIN) {
      const int32_t direct_support = FindVoxelAt(
          voxels, target_x, target_y, player.z - 1, width, height);
      if (direct_support < 0) {
        const int32_t lower_support = FindNearestVoxelBelow(
            voxels, count, target_x, target_y, player.z, -1, width);
        if (lower_support >= 0 && !automatic_ice_motion && !started_on_ice) {
          // Walking cannot step down a ledge; Ice momentum can carry the
          // player off it and then gravity chooses the landing elevation.
          return 0;
        }
      }
    }

    if (!pushed) {
      // The player is itself a moving support. Objects resting on the player
      // use the same passenger rules as objects riding another moving body.
      ClearMovingObjects();
      if (!TranslateMovingObjects(
              voxels,
              count,
              player_index,
              dx,
              dy,
              width,
              height,
              true)) {
        return 0;
      }
    }

    if (rode_pushed_object) {
      player.x = player_from_x + rider_delta_x;
      player.y = player_from_y + rider_delta_y;
      player.z = player_from_z + rider_delta_z;
    } else {
      player.x = target_x;
      player.y = target_y;
    }
    BuildSpatialIndex(voxels, count, width, height);
    // A lone pushed body has already completed both its horizontal movement
    // and gravity resolution inside PushAndSlide. Multi-body states still use
    // the global settle pass because moving supports can affect bodies that
    // were not part of the final Ice substep.
    const bool pushed_only_object_already_settled =
        pushed && g_object_count == 1 && g_moving_objects[0] != 0;
    if (!pushed_only_object_already_settled) {
      SettleAllObjects(voxels, count, width, height);
    }

    if (player.z != INT32_MIN &&
        FindVoxelAt(voxels, player.x, player.y, player.z - 1, width, height) < 0) {
      const int32_t lower_support = FindNearestVoxelBelow(
          voxels, count, player.x, player.y, player.z, -1, width);
      if (lower_support < 0) {
        if (!player_gravity_armed) return 0;
        player.x = -1;
        BuildSpatialIndex(voxels, count, width, height);
        return 0;
      }
      if (!automatic_ice_motion && !started_on_ice && !pushed) {
        // This normally gets caught before committing the horizontal step.
        // Keep the guard here for support changes caused by settling bodies.
        player.x -= dx;
        player.y -= dy;
        BuildSpatialIndex(voxels, count, width, height);
        return 0;
      }
      player_gravity_armed = true;
      player.z = voxels[lower_support].z + 1;
      player_fell = true;
      BuildSpatialIndex(voxels, count, width, height);
      SettleAllObjects(voxels, count, width, height);
    }

    // A fall spends the current horizontal momentum. This also prevents a
    // lower Ice surface from extending motion that began on a higher one.
    if (player_fell) return 0;

    if (!IsSupportedByIce(voxels, player, width, height)) {
      if (!automatic_ice_motion && !pushed && player.z != INT32_MIN) {
        const int32_t run_up_x = player.x + dx;
        const int32_t run_up_y = player.y + dy;
        if (IsInsideRoom(run_up_x, run_up_y, width, height) &&
            FindVoxelAt(voxels, run_up_x, run_up_y, player.z, width, height) < 0) {
          const int32_t run_up_support = FindVoxelAt(
              voxels, run_up_x, run_up_y, player.z - 1, width, height);
          if (run_up_support >= 0 && voxels[run_up_support].role == kIceRole) {
            automatic_ice_motion = true;
            continue;
          }
        }
      }
      return 0;
    }
    automatic_ice_motion = true;
  }
}

}  // namespace voxelbench
