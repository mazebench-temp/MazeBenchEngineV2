#include "voxelbench/physics.hpp"

namespace voxelbench {
namespace {

constexpr uint32_t HashRoleLiteral(const char* value, uint32_t hash = 2166136261u) {
  return *value == '\0'
      ? hash
      : HashRoleLiteral(value + 1, (hash ^ static_cast<uint8_t>(*value)) * 16777619u);
}

constexpr uint32_t kPlayerRole = HashRoleLiteral("player");
constexpr uint32_t kPushableRole = HashRoleLiteral("pushable");
constexpr uint32_t kIceRole = HashRoleLiteral("ice");

bool IsInsideRoom(int32_t x, int32_t y, int32_t width, int32_t height) {
  return x >= 0 && x < width && y >= 0 && y < height;
}

int32_t FindVoxelAt(Voxel* voxels, int32_t count, int32_t x, int32_t y, int32_t z) {
  for (int32_t index = 0; index < count; ++index) {
    const Voxel& voxel = voxels[index];
    if (voxel.x == x && voxel.y == y && voxel.z == z) return index;
  }
  return -1;
}

bool IsSupportedByIce(Voxel* voxels, int32_t count, const Voxel& voxel) {
  const int32_t support_index = FindVoxelAt(
      voxels, count, voxel.x, voxel.y, voxel.z - 1);
  return support_index >= 0 && voxels[support_index].role == kIceRole;
}

bool PushAndSlide(
    Voxel* voxels,
    int32_t count,
    int32_t target_index,
    int32_t dx,
    int32_t dy,
    int32_t width,
    int32_t height) {
  Voxel& target = voxels[target_index];
  for (;;) {
    const int32_t next_x = target.x + dx;
    const int32_t next_y = target.y + dy;
    if (!IsInsideRoom(next_x, next_y, width, height) ||
        FindVoxelAt(voxels, count, next_x, next_y, target.z) >= 0) {
      return false;
    }
    target.x = next_x;
    target.y = next_y;
    if (!IsSupportedByIce(voxels, count, target)) return true;

    // The first translation was the player's push. Further translations are
    // automatic Ice motion and stop safely at the first obstacle or edge.
    const int32_t slide_x = target.x + dx;
    const int32_t slide_y = target.y + dy;
    if (!IsInsideRoom(slide_x, slide_y, width, height) ||
        FindVoxelAt(voxels, count, slide_x, slide_y, target.z) >= 0) {
      return true;
    }
  }
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
  if (voxels == nullptr || count < 0 || count > kVoxelCapacity || width <= 0 ||
      height <= 0 || direction < 0 || direction > 3) {
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

  constexpr int32_t kDx[] = {0, 1, 0, -1};
  constexpr int32_t kDy[] = {-1, 0, 1, 0};
  const int32_t dx = kDx[direction];
  const int32_t dy = kDy[direction];
  Voxel& player = voxels[player_index];

  for (;;) {
    const int32_t target_x = player.x + dx;
    const int32_t target_y = player.y + dy;
    if (!IsInsideRoom(target_x, target_y, width, height)) return 0;

    const int32_t target_index = FindVoxelAt(
        voxels, count, target_x, target_y, player.z);
    if (target_index >= 0) {
      if (voxels[target_index].role != kPushableRole ||
          !PushAndSlide(voxels, count, target_index, dx, dy, width, height)) {
        return 0;
      }
    }

    player.x = target_x;
    player.y = target_y;
    if (!IsSupportedByIce(voxels, count, player)) return 0;
  }
}

}  // namespace voxelbench
