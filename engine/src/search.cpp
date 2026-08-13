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
constexpr uint32_t kIceRole = HashRoleLiteral("ice");
constexpr int32_t kHashCapacity = 131072;
constexpr int32_t kHashMask = kHashCapacity - 1;
constexpr int16_t kInactiveCoordinate = INT16_MIN;
constexpr int32_t kSearchGoalCapacity = 64;
constexpr int32_t kMacroCellCapacity = kSearchVoxelCapacity;
constexpr uint8_t kWalkOnlyDirection = 4;
constexpr uint8_t kNoSupport = 0;
constexpr uint8_t kFloorSupport = 1;
constexpr uint8_t kIceSupport = 2;
constexpr uint8_t kOtherSupport = 3;

struct SearchNode {
  int16_t coordinates[kSearchDynamicEntityCapacity][3];
  uint64_t collected_goals;
  uint32_t parent;
  uint32_t priority;
  uint16_t cost;
  uint16_t approach_cell;
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
  int32_t player_entity;
  int32_t goal_indices[kSearchGoalCapacity];
  int32_t goal_coordinates[kSearchGoalCapacity][3];
  int32_t goal_count;
  uint64_t candidate_collected_goals;
  int32_t heap_nodes[kSearchNodeCapacity];
  int32_t heap_positions[kSearchNodeCapacity];
  int32_t heap_size;
  uint8_t closed[kSearchNodeCapacity];
  int16_t macro_occupants[kMacroCellCapacity];
  uint8_t macro_support[kMacroCellCapacity];
  int16_t walk_distance[kMacroCellCapacity];
  int16_t walk_parent[kMacroCellCapacity];
  uint8_t walk_direction[kMacroCellCapacity];
  uint16_t walk_queue[kMacroCellCapacity];
  int32_t reconstruct_nodes[kSearchSolutionCapacity];
  uint8_t reconstruct_directions[kMacroCellCapacity];
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

bool IsPushable(uint32_t role) {
  return role == kPushableRole || role == kWeightlessPushableRole;
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

bool HeapLess(const SearchData* data, int32_t left, int32_t right) {
  const SearchNode& left_node = data->nodes[left];
  const SearchNode& right_node = data->nodes[right];
  return left_node.priority != right_node.priority
      ? left_node.priority < right_node.priority
      : left_node.cost < right_node.cost;
}

void HeapSwap(SearchData* data, int32_t left, int32_t right) {
  const int32_t temporary = data->heap_nodes[left];
  data->heap_nodes[left] = data->heap_nodes[right];
  data->heap_nodes[right] = temporary;
  data->heap_positions[data->heap_nodes[left]] = left;
  data->heap_positions[data->heap_nodes[right]] = right;
}

void HeapRaise(SearchData* data, int32_t position) {
  while (position > 0) {
    const int32_t parent = (position - 1) / 2;
    if (!HeapLess(
            data, data->heap_nodes[position], data->heap_nodes[parent])) break;
    HeapSwap(data, position, parent);
    position = parent;
  }
}

void HeapPush(SearchData* data, int32_t node) {
  const int32_t position = data->heap_size++;
  data->heap_nodes[position] = node;
  data->heap_positions[node] = position;
  HeapRaise(data, position);
}

int32_t HeapPop(SearchData* data) {
  const int32_t result = data->heap_nodes[0];
  --data->heap_size;
  data->heap_positions[result] = -1;
  if (data->heap_size <= 0) return result;
  data->heap_nodes[0] = data->heap_nodes[data->heap_size];
  data->heap_positions[data->heap_nodes[0]] = 0;
  int32_t position = 0;
  for (;;) {
    const int32_t left = position * 2 + 1;
    if (left >= data->heap_size) break;
    const int32_t right = left + 1;
    const int32_t child = right < data->heap_size && HeapLess(
        data, data->heap_nodes[right], data->heap_nodes[left]) ? right : left;
    if (!HeapLess(data, data->heap_nodes[child], data->heap_nodes[position])) break;
    HeapSwap(data, position, child);
    position = child;
  }
  return result;
}

void HeapDecrease(SearchData* data, int32_t node) {
  const int32_t position = data->heap_positions[node];
  if (position >= 0) HeapRaise(data, position);
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

bool CandidatePlayerIsActive(
    const SearchData* data,
    int32_t width,
    int32_t height) {
  if (data->player_entity < 0) return false;
  const int32_t x = DecodeCoordinate(data->candidate[data->player_entity][0]);
  const int32_t y = DecodeCoordinate(data->candidate[data->player_entity][1]);
  return x >= 0 && x < width && y >= 0 && y < height;
}

bool IsGoal(const SearchData* data) {
  if (data->goal_count <= 0) return false;
  const uint64_t all_goals = data->goal_count == kSearchGoalCapacity
      ? UINT64_MAX
      : (uint64_t{1} << data->goal_count) - 1;
  return data->candidate_collected_goals == all_goals;
}

bool NodeIsGoal(const SearchData* data, const SearchNode& node) {
  if (data->goal_count <= 0) return false;
  const uint64_t all_goals = data->goal_count == kSearchGoalCapacity
      ? UINT64_MAX
      : (uint64_t{1} << data->goal_count) - 1;
  return node.collected_goals == all_goals;
}

SearchResult InvalidResult() {
  SearchResult result{};
  result.status = SearchStatus::kInvalid;
  return result;
}

void ReconstructCommandSolution(
    const SearchData* data,
    int32_t node,
    SearchResult* result) {
  result->solution_length = data->nodes[node].cost;
  result->moves = result->solution_length;
  int32_t cursor = result->solution_length;
  while (cursor > 0) {
    result->solution[--cursor] = data->nodes[node].direction;
    node = static_cast<int32_t>(data->nodes[node].parent);
  }
}

uint64_t AllGoalMask(const SearchData* data) {
  return data->goal_count == kSearchGoalCapacity
      ? UINT64_MAX
      : (uint64_t{1} << data->goal_count) - 1;
}

bool SceneIsSettled(const SearchData* data, int32_t width, int32_t height) {
  for (int32_t entity = 0; entity < data->entity_count; ++entity) {
    bool supported = false;
    for (int32_t member = 0;
         member < data->dynamic_voxel_count && !supported; ++member) {
      if (data->voxel_entities[member] != entity) continue;
      const Voxel& voxel = data->scene[member];
      if (voxel.x < 0 || voxel.x >= width || voxel.y < 0 || voxel.y >= height) {
        continue;
      }
      for (int32_t other = 0; other < data->count; ++other) {
        if (data->scene[other].role == kGoalRole ||
            (other < data->dynamic_voxel_count &&
             data->voxel_entities[other] == entity)) {
          continue;
        }
        if (data->scene[other].x == voxel.x &&
            data->scene[other].y == voxel.y &&
            data->scene[other].z == voxel.z - 1) {
          supported = true;
          break;
        }
      }
    }
    if (!supported) return false;
  }
  return true;
}

void BuildMacroSurface(
    SearchData* data,
    int32_t width,
    int32_t height,
    int32_t player_z) {
  const int32_t cells = width * height;
  for (int32_t cell = 0; cell < cells; ++cell) {
    data->macro_occupants[cell] = -1;
    data->macro_support[cell] = 0;
  }
  for (int32_t index = 0; index < data->count; ++index) {
    const Voxel& voxel = data->scene[index];
    if (index == data->player_index || voxel.role == kGoalRole ||
        voxel.x < 0 || voxel.x >= width || voxel.y < 0 || voxel.y >= height) {
      continue;
    }
    const int32_t cell = voxel.y * width + voxel.x;
    if (voxel.z == player_z) data->macro_occupants[cell] = static_cast<int16_t>(index);
    if (voxel.z == player_z - 1) data->macro_support[cell] = 1;
  }
}

bool BuildFlatStaticIceSurface(
    SearchData* data,
    int32_t width,
    int32_t height,
    int32_t player_z) {
  const int64_t cell_count = static_cast<int64_t>(width) * height;
  if (data->entity_count != 1 || data->player_entity != 0 ||
      cell_count <= 0 || cell_count > kMacroCellCapacity) {
    return false;
  }
  const int32_t cells = static_cast<int32_t>(cell_count);
  for (int32_t cell = 0; cell < cells; ++cell) {
    data->macro_occupants[cell] = -1;
    data->macro_support[cell] = kNoSupport;
  }
  for (int32_t index = 0; index < data->count; ++index) {
    const Voxel& voxel = data->scene[index];
    if (index == data->player_index || voxel.role == kGoalRole) continue;
    if (voxel.x < 0 || voxel.x >= width || voxel.y < 0 || voxel.y >= height) {
      return false;
    }
    const int32_t cell = voxel.y * width + voxel.x;
    if (voxel.z == player_z) {
      if (data->macro_occupants[cell] >= 0) return false;
      data->macro_occupants[cell] = static_cast<int16_t>(index);
    } else if (voxel.z == player_z - 1) {
      if (data->macro_support[cell] != kNoSupport) return false;
      data->macro_support[cell] = voxel.role == kIceRole
          ? kIceSupport
          : voxel.role == HashRoleLiteral("floor")
              ? kFloorSupport
              : kOtherSupport;
    } else {
      // Falling onto lower geometry and raised terrain retain the general
      // discrete tick engine. This fast path is deliberately flat and exact.
      return false;
    }
  }
  const Voxel& player = data->scene[data->player_index];
  const int32_t player_cell = player.y * width + player.x;
  if (data->macro_support[player_cell] == kNoSupport ||
      data->macro_occupants[player_cell] >= 0) {
    return false;
  }
  for (int32_t goal = 0; goal < data->goal_count; ++goal) {
    const int32_t x = data->goal_coordinates[goal][0];
    const int32_t y = data->goal_coordinates[goal][1];
    if (x < 0 || x >= width || y < 0 || y >= height ||
        data->goal_coordinates[goal][2] != player_z ||
        data->macro_occupants[y * width + x] >= 0) {
      return false;
    }
  }
  return true;
}

bool AdvanceFlatStaticIce(
    SearchData* data,
    const SearchNode& parent,
    int32_t width,
    int32_t height,
    int32_t direction) {
  constexpr int32_t kDx[4] = {0, 1, 0, -1};
  constexpr int32_t kDy[4] = {-1, 0, 1, 0};
  const int32_t dx = kDx[direction];
  const int32_t dy = kDy[direction];
  int32_t x = DecodeCoordinate(parent.coordinates[data->player_entity][0]);
  int32_t y = DecodeCoordinate(parent.coordinates[data->player_entity][1]);
  const int32_t z = DecodeCoordinate(
      parent.coordinates[data->player_entity][2]);
  if (x < 0 || x >= width || y < 0 || y >= height) return false;
  const int32_t from_x = x;
  const int32_t from_y = y;
  uint64_t collected_goals = parent.collected_goals;

  auto can_enter = [&](int32_t next_x, int32_t next_y) {
    return next_x >= 0 && next_x < width && next_y >= 0 && next_y < height &&
        data->macro_occupants[next_y * width + next_x] < 0;
  };
  auto collect = [&] {
    for (int32_t goal = 0; goal < data->goal_count; ++goal) {
      if (data->goal_coordinates[goal][0] == x &&
          data->goal_coordinates[goal][1] == y &&
          data->goal_coordinates[goal][2] == z) {
        collected_goals |= uint64_t{1} << goal;
      }
    }
  };

  int32_t next_x = x + dx;
  int32_t next_y = y + dy;
  if (!can_enter(next_x, next_y)) return false;
  uint8_t support = data->macro_support[next_y * width + next_x];
  if (support == kNoSupport) return false;
  x = next_x;
  y = next_y;

  bool sliding = support == kIceSupport;
  if (!sliding) {
    next_x = x + dx;
    next_y = y + dy;
    sliding = can_enter(next_x, next_y) &&
        data->macro_support[next_y * width + next_x] == kIceSupport;
  }
  while (sliding) {
    next_x = x + dx;
    next_y = y + dy;
    if (!can_enter(next_x, next_y)) break;
    support = data->macro_support[next_y * width + next_x];
    if (support == kNoSupport) return false;
    x = next_x;
    y = next_y;
    if (support != kIceSupport) break;
  }
  collect();
  if (x == from_x && y == from_y && collected_goals == parent.collected_goals) {
    return false;
  }
  for (int32_t entity = 0; entity < data->entity_count; ++entity) {
    for (int32_t axis = 0; axis < 3; ++axis) {
      data->candidate[entity][axis] = parent.coordinates[entity][axis];
    }
  }
  EncodeCoordinate(x, &data->candidate[data->player_entity][0]);
  EncodeCoordinate(y, &data->candidate[data->player_entity][1]);
  data->candidate_collected_goals = collected_goals;
  return true;
}

int32_t BuildWalkReachability(
    SearchData* data,
    int32_t width,
    int32_t height,
    int32_t start_cell) {
  const int32_t cells = width * height;
  for (int32_t cell = 0; cell < cells; ++cell) {
    data->walk_distance[cell] = -1;
    data->walk_parent[cell] = -1;
  }
  data->walk_distance[start_cell] = 0;
  data->walk_queue[0] = static_cast<uint16_t>(start_cell);
  int32_t queue_size = 1;
  constexpr int32_t kDx[4] = {0, 1, 0, -1};
  constexpr int32_t kDy[4] = {-1, 0, 1, 0};
  for (int32_t head = 0; head < queue_size; ++head) {
    const int32_t cell = data->walk_queue[head];
    const int32_t x = cell % width;
    const int32_t y = cell / width;
    for (int32_t direction = 0; direction < 4; ++direction) {
      const int32_t next_x = x + kDx[direction];
      const int32_t next_y = y + kDy[direction];
      if (next_x < 0 || next_x >= width || next_y < 0 || next_y >= height) {
        continue;
      }
      const int32_t next = next_y * width + next_x;
      if (data->walk_distance[next] >= 0 ||
          data->macro_occupants[next] >= 0 || data->macro_support[next] == 0) {
        continue;
      }
      data->walk_distance[next] = static_cast<int16_t>(
          data->walk_distance[cell] + 1);
      data->walk_parent[next] = static_cast<int16_t>(cell);
      data->walk_direction[next] = static_cast<uint8_t>(direction);
      data->walk_queue[queue_size++] = static_cast<uint16_t>(next);
    }
  }
  return queue_size;
}

uint32_t MacroPriority(
    const SearchData* data,
    const int16_t coordinates[kSearchDynamicEntityCapacity][3],
    uint64_t collected_goals,
    uint16_t cost) {
  if (collected_goals == AllGoalMask(data)) return cost;
  const int32_t player_x = DecodeCoordinate(
      coordinates[data->player_entity][0]);
  const int32_t player_y = DecodeCoordinate(
      coordinates[data->player_entity][1]);
  const int32_t goal_x = data->goal_coordinates[0][0];
  const int32_t goal_y = data->goal_coordinates[0][1];
  const int32_t dx = player_x > goal_x ? player_x - goal_x : goal_x - player_x;
  const int32_t dy = player_y > goal_y ? player_y - goal_y : goal_y - player_y;
  return static_cast<uint32_t>(cost) + static_cast<uint32_t>(dx + dy);
}

bool DynamicObjectsChangedExceptPlayer(
    const SearchData* data,
    const SearchNode& parent) {
  for (int32_t entity = 0; entity < data->entity_count; ++entity) {
    if (entity == data->player_entity) continue;
    for (int32_t axis = 0; axis < 3; ++axis) {
      if (data->candidate[entity][axis] != parent.coordinates[entity][axis]) {
        return true;
      }
    }
  }
  return false;
}

bool AddMacroNode(
    SearchData* data,
    int32_t parent,
    uint16_t cost,
    uint16_t approach_cell,
    uint8_t direction,
    int32_t maximum_nodes,
    int32_t* node_count,
    bool* limit_reached,
    SearchResult* result) {
  ++result->generated;
  const uint64_t hash = HashState(
      data->candidate, data->entity_count, data->candidate_collected_goals);
  const int32_t existing = FindState(
      data, hash, data->candidate, data->candidate_collected_goals);
  if (existing >= 0) {
    ++result->transpositions;
    SearchNode& node = data->nodes[existing];
    if (cost < node.cost && data->closed[existing] == 0) {
      node.parent = static_cast<uint32_t>(parent);
      node.cost = cost;
      node.approach_cell = approach_cell;
      node.direction = direction;
      node.priority = MacroPriority(
          data, node.coordinates, node.collected_goals, cost);
      HeapDecrease(data, existing);
    }
    return false;
  }
  if (*node_count >= maximum_nodes) {
    *limit_reached = true;
    return false;
  }
  const int32_t index = (*node_count)++;
  SearchNode& child = data->nodes[index];
  StoreNodeCoordinates(
      &child,
      data->candidate,
      data->entity_count,
      data->candidate_collected_goals);
  child.parent = static_cast<uint32_t>(parent);
  child.cost = cost;
  child.approach_cell = approach_cell;
  child.direction = direction;
  child.priority = MacroPriority(
      data, child.coordinates, child.collected_goals, cost);
  data->closed[index] = 0;
  data->heap_positions[index] = -1;
  InsertState(data, hash, index);
  HeapPush(data, index);
  return true;
}

bool AppendWalkingPath(
    SearchData* data,
    int32_t target_cell,
    SearchResult* result) {
  if (target_cell < 0 || target_cell >= kMacroCellCapacity ||
      data->walk_distance[target_cell] < 0) return false;
  int32_t length = 0;
  int32_t cell = target_cell;
  while (data->walk_parent[cell] >= 0) {
    if (length >= kMacroCellCapacity) return false;
    data->reconstruct_directions[length++] = data->walk_direction[cell];
    cell = data->walk_parent[cell];
  }
  if (result->solution_length + length > kSearchSolutionCapacity) return false;
  while (length > 0) {
    result->solution[result->solution_length++] =
        data->reconstruct_directions[--length];
  }
  return true;
}

bool ReconstructMacroSolution(
    SearchData* data,
    int32_t node,
    int32_t width,
    int32_t height,
    SearchResult* result) {
  int32_t edge_count = 0;
  for (int32_t cursor = node; cursor != 0;
       cursor = static_cast<int32_t>(data->nodes[cursor].parent)) {
    if (edge_count >= kSearchSolutionCapacity) return false;
    data->reconstruct_nodes[edge_count++] = cursor;
  }
  result->solution_length = 0;
  while (edge_count > 0) {
    const int32_t child_index = data->reconstruct_nodes[--edge_count];
    const SearchNode& child = data->nodes[child_index];
    const SearchNode& parent = data->nodes[child.parent];
    LoadNode(data, parent);
    const Voxel& player = data->scene[data->player_index];
    BuildMacroSurface(data, width, height, player.z);
    BuildWalkReachability(data, width, height, player.y * width + player.x);
    if (!AppendWalkingPath(data, child.approach_cell, result)) return false;
    if (child.direction < 4) {
      if (result->solution_length >= kSearchSolutionCapacity) return false;
      result->solution[result->solution_length++] = child.direction;
    }
  }
  result->moves = result->solution_length;
  return result->moves == data->nodes[node].cost;
}

SearchResult SearchCommands(
    SearchData* data,
    PhysicsWorkspace* physics_workspace,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t maximum_nodes,
    int32_t node_count,
    bool flat_static_ice) {
  SearchResult result{};
  for (int32_t head = 0; head < node_count; ++head) {
    const SearchNode& parent = data->nodes[head];
    if (parent.cost >= kSearchSolutionCapacity) continue;
    ++result.expanded;
    for (int32_t direction = 0; direction < 4; ++direction) {
      if (flat_static_ice) {
        if (!AdvanceFlatStaticIce(data, parent, width, height, direction)) {
          continue;
        }
      } else {
        LoadNode(data, parent);
        const int32_t status = simulate_turn(
            physics_workspace,
            data->scene,
            count,
            width,
            height,
            direction);
        if (status != 0 || !CaptureCandidate(data) ||
            !CandidatePlayerIsActive(data, width, height)) continue;
      }
      if (IsGoal(data)) {
        result.status = SearchStatus::kSolved;
        ReconstructCommandSolution(data, head, &result);
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
      child.cost = static_cast<uint16_t>(parent.cost + 1);
      child.direction = static_cast<uint8_t>(direction);
      InsertState(data, hash, node_count);
      ++node_count;
    }
  }
  result.status = SearchStatus::kUnsolved;
  return result;
}

SearchResult SearchMacroMoves(
    SearchData* data,
    PhysicsWorkspace* physics_workspace,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t maximum_nodes,
    int32_t node_count) {
  SearchResult result{};
  data->heap_size = 0;
  for (int32_t node = 0; node < maximum_nodes; ++node) {
    data->heap_positions[node] = -1;
    data->closed[node] = 0;
  }
  data->nodes[0].priority = MacroPriority(
      data,
      data->nodes[0].coordinates,
      data->nodes[0].collected_goals,
      0);
  HeapPush(data, 0);
  bool limit_reached = false;
  constexpr int32_t kDx[4] = {0, 1, 0, -1};
  constexpr int32_t kDy[4] = {-1, 0, 1, 0};
  while (data->heap_size > 0) {
    const int32_t head = HeapPop(data);
    if (data->closed[head] != 0) continue;
    data->closed[head] = 1;
    const SearchNode& parent = data->nodes[head];
    if (NodeIsGoal(data, parent)) {
      result.status = SearchStatus::kSolved;
      if (!ReconstructMacroSolution(data, head, width, height, &result)) {
        return InvalidResult();
      }
      return result;
    }
    if (parent.cost >= kSearchSolutionCapacity) continue;
    ++result.expanded;
    LoadNode(data, parent);
    const Voxel player = data->scene[data->player_index];
    if (player.x < 0 || player.x >= width || player.y < 0 || player.y >= height) {
      continue;
    }
    BuildMacroSurface(data, width, height, player.z);
    const int32_t walk_count = BuildWalkReachability(
        data, width, height, player.y * width + player.x);

    const int32_t goal_x = data->goal_coordinates[0][0];
    const int32_t goal_y = data->goal_coordinates[0][1];
    const int32_t goal_z = data->goal_coordinates[0][2];
    if (goal_x >= 0 && goal_x < width && goal_y >= 0 && goal_y < height &&
        goal_z == player.z) {
      const int32_t goal_cell = goal_y * width + goal_x;
      const int32_t walk_cost = data->walk_distance[goal_cell];
      if (walk_cost > 0 &&
          parent.cost + walk_cost < kSearchSolutionCapacity) {
        for (int32_t entity = 0; entity < data->entity_count; ++entity) {
          for (int32_t axis = 0; axis < 3; ++axis) {
            data->candidate[entity][axis] = parent.coordinates[entity][axis];
          }
        }
        EncodeCoordinate(goal_x, &data->candidate[data->player_entity][0]);
        EncodeCoordinate(goal_y, &data->candidate[data->player_entity][1]);
        data->candidate_collected_goals = AllGoalMask(data);
        AddMacroNode(
            data,
            head,
            static_cast<uint16_t>(parent.cost + walk_cost),
            static_cast<uint16_t>(goal_cell),
            kWalkOnlyDirection,
            maximum_nodes,
            &node_count,
            &limit_reached,
            &result);
      }
    }

    for (int32_t walk = 0; walk < walk_count; ++walk) {
      const int32_t approach_cell = data->walk_queue[walk];
      const int32_t approach_x = approach_cell % width;
      const int32_t approach_y = approach_cell / width;
      for (int32_t direction = 0; direction < 4; ++direction) {
        const int32_t target_x = approach_x + kDx[direction];
        const int32_t target_y = approach_y + kDy[direction];
        if (target_x < 0 || target_x >= width ||
            target_y < 0 || target_y >= height) continue;
        const int32_t occupant =
            data->macro_occupants[target_y * width + target_x];
        if (occupant < 0 || !IsPushable(data->scene[occupant].role)) continue;
        const int32_t edge_cost = data->walk_distance[approach_cell] + 1;
        if (parent.cost + edge_cost >= kSearchSolutionCapacity) continue;
        LoadNode(data, parent);
        data->scene[data->player_index].x = approach_x;
        data->scene[data->player_index].y = approach_y;
        const int32_t status = simulate_turn(
            physics_workspace,
            data->scene,
            count,
            width,
            height,
            direction);
        if (status != 0 || !CaptureCandidate(data) ||
            !CandidatePlayerIsActive(data, width, height) ||
            !DynamicObjectsChangedExceptPlayer(data, parent)) continue;
        AddMacroNode(
            data,
            head,
            static_cast<uint16_t>(parent.cost + edge_cost),
            static_cast<uint16_t>(approach_cell),
            static_cast<uint8_t>(direction),
            maximum_nodes,
            &node_count,
            &limit_reached,
            &result);
      }
    }
  }
  result.status = limit_reached ? SearchStatus::kLimitHit : SearchStatus::kUnsolved;
  return result;
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
  data->player_entity = -1;
  data->goal_count = 0;
  bool has_ice = false;
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
      data->player_entity = entity;
    }
  }
  static_count = data->dynamic_voxel_count;
  for (int32_t source = 0; source < count; ++source) {
    if (IsDynamic(voxels[source].role)) continue;
    const int32_t target = static_count++;
    data->scene[target] = voxels[source];
    if (voxels[source].role == kIceRole) has_ice = true;
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
  if (!CaptureCandidate(data) ||
      !CandidatePlayerIsActive(data, width, height)) return InvalidResult();
  SearchNode& root = data->nodes[0];
  StoreNodeCoordinates(
      &root,
      data->candidate,
      data->entity_count,
      data->candidate_collected_goals);
  root.parent = 0;
  root.priority = 0;
  root.cost = 0;
  root.approach_cell = 0;
  root.direction = 0;
  StartHashGeneration(data);
  InsertState(data, HashState(
      root.coordinates, data->entity_count, root.collected_goals), 0);
  int32_t node_count = 1;

  if (IsGoal(data)) {
    result.status = SearchStatus::kSolved;
    return result;
  }
  const Voxel& root_player = data->scene[data->player_index];
  const bool player_starts_on_goal = data->goal_count == 1 &&
      root_player.x == data->goal_coordinates[0][0] &&
      root_player.y == data->goal_coordinates[0][1] &&
      root_player.z == data->goal_coordinates[0][2];
  const int64_t cell_count = static_cast<int64_t>(width) * height;
  const bool macro_compatible = !has_ice && data->goal_count == 1 &&
      cell_count <= kMacroCellCapacity &&
      !player_starts_on_goal && SceneIsSettled(data, width, height);
  const bool flat_static_ice = has_ice && !player_starts_on_goal &&
      BuildFlatStaticIceSurface(
          data, width, height, root_player.z) &&
      SceneIsSettled(data, width, height);
  return macro_compatible
      ? SearchMacroMoves(
            data,
            physics_workspace,
            count,
            width,
            height,
            maximum_nodes,
            node_count)
      : SearchCommands(
            data,
            physics_workspace,
            count,
            width,
            height,
            maximum_nodes,
            node_count,
            flat_static_ice);
}

}  // namespace voxelbench
