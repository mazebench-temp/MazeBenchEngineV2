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
