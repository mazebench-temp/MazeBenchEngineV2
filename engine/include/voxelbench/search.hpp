#pragma once

#include "voxelbench/physics.hpp"

#include <stdint.h>

namespace voxelbench {

// Search stores one translation per rigid moving entity rather than one state
// coordinate per voxel. A generic polycube may therefore fill the authored
// search volume without making each node larger.
constexpr int32_t kSearchVoxelCapacity = 4096;
constexpr int32_t kSearchDynamicEntityCapacity = 64;
constexpr int32_t kSearchNodeCapacity = 50000;
constexpr int32_t kSearchSolutionCapacity = 4096;
constexpr int32_t kSearchWorkspaceBytes = 24 * 1024 * 1024;

struct SearchWorkspace {
  alignas(8) uint8_t storage[kSearchWorkspaceBytes];
};

enum class SearchStatus : int32_t {
  kInvalid = -1,
  kUnsolved = 0,
  kSolved = 1,
  kLimitHit = 2,
};

struct SearchResult {
  SearchStatus status;
  int32_t moves;
  int32_t expanded;
  int32_t generated;
  int32_t transpositions;
  int32_t solution_length;
  int32_t solution[kSearchSolutionCapacity];
};

// Exact shortest-command search. This compact browser/search backend stores
// only moving entity translations; immutable terrain and each polycube's
// relative voxel geometry remain in one scene copy.
// Uniform command costs make the breadth-first queue A* with h=0.
SearchResult search_shortest(
    SearchWorkspace* search_workspace,
    PhysicsWorkspace* physics_workspace,
    const Voxel* voxels,
    int32_t count,
    int32_t width,
    int32_t height,
    int32_t maximum_nodes = kSearchNodeCapacity);

}  // namespace voxelbench
