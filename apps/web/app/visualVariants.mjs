export const SLOPE_DIRECTIONS = ["up", "right", "down", "left"];

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
