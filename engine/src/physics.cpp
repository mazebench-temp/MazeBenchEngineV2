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
constexpr uint32_t kFloorRole = HashRoleLiteral("floor");

constexpr int32_t kHashCapacity = 131072;
constexpr int32_t kHashMask = kHashCapacity - 1;
constexpr int32_t kDenseColumnCapacity = 4096;
constexpr int32_t kLinearSpatialThreshold = 16;
constexpr uint8_t kNormalObject = 0;
constexpr uint8_t kWeightlessObject = 1;

// These fixed workspaces keep the freestanding WebAssembly ABI allocation-free.
// Stamps make rebuilding the spatial index O(voxel count), without clearing its
// 131k slots between the many unit translations in an Ice move.
struct WorkspaceData {
  uint64_t hash_keys[kHashCapacity];
  int32_t hash_values[kHashCapacity];
  uint32_t hash_stamps[kHashCapacity];
  uint32_t hash_generation;
  int32_t voxel_object[kVoxelCapacity];
  int32_t next_object_voxel[kVoxelCapacity];
  int32_t object_head[kVoxelCapacity];
  uint8_t object_kind[kVoxelCapacity];
  uint8_t moving_objects[kVoxelCapacity];
  uint8_t mandatory_objects[kVoxelCapacity];
  uint8_t carried_objects[kVoxelCapacity];
  uint8_t rejected_carriers[kVoxelCapacity];
  uint8_t fell_objects[kVoxelCapacity];
  uint8_t object_falling[kVoxelCapacity];
  uint8_t pending_fall[kVoxelCapacity];
  int32_t carrier_parent[kVoxelCapacity];
  uint8_t object_gravity_armed[kVoxelCapacity];
  uint8_t component_objects[kVoxelCapacity];
  int32_t component_indegree[kVoxelCapacity];
  int32_t object_queue[kVoxelCapacity];
  int32_t column_heads[kDenseColumnCapacity];
  int32_t next_column_voxel[kVoxelCapacity];
  int32_t object_count;
  int32_t weightless_object_count;
  bool dense_columns;
  bool linear_spatial;
  int32_t spatial_voxel_count;
  bool moving_objects_fell;
  int32_t ignored_support_voxel;
  MotionState* continuation_state;
  Voxel* continuation_voxels;
  int32_t continuation_tick;
};

static_assert(sizeof(WorkspaceData) <= kPhysicsWorkspaceBytes);

#if defined(__wasm__)
WorkspaceData* g_active_workspace = nullptr;
#else
thread_local WorkspaceData* g_active_workspace = nullptr;
#endif

WorkspaceData& ActiveWorkspace() {
  return *g_active_workspace;
}

#define g_hash_keys ActiveWorkspace().hash_keys
#define g_hash_values ActiveWorkspace().hash_values
#define g_hash_stamps ActiveWorkspace().hash_stamps
#define g_hash_generation ActiveWorkspace().hash_generation
#define g_voxel_object ActiveWorkspace().voxel_object
#define g_next_object_voxel ActiveWorkspace().next_object_voxel
#define g_object_head ActiveWorkspace().object_head
#define g_object_kind ActiveWorkspace().object_kind
#define g_moving_objects ActiveWorkspace().moving_objects
#define g_mandatory_objects ActiveWorkspace().mandatory_objects
#define g_carried_objects ActiveWorkspace().carried_objects
#define g_rejected_carriers ActiveWorkspace().rejected_carriers
#define g_fell_objects ActiveWorkspace().fell_objects
#define g_object_falling ActiveWorkspace().object_falling
#define g_pending_fall ActiveWorkspace().pending_fall
#define g_carrier_parent ActiveWorkspace().carrier_parent
#define g_object_gravity_armed ActiveWorkspace().object_gravity_armed
#define g_component_objects ActiveWorkspace().component_objects
#define g_component_indegree ActiveWorkspace().component_indegree
#define g_object_queue ActiveWorkspace().object_queue
#define g_column_heads ActiveWorkspace().column_heads
#define g_next_column_voxel ActiveWorkspace().next_column_voxel
#define g_object_count ActiveWorkspace().object_count
#define g_weightless_object_count ActiveWorkspace().weightless_object_count
#define g_dense_columns ActiveWorkspace().dense_columns
#define g_linear_spatial ActiveWorkspace().linear_spatial
#define g_spatial_voxel_count ActiveWorkspace().spatial_voxel_count
#define g_moving_objects_fell ActiveWorkspace().moving_objects_fell
#define g_ignored_support_voxel ActiveWorkspace().ignored_support_voxel

WorkspaceData* WorkspaceStorage(PhysicsWorkspace* workspace) {
  return reinterpret_cast<WorkspaceData*>(workspace->storage);
}

struct WorkspaceScope {
  explicit WorkspaceScope(PhysicsWorkspace* workspace)
      : previous(g_active_workspace) {
    g_active_workspace = WorkspaceStorage(workspace);
  }
  ~WorkspaceScope() { g_active_workspace = previous; }
  WorkspaceData* previous;
};

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
    return false;
  }

  while (MovingObjectsAreFullyOnIce(voxels, width, height)) {
    if (!TranslateMovingObjects(
            voxels,
            count,
            player_index,
            dx,
            dy,
            width,
            height,
            false,
            false,
            false)) {
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

bool ObjectsNeedGravityTick(
    Voxel* voxels,
    int32_t width,
    int32_t height) {
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
  }
  return needs_tick;
}

bool ObjectsAdvancedThisTick() {
  for (int32_t object = 0; object < g_object_count; ++object) {
    if (g_fell_objects[object] != 0) return true;
  }
  return false;
}

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

}  // namespace

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
    int32_t player_index = -1;
    for (int32_t index = 0; index < count; ++index) {
      if (voxels[index].role == kPlayerRole) {
        player_index = index;
        break;
      }
    }
    if (player_index < 0) return TickResult::kNoPlayer;
    BuildObjects(voxels, count);
    BuildSpatialIndex(voxels, count, width, height);
    InitializeMotion(
        state, voxels, count, width, height, player_index, direction);
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

  auto store_and_finish = [&](int32_t phase, bool advanced) {
    state->phase = phase;
    if (advanced) ++state->tick;
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
        ObjectsNeedGravityTick(voxels, width, height);
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
        objects_advanced || player_advanced);
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
          ObjectsNeedGravityTick(voxels, width, height);
      if (gravity_continues) {
        state->reserved[0] = kResumeNothing;
      }
      return store_and_finish(
          gravity_continues ? kMotionGravity : kMotionComplete,
          ObjectsAdvancedThisTick());
    }

    const bool player_rode_object =
        MovePlayerWithRider(voxels, player_index, rider, width, height);
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
      }
    }
    BuildSpatialIndex(voxels, count, width, height);
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
        ObjectsNeedGravityTick(voxels, width, height);
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
        ObjectsNeedGravityTick(voxels, width, height);
    state->reserved[0] =
        pushed && IsSupportedByIce(voxels, player, width, height)
        ? kResumePlayerIce
        : kResumeNothing;
    return store_and_finish(
        gravity_continues ? kMotionGravity : kMotionComplete,
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
  const bool gravity_continues = ObjectsNeedGravityTick(voxels, width, height);
  state->reserved[0] = gravity_continues &&
      IsSupportedByIce(voxels, player, width, height)
      ? kResumePlayerIce
      : kResumeNothing;
  return store_and_finish(
      gravity_continues ? kMotionGravity : kMotionComplete,
      true);
}

int32_t simulate_command(
    PhysicsWorkspace* workspace,
    MotionState* state,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction,
    TickObserver observer,
    void* context) {
  if (state == nullptr) return -1;
  reset_motion_state(state);
  constexpr int32_t kMaximumTicks = 1000000;
  for (int32_t iteration = 0; iteration < kMaximumTicks; ++iteration) {
    const int32_t before_tick = state->tick;
    const TickResult result = step_tick(
        workspace, state, voxels, count, width, height, direction);
    if (result == TickResult::kInvalid) return -1;
    if (result == TickResult::kNoPlayer) return -2;
    if (state->tick != before_tick && observer != nullptr) {
      observer(voxels, count, state, context);
    }
    if (result == TickResult::kComplete) return 0;
  }
  return -1;
}

int32_t simulate_turn_legacy(
    PhysicsWorkspace* workspace,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  if (workspace == nullptr) return -1;
  WorkspaceScope workspace_scope(workspace);
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
              true,
              true,
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

int32_t simulate_turn(
    PhysicsWorkspace* workspace,
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  MotionState state;
  return simulate_command(
      workspace, &state, voxels, count, width, height, direction);
}

int32_t simulate_turn(
    Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  static thread_local PhysicsWorkspace workspace;
  static thread_local bool initialized = false;
  if (!initialized) {
    reset_workspace(&workspace);
    initialized = true;
  }
  return simulate_turn(&workspace, voxels, count, width, height, direction);
}

}  // namespace voxelbench
