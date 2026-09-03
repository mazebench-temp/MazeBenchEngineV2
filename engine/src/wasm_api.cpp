#include "voxelbench/physics.hpp"
#include "voxelbench/search.hpp"

namespace {

constexpr uint32_t HashRoleLiteral(const char* value) {
  uint32_t hash = 2166136261u;
  for (int32_t index = 0; value[index] != '\0'; ++index) {
    hash = (hash ^ static_cast<uint8_t>(value[index])) * 16777619u;
  }
  return hash;
}

constexpr uint32_t kRandomPlayerRole = HashRoleLiteral("player");
constexpr uint32_t kRandomGoalRole = HashRoleLiteral("goal");
constexpr int32_t kRandomTrailCapacity = 50;
constexpr int32_t kRandomVisitedWords = 8;

voxelbench::Voxel g_voxels[voxelbench::kVoxelCapacity];
voxelbench::Voxel g_random_undo_dynamic[voxelbench::kVoxelCapacity];
voxelbench::Voxel g_random_undo_goals[voxelbench::kVoxelCapacity];
voxelbench::PhysicsWorkspace g_workspace;
voxelbench::MotionState g_motion_state;
voxelbench::SearchWorkspace g_search_workspace;
voxelbench::SearchResult g_search_result;
voxelbench::EdgeSearchResult g_edge_search_result;
uint8_t g_role_buffer[voxelbench::kRoleBufferCapacity];
bool g_initialized = false;
int32_t g_random_count = 0;
int32_t g_random_width = 0;
int32_t g_random_height = 0;
int32_t g_random_dynamic_count = 0;
int32_t g_random_player_index = -1;
int32_t g_random_goal_count = 0;
int32_t g_random_goal_indices[voxelbench::kVoxelCapacity];
uint32_t g_random_seed = 1;
uint32_t g_random_visited[kRandomVisitedWords];
int32_t g_random_trail[kRandomTrailCapacity];
int32_t g_random_trail_count = 0;
int32_t g_random_trail_next = 0;
int32_t g_random_actions = 0;
int32_t g_random_undos = 0;
int32_t g_random_exit_direction = -1;

void EnsureInitialized() {
  if (g_initialized) return;
  voxelbench::reset_workspace(&g_workspace);
  voxelbench::reset_motion_state(&g_motion_state);
  g_initialized = true;
}

bool RandomObjectIsActive(const voxelbench::Voxel& voxel) {
  return voxel.x >= 0 && voxel.y >= 0 && voxel.x < g_random_width &&
      voxel.y < g_random_height && voxel.z != INT32_MIN;
}

void SaveRandomUndo() {
  for (int32_t index = 0; index < g_random_dynamic_count; ++index) {
    g_random_undo_dynamic[index] = g_voxels[index];
  }
  for (int32_t goal = 0; goal < g_random_goal_count; ++goal) {
    g_random_undo_goals[goal] = g_voxels[g_random_goal_indices[goal]];
  }
}

void RestoreRandomUndo() {
  for (int32_t index = 0; index < g_random_dynamic_count; ++index) {
    g_voxels[index] = g_random_undo_dynamic[index];
  }
  for (int32_t goal = 0; goal < g_random_goal_count; ++goal) {
    g_voxels[g_random_goal_indices[goal]] = g_random_undo_goals[goal];
  }
}

uint32_t NextRandomValue() {
  uint32_t value = g_random_seed;
  value ^= value << 13u;
  value ^= value >> 17u;
  value ^= value << 5u;
  g_random_seed = value == 0 ? 0x9e3779b9u : value;
  return g_random_seed;
}

void RecordRandomVisit() {
  const voxelbench::Voxel& player = g_voxels[g_random_player_index];
  if (!RandomObjectIsActive(player)) return;
  const int32_t cell = player.y * g_random_width + player.x;
  if (cell >= 0 && cell < 256) {
    g_random_visited[cell / 32] |= uint32_t{1} << (cell % 32);
  }
  g_random_trail[g_random_trail_next] = cell;
  g_random_trail_next = (g_random_trail_next + 1) % kRandomTrailCapacity;
  if (g_random_trail_count < kRandomTrailCapacity) ++g_random_trail_count;
}

bool RandomActionMayExit(
    const voxelbench::Voxel& before,
    const voxelbench::Voxel& after,
    int32_t direction) {
  if (!RandomObjectIsActive(after)) return false;
  const int32_t dx = after.x - before.x;
  const int32_t dy = after.y - before.y;
  if (after.y == 0 && ((dx == 0 && dy < 0) ||
      (dx == 0 && dy == 0 && direction == 0))) return true;
  if (after.x == g_random_width - 1 && ((dx > 0 && dy == 0) ||
      (dx == 0 && dy == 0 && direction == 1))) return true;
  if (after.y == g_random_height - 1 && ((dx == 0 && dy > 0) ||
      (dx == 0 && dy == 0 && direction == 2))) return true;
  if (after.x == 0 && ((dx < 0 && dy == 0) ||
      (dx == 0 && dy == 0 && direction == 3))) return true;
  return false;
}

}  // namespace

extern "C" {

int32_t physics_abi_version() {
  return voxelbench::kPhysicsAbiVersion;
}

int32_t voxel_stride() {
  return static_cast<int32_t>(sizeof(voxelbench::Voxel) / sizeof(int32_t));
}

int32_t voxel_capacity() {
  return voxelbench::kVoxelCapacity;
}

voxelbench::Voxel* voxel_buffer() {
  return g_voxels;
}

int32_t role_buffer_capacity() {
  return voxelbench::kRoleBufferCapacity;
}

uint8_t* role_buffer() {
  return g_role_buffer;
}

uint8_t* motion_state_buffer() {
  EnsureInitialized();
  return reinterpret_cast<uint8_t*>(&g_motion_state);
}

int32_t motion_state_size() {
  return static_cast<int32_t>(sizeof(g_motion_state));
}

void reset_command() {
  EnsureInitialized();
  voxelbench::reset_motion_state(&g_motion_state);
}

int32_t step_command_tick(
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  EnsureInitialized();
  return static_cast<int32_t>(voxelbench::step_command_tick(
      &g_workspace,
      &g_motion_state,
      g_voxels,
      count,
      width,
      height,
      direction));
}

int32_t command_tick() {
  EnsureInitialized();
  return g_motion_state.tick;
}

int32_t command_cycle_detected() {
  EnsureInitialized();
  return g_motion_state.cycle_start_tick >= 0 ? 1 : 0;
}

int32_t command_cycle_start_tick() {
  EnsureInitialized();
  return g_motion_state.cycle_start_tick;
}

int32_t command_cycle_repeat_tick() {
  EnsureInitialized();
  return g_motion_state.cycle_repeat_tick;
}

uint32_t role_code(int32_t length) {
  if (length < 0 || length > voxelbench::kRoleBufferCapacity) return 0;
  return voxelbench::hash_role(g_role_buffer, length);
}

int32_t simulate_turn(
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  EnsureInitialized();
  return voxelbench::simulate_turn(
      &g_workspace, g_voxels, count, width, height, direction);
}

int32_t search_prepare_scene(
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t dynamic_voxel_count) {
  EnsureInitialized();
  return voxelbench::prepare_scene(
      &g_workspace,
      g_voxels,
      count,
      width,
      height,
      dynamic_voxel_count) ? 1 : 0;
}

int32_t search_prepare_quiescent_snapshot(
    int32_t count,
    int32_t width,
    int32_t height) {
  EnsureInitialized();
  return voxelbench::prepare_quiescent_snapshot(
      &g_workspace, g_voxels, count, width, height) ? 1 : 0;
}

int32_t search_try_passive_quiescent_turn(
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t direction) {
  EnsureInitialized();
  return voxelbench::try_simulate_passive_quiescent_turn(
      &g_workspace, g_voxels, count, width, height, direction);
}

int32_t random_agent_begin(
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t dynamic_voxel_count,
    uint32_t seed) {
  EnsureInitialized();
  if (count < 1 || count > voxelbench::kVoxelCapacity || width < 1 ||
      height < 1 || static_cast<int64_t>(width) * height > 256 ||
      dynamic_voxel_count < 1 || dynamic_voxel_count > count) {
    return 0;
  }
  g_random_count = count;
  g_random_width = width;
  g_random_height = height;
  g_random_dynamic_count = dynamic_voxel_count;
  g_random_player_index = -1;
  g_random_goal_count = 0;
  for (int32_t index = 0; index < count; ++index) {
    if (g_voxels[index].role == kRandomPlayerRole &&
        g_random_player_index < 0) {
      g_random_player_index = index;
    }
    if (g_voxels[index].role == kRandomGoalRole) {
      g_random_goal_indices[g_random_goal_count++] = index;
    }
  }
  if (g_random_player_index < 0 || g_random_player_index >= dynamic_voxel_count ||
      !voxelbench::prepare_scene(
          &g_workspace, g_voxels, count, width, height, dynamic_voxel_count) ||
      !voxelbench::prepare_quiescent_snapshot(
          &g_workspace, g_voxels, count, width, height)) {
    return 0;
  }
  g_random_seed = seed == 0 ? 0x9e3779b9u : seed;
  return 1;
}

int32_t random_agent_run(int32_t maximum_actions) {
  EnsureInitialized();
  if (maximum_actions < 0 || g_random_player_index < 0) return -1;
  for (int32_t word = 0; word < kRandomVisitedWords; ++word) {
    g_random_visited[word] = 0;
  }
  g_random_trail_count = 0;
  g_random_trail_next = 0;
  g_random_actions = 0;
  g_random_undos = 0;
  g_random_exit_direction = -1;
  RecordRandomVisit();

  for (int32_t action = 0; action < maximum_actions; ++action) {
    SaveRandomUndo();
    const voxelbench::Voxel before = g_voxels[g_random_player_index];
    const int32_t direction = static_cast<int32_t>(NextRandomValue() & 3u);
    int32_t result = voxelbench::try_simulate_passive_quiescent_turn(
        &g_workspace,
        g_voxels,
        g_random_count,
        g_random_width,
        g_random_height,
        direction);
    if (result == 0) {
      result = voxelbench::simulate_quiescent_turn(
          &g_workspace,
          g_voxels,
          g_random_count,
          g_random_width,
          g_random_height,
          direction,
          true);
      if (result == 0 && !voxelbench::prepare_quiescent_snapshot(
          &g_workspace,
          g_voxels,
          g_random_count,
          g_random_width,
          g_random_height)) {
        return -1;
      }
    }
    if (result < 0) return result;

    const voxelbench::Voxel& player = g_voxels[g_random_player_index];
    if (!RandomObjectIsActive(player)) {
      RestoreRandomUndo();
      ++g_random_actions;
      ++g_random_undos;
      if (!voxelbench::prepare_quiescent_snapshot(
          &g_workspace,
          g_voxels,
          g_random_count,
          g_random_width,
          g_random_height)) {
        return -1;
      }
      RecordRandomVisit();
      continue;
    }
    if (RandomActionMayExit(before, player, direction)) {
      RestoreRandomUndo();
      g_random_exit_direction = direction;
      if (!voxelbench::prepare_quiescent_snapshot(
          &g_workspace,
          g_voxels,
          g_random_count,
          g_random_width,
          g_random_height)) {
        return -1;
      }
      return 1;
    }
    ++g_random_actions;
    RecordRandomVisit();
  }
  return 0;
}

int32_t random_agent_actions() {
  return g_random_actions;
}

int32_t random_agent_death_undos() {
  return g_random_undos;
}

int32_t random_agent_exit_direction() {
  return g_random_exit_direction;
}

uint32_t random_agent_seed() {
  return g_random_seed;
}

uint32_t random_agent_visited_word(int32_t index) {
  if (index < 0 || index >= kRandomVisitedWords) return 0;
  return g_random_visited[index];
}

int32_t random_agent_trail_count() {
  return g_random_trail_count;
}

int32_t random_agent_trail_cell(int32_t index) {
  if (index < 0 || index >= g_random_trail_count) return -1;
  const int32_t start = g_random_trail_count < kRandomTrailCapacity
      ? 0
      : g_random_trail_next;
  return g_random_trail[(start + index) % kRandomTrailCapacity];
}

int32_t search_node_capacity() {
  return voxelbench::kSearchNodeCapacity;
}

int32_t search_voxel_capacity() {
  return voxelbench::kSearchVoxelCapacity;
}

int32_t search_solve(
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t maximum_nodes) {
  EnsureInitialized();
  g_search_result = voxelbench::search_shortest(
      &g_search_workspace,
      &g_workspace,
      g_voxels,
      count,
      width,
      height,
      maximum_nodes);
  return static_cast<int32_t>(g_search_result.status);
}

int32_t search_edges(
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t maximum_nodes) {
  EnsureInitialized();
  g_edge_search_result = voxelbench::search_reachable_edges(
      &g_search_workspace,
      &g_workspace,
      g_voxels,
      count,
      width,
      height,
      maximum_nodes);
  g_search_result = {};
  g_search_result.status = g_edge_search_result.status;
  g_search_result.expanded = g_edge_search_result.expanded;
  g_search_result.generated = g_edge_search_result.generated;
  g_search_result.transpositions = g_edge_search_result.transpositions;
  g_search_result.local_expanded = g_edge_search_result.local_expanded;
  g_search_result.command_transitions =
      g_edge_search_result.command_transitions;
  g_search_result.full_physics_transitions =
      g_edge_search_result.full_physics_transitions;
  return static_cast<int32_t>(g_edge_search_result.status);
}

int32_t search_edge_count() {
  return g_edge_search_result.edges;
}

int32_t search_edge_solution(
    int32_t index,
    int32_t count,
    int32_t width,
    int32_t height) {
  EnsureInitialized();
  g_search_result = voxelbench::search_edge_solution(
      &g_search_workspace,
      &g_workspace,
      index,
      count,
      width,
      height);
  return static_cast<int32_t>(g_search_result.status);
}

int32_t search_moves() {
  return g_search_result.moves;
}

int32_t search_expanded() {
  return g_search_result.expanded;
}

int32_t search_generated() {
  return g_search_result.generated;
}

int32_t search_transpositions() {
  return g_search_result.transpositions;
}

int32_t search_local_expanded() {
  return g_search_result.local_expanded;
}

int32_t search_command_transitions() {
  return g_search_result.command_transitions;
}

int32_t search_full_physics_transitions() {
  return g_search_result.full_physics_transitions;
}

int32_t search_solution_length() {
  return g_search_result.solution_length;
}

int32_t search_solution_step(int32_t index) {
  if (index < 0 || index >= g_search_result.solution_length) return -1;
  return g_search_result.solution[index];
}

}  // extern "C"
