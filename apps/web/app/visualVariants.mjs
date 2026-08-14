export const SLOPE_DIRECTIONS = ["up", "right", "down", "left"];
export const LIFT_ORIENTATIONS = ["top", "north", "east", "south", "west"];

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

export function normalizeLiftOrientation(orientation, variantId = 0) {
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
  const orientation = normalizeLiftOrientation(voxel?.orientation, voxel?.variantId);
  if (orientation === "top") {
    return { ...voxel, orientation, variantId: 0 };
  }
  const horizontal = LIFT_ORIENTATIONS.slice(1);
  const index = horizontal.indexOf(orientation);
  const turns = ((quarterTurns % 4) + 4) % 4;
  const nextOrientation = horizontal[(index + turns) % 4];
  return {
    ...voxel,
    orientation: nextOrientation,
    variantId: liftOrientationIndex(nextOrientation),
  };
}

export function rotateVoxelVisualMetadata(voxel, quarterTurns) {
  return voxel?.blockId === "player-lift"
    ? rotateLiftMetadata(voxel, quarterTurns)
    : rotateSlopeMetadata(voxel, quarterTurns);
}
