#include "voxelbench/search.hpp"

#include <limits.h>

namespace voxelbench {
namespace {

constexpr uint32_t HashRoleLiteral(const char* value, uint32_t hash = 2166136261u) {
  return *value == '\0'
      ? hash
      : HashRoleLiteral(
            value + 1,
            (hash ^ static_cast<uint8_t>(*value)) * 16777619u);
}

constexpr uint32_t kPlayerRole = HashRoleLiteral("player");
constexpr uint32_t kPushableRole = HashRoleLiteral("pushable");
constexpr uint32_t kWeightlessPushableRole =
    HashRoleLiteral("weightless-pushable");
constexpr uint32_t kGoalRole = HashRoleLiteral("goal");
constexpr int32_t kHashCapacity = 131072;
constexpr int32_t kHashMask = kHashCapacity - 1;
constexpr int16_t kInactiveCoordinate = INT16_MIN;
constexpr int32_t kSearchGoalCapacity = 64;

struct SearchNode {
  int16_t coordinates[kSearchDynamicEntityCapacity][3];
  uint64_t collected_goals;
  uint32_t parent;
  uint16_t depth;
  uint8_t direction;
  uint8_t reserved;
};

struct SearchData {
  SearchNode nodes[kSearchNodeCapacity];
  uint64_t hash_keys[kHashCapacity];
  int32_t hash_nodes[kHashCapacity];
  uint32_t hash_stamps[kHashCapacity];
  uint32_t hash_generation;
  Voxel scene[kSearchVoxelCapacity];
  int16_t candidate[kSearchDynamicEntityCapacity][3];
  int32_t base_offsets[kSearchVoxelCapacity][3];
  int16_t voxel_entities[kSearchVoxelCapacity];
  int32_t entity_anchors[kSearchDynamicEntityCapacity];
  uint32_t entity_roles[kSearchDynamicEntityCapacity];
  int32_t entity_generic_ids[kSearchDynamicEntityCapacity];
  int32_t count;
  int32_t dynamic_voxel_count;
  int32_t entity_count;
  int32_t player_index;
  int32_t goal_indices[kSearchGoalCapacity];
  int32_t goal_coordinates[kSearchGoalCapacity][3];
  int32_t goal_count;
  uint64_t candidate_collected_goals;
};

static_assert(sizeof(SearchData) <= kSearchWorkspaceBytes);

SearchData* Data(SearchWorkspace* workspace) {
  return reinterpret_cast<SearchData*>(workspace->storage);
}

uint64_t Mix64(uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27u;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31u);
}

bool IsDynamic(uint32_t role) {
  return role == kPlayerRole || role == kPushableRole ||
      role == kWeightlessPushableRole;
}

bool EncodeCoordinate(int32_t value, int16_t* output) {
  if (value == INT32_MIN) {
    *output = kInactiveCoordinate;
    return true;
  }
  if (value <= INT16_MIN || value > INT16_MAX) return false;
  *output = static_cast<int16_t>(value);
  return true;
}

int32_t DecodeCoordinate(int16_t value) {
  return value == kInactiveCoordinate ? INT32_MIN : value;
}

uint64_t HashState(
    const int16_t coordinates[kSearchDynamicEntityCapacity][3],
    int32_t entity_count,
    uint64_t collected_goals) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (int32_t entity = 0; entity < entity_count; ++entity) {
    for (int32_t axis = 0; axis < 3; ++axis) {
      hash ^= static_cast<uint16_t>(coordinates[entity][axis]);
      hash *= 0x100000001b3ULL;
    }
  }
  hash ^= collected_goals;
  hash *= 0x100000001b3ULL;
  return Mix64(hash);
}

bool CoordinatesEqual(
    const SearchNode& node,
    const int16_t coordinates[kSearchDynamicEntityCapacity][3],
    int32_t entity_count,
    uint64_t collected_goals) {
  if (node.collected_goals != collected_goals) return false;
  for (int32_t entity = 0; entity < entity_count; ++entity) {
    for (int32_t axis = 0; axis < 3; ++axis) {
      if (node.coordinates[entity][axis] != coordinates[entity][axis]) {
        return false;
      }
    }
  }
  return true;
}

void StartHashGeneration(SearchData* data) {
  ++data->hash_generation;
  if (data->hash_generation != 0) return;
  for (int32_t slot = 0; slot < kHashCapacity; ++slot) {
    data->hash_stamps[slot] = 0;
  }
  data->hash_generation = 1;
}

int32_t FindState(
    SearchData* data,
    uint64_t hash,
    const int16_t coordinates[kSearchDynamicEntityCapacity][3],
    uint64_t collected_goals) {
  int32_t slot = static_cast<int32_t>(hash) & kHashMask;
  for (;;) {
    if (data->hash_stamps[slot] != data->hash_generation) return -1;
    const int32_t node = data->hash_nodes[slot];
    if (data->hash_keys[slot] == hash &&
        CoordinatesEqual(
            data->nodes[node], coordinates, data->entity_count, collected_goals)) {
      return node;
    }
    slot = (slot + 1) & kHashMask;
  }
}

void InsertState(SearchData* data, uint64_t hash, int32_t node) {
  int32_t slot = static_cast<int32_t>(hash) & kHashMask;
  while (data->hash_stamps[slot] == data->hash_generation) {
    slot = (slot + 1) & kHashMask;
  }
  data->hash_stamps[slot] = data->hash_generation;
  data->hash_keys[slot] = hash;
  data->hash_nodes[slot] = node;
}

void StoreNodeCoordinates(
    SearchNode* node,
    const int16_t coordinates[kSearchDynamicEntityCapacity][3],
    int32_t entity_count,
    uint64_t collected_goals) {
  for (int32_t entity = 0; entity < entity_count; ++entity) {
    for (int32_t axis = 0; axis < 3; ++axis) {
      node->coordinates[entity][axis] = coordinates[entity][axis];
    }
  }
  node->collected_goals = collected_goals;
}

void LoadNode(SearchData* data, const SearchNode& node) {
  for (int32_t dynamic = 0; dynamic < data->dynamic_voxel_count; ++dynamic) {
    const int32_t entity = data->voxel_entities[dynamic];
    const int32_t anchor_x = DecodeCoordinate(node.coordinates[entity][0]);
    if (anchor_x < 0) {
      data->scene[dynamic].x = -1;
      continue;
    }
    data->scene[dynamic].x = anchor_x + data->base_offsets[dynamic][0];
    data->scene[dynamic].y =
        DecodeCoordinate(node.coordinates[entity][1]) +
        data->base_offsets[dynamic][1];
    data->scene[dynamic].z =
        DecodeCoordinate(node.coordinates[entity][2]) +
        data->base_offsets[dynamic][2];
  }
  for (int32_t goal = 0; goal < data->goal_count; ++goal) {
    Voxel& voxel = data->scene[data->goal_indices[goal]];
    voxel.x = (node.collected_goals & (uint64_t{1} << goal)) != 0
        ? -1
        : data->goal_coordinates[goal][0];
    voxel.y = data->goal_coordinates[goal][1];
    voxel.z = data->goal_coordinates[goal][2];
  }
}

bool CaptureCandidate(SearchData* data) {
  for (int32_t entity = 0; entity < data->entity_count; ++entity) {
    const Voxel& anchor = data->scene[data->entity_anchors[entity]];
    const int32_t x = anchor.x < 0 ? -1 : anchor.x;
    const int32_t y = anchor.x < 0 ? 0 : anchor.y;
    const int32_t z = anchor.x < 0 ? 0 : anchor.z;
    if (!EncodeCoordinate(x, &data->candidate[entity][0]) ||
        !EncodeCoordinate(y, &data->candidate[entity][1]) ||
        !EncodeCoordinate(z, &data->candidate[entity][2])) {
      return false;
    }
  }
  data->candidate_collected_goals = 0;
  for (int32_t goal = 0; goal < data->goal_count; ++goal) {
    if (data->scene[data->goal_indices[goal]].x < 0) {
      data->candidate_collected_goals |= uint64_t{1} << goal;
    }
  }
  return true;
}

bool IsGoal(const SearchData* data) {
  if (data->goal_count <= 0) return false;
  const uint64_t all_goals = data->goal_count == kSearchGoalCapacity
      ? UINT64_MAX
      : (uint64_t{1} << data->goal_count) - 1;
  return data->candidate_collected_goals == all_goals;
}

SearchResult InvalidResult() {
  SearchResult result{};
  result.status = SearchStatus::kInvalid;
  return result;
}

void ReconstructSolution(
    const SearchData* data,
    int32_t node,
    SearchResult* result) {
  result->solution_length = data->nodes[node].depth;
  result->moves = result->solution_length;
  int32_t cursor = result->solution_length;
  while (cursor > 0) {
    result->solution[--cursor] = data->nodes[node].direction;
    node = static_cast<int32_t>(data->nodes[node].parent);
  }
}

}  // namespace

SearchResult search_shortest(
    SearchWorkspace* search_workspace,
    PhysicsWorkspace* physics_workspace,
    const Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t maximum_nodes) {
  if (search_workspace == nullptr || physics_workspace == nullptr ||
      voxels == nullptr || count <= 0 || count > kSearchVoxelCapacity ||
      width <= 0 || height <= 0 || maximum_nodes <= 0) {
    return InvalidResult();
  }
  if (maximum_nodes > kSearchNodeCapacity) maximum_nodes = kSearchNodeCapacity;

  SearchData* data = Data(search_workspace);
  data->count = count;
  data->dynamic_voxel_count = 0;
  data->entity_count = 0;
  data->player_index = -1;
  data->goal_count = 0;
  int32_t static_count = 0;

  // Dynamic entities first enables the physics engine's compact hot paths.
  for (int32_t source = 0; source < count; ++source) {
    if (!IsDynamic(voxels[source].role)) continue;
    const int32_t target = data->dynamic_voxel_count++;
    data->scene[target] = voxels[source];
    int32_t entity = -1;
    if (voxels[source].role == kWeightlessPushableRole) {
      for (int32_t candidate = 0; candidate < data->entity_count; ++candidate) {
        if (data->entity_roles[candidate] == voxels[source].role &&
            data->entity_generic_ids[candidate] == voxels[source].generic_id) {
          entity = candidate;
          break;
        }
      }
    }
    if (entity < 0) {
      if (data->entity_count >= kSearchDynamicEntityCapacity) {
        return InvalidResult();
      }
      entity = data->entity_count++;
      data->entity_anchors[entity] = target;
      data->entity_roles[entity] = voxels[source].role;
      data->entity_generic_ids[entity] = voxels[source].generic_id;
    }
    data->voxel_entities[target] = static_cast<int16_t>(entity);
    const Voxel& anchor = data->scene[data->entity_anchors[entity]];
    data->base_offsets[target][0] = voxels[source].x - anchor.x;
    data->base_offsets[target][1] = voxels[source].y - anchor.y;
    data->base_offsets[target][2] = voxels[source].z - anchor.z;
    if (voxels[source].role == kPlayerRole) {
      if (data->player_index >= 0) return InvalidResult();
      data->player_index = target;
    }
  }
  static_count = data->dynamic_voxel_count;
  for (int32_t source = 0; source < count; ++source) {
    if (IsDynamic(voxels[source].role)) continue;
    const int32_t target = static_count++;
    data->scene[target] = voxels[source];
    if (voxels[source].role == kGoalRole) {
      if (data->goal_count >= kSearchGoalCapacity) return InvalidResult();
      const int32_t goal = data->goal_count++;
      data->goal_indices[goal] = target;
      data->goal_coordinates[goal][0] = voxels[source].x;
      data->goal_coordinates[goal][1] = voxels[source].y;
      data->goal_coordinates[goal][2] = voxels[source].z;
    }
  }
  if (data->player_index < 0 || data->goal_count <= 0 ||
      data->entity_count <= 0) {
    return InvalidResult();
  }

  SearchResult result{};
  if (!CaptureCandidate(data)) return InvalidResult();
  SearchNode& root = data->nodes[0];
  StoreNodeCoordinates(
      &root,
      data->candidate,
      data->entity_count,
      data->candidate_collected_goals);
  root.parent = 0;
  root.depth = 0;
  root.direction = 0;
  StartHashGeneration(data);
  InsertState(data, HashState(
      root.coordinates, data->entity_count, root.collected_goals), 0);
  int32_t node_count = 1;

  if (IsGoal(data)) {
    result.status = SearchStatus::kSolved;
    return result;
  }

  for (int32_t head = 0; head < node_count; ++head) {
    const SearchNode& parent = data->nodes[head];
    if (parent.depth >= kSearchSolutionCapacity) continue;
    ++result.expanded;
    for (int32_t direction = 0; direction < 4; ++direction) {
      LoadNode(data, parent);
      const int32_t status = simulate_turn(
          physics_workspace,
          data->scene,
          count,
          width,
          height,
          direction);
      if (status != 0 || !CaptureCandidate(data)) {
        continue;
      }
      if (IsGoal(data)) {
        result.status = SearchStatus::kSolved;
        ReconstructSolution(data, head, &result);
        result.solution[result.solution_length++] = direction;
        result.moves = result.solution_length;
        return result;
      }
      if (CoordinatesEqual(
              parent,
              data->candidate,
              data->entity_count,
              data->candidate_collected_goals)) continue;
      ++result.generated;
      const uint64_t hash = HashState(
          data->candidate,
          data->entity_count,
          data->candidate_collected_goals);
      if (FindState(
              data, hash, data->candidate, data->candidate_collected_goals) >= 0) {
        ++result.transpositions;
        continue;
      }
      if (node_count >= maximum_nodes) {
        result.status = SearchStatus::kLimitHit;
        return result;
      }
      SearchNode& child = data->nodes[node_count];
      StoreNodeCoordinates(
          &child,
          data->candidate,
          data->entity_count,
          data->candidate_collected_goals);
      child.parent = static_cast<uint32_t>(head);
      child.depth = static_cast<uint16_t>(parent.depth + 1);
      child.direction = static_cast<uint8_t>(direction);
      InsertState(data, hash, node_count);
      ++node_count;
    }
  }

  result.status = SearchStatus::kUnsolved;
  return result;
}

}  // namespace voxelbench
