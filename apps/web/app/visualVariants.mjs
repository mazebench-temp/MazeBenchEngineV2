export const SLOPE_DIRECTIONS = ["up", "right", "down", "left"];
export const LIFT_ORIENTATIONS = ["top", "north", "east", "south", "west"];
export const LIFT_GENERIC_MAX = 9;

const DIRECTION_ALIASES = new Map([
  ["north", "up"],
  ["east", "right"],
  ["south", "down"],
  ["west", "left"],
  ["u", "up"],
  ["r", "right"],
  ["d", "down"],
  ["l", "left"],
]);

const LIFT_ORIENTATION_ALIASES = new Map([
  ["up", "top"],
  ["upward", "top"],
  ["floor", "top"],
  ["front", "north"],
  ["right", "east"],
  ["back", "south"],
  ["left", "west"],
]);

export function normalizeSlopeDirection(orientation, variantId = 0) {
  const candidate = String(orientation ?? "").trim().toLowerCase();
  const normalized = DIRECTION_ALIASES.get(candidate) ?? candidate;
  if (SLOPE_DIRECTIONS.includes(normalized)) return normalized;
  const index = Number.isInteger(Number(variantId)) ? Number(variantId) : 0;
  return SLOPE_DIRECTIONS[((index % 4) + 4) % 4];
}

export function slopeDirectionIndex(direction) {
  return SLOPE_DIRECTIONS.indexOf(normalizeSlopeDirection(direction));
}

export function offsetSlopeDirection(direction, offset) {
  const index = slopeDirectionIndex(direction);
  return SLOPE_DIRECTIONS[((index + offset) % 4 + 4) % 4];
}

export function rotateSlopeMetadata(voxel, quarterTurns) {
  const orientation = String(voxel?.orientation ?? "").trim().toLowerCase();
  const normalized = DIRECTION_ALIASES.get(orientation) ?? orientation;
  if (!SLOPE_DIRECTIONS.includes(normalized)) return { ...voxel };
  const nextDirection = offsetSlopeDirection(normalized, quarterTurns);
  return {
    ...voxel,
    orientation: nextDirection,
    variantId: slopeDirectionIndex(nextDirection),
  };
}

export function liftIsRaised(genericId = 0) {
  const id = Math.max(0, Math.min(LIFT_GENERIC_MAX, Math.floor(Number(genericId) || 0)));
  return id % 2 === 1;
}

export function liftGenericId(orientation, raised = false) {
  return liftOrientationIndex(orientation) * 2 + (raised ? 1 : 0);
}

// Editor face picks are expressed in world coordinates, so this mapping stays
// correct regardless of the camera yaw. A bottom-face pick is deliberately not
// mapped: downward lifts do not exist in the authored 0–9 family yet.
export function liftOrientationFromPaintFace(facePick = {}) {
  if (facePick?.face === "bottom-face") return null;
  const dx = Math.sign(Number(facePick?.dx) || 0);
  const dy = Math.sign(Number(facePick?.dy) || 0);
  if (dx > 0) return "east";
  if (dx < 0) return "west";
  if (dy > 0) return "south";
  if (dy < 0) return "north";
  return "top";
}

export function liftOrientationFromGenericId(genericId = 0) {
  const id = Math.max(0, Math.min(LIFT_GENERIC_MAX, Math.floor(Number(genericId) || 0)));
  return LIFT_ORIENTATIONS[Math.floor(id / 2)];
}

export function normalizeLiftOrientation(orientation, variantId = 0, genericId) {
  if (genericId !== undefined && genericId !== null) {
    return liftOrientationFromGenericId(genericId);
  }
  const candidate = String(orientation ?? "").trim().toLowerCase();
  const normalized = LIFT_ORIENTATION_ALIASES.get(candidate) ?? candidate;
  if (LIFT_ORIENTATIONS.includes(normalized)) return normalized;
  const index = Number.isInteger(Number(variantId)) ? Number(variantId) : 0;
  return LIFT_ORIENTATIONS[((index % LIFT_ORIENTATIONS.length) + LIFT_ORIENTATIONS.length) % LIFT_ORIENTATIONS.length];
}

export function liftOrientationIndex(orientation) {
  return LIFT_ORIENTATIONS.indexOf(normalizeLiftOrientation(orientation));
}

export function rotateLiftMetadata(voxel, quarterTurns) {
  const explicitOrientation = normalizeLiftOrientation(voxel?.orientation, voxel?.variantId);
  const hasEncodedOrientation = Number(voxel?.genericId) >= 2;
  const orientation = hasEncodedOrientation
    ? liftOrientationFromGenericId(voxel.genericId)
    : explicitOrientation;
  const raised = liftIsRaised(voxel?.genericId);
  if (orientation === "top") {
    const genericId = liftGenericId(orientation, raised);
    return {
      ...voxel,
      orientation,
      variantId: 0,
      ...(voxel?.genericId === undefined ? {} : { genericId }),
      ...(voxel?.groupId === undefined ? {} : { groupId: genericId }),
    };
  }
  const horizontal = LIFT_ORIENTATIONS.slice(1);
  const index = horizontal.indexOf(orientation);
  const turns = ((quarterTurns % 4) + 4) % 4;
  const nextOrientation = horizontal[(index + turns) % 4];
  const genericId = liftGenericId(nextOrientation, raised);
  return {
    ...voxel,
    orientation: nextOrientation,
    variantId: liftOrientationIndex(nextOrientation),
    ...(voxel?.genericId === undefined ? {} : { genericId }),
    ...(voxel?.groupId === undefined ? {} : { groupId: genericId }),
  };
}

export function rotateVoxelVisualMetadata(voxel, quarterTurns) {
  return voxel?.blockId === "player-lift"
    ? rotateLiftMetadata(voxel, quarterTurns)
    : rotateSlopeMetadata(voxel, quarterTurns);
}
