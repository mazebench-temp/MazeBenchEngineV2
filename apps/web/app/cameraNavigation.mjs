const DIRECTIONS = ["up", "right", "down", "left"];

/**
 * Convert a free camera yaw to the nearest normalized quarter turn.
 *
 * @param {number} yaw
 */
export function cameraYawQuarterTurns(yaw) {
  const turns = Math.round((Number(yaw) || 0) / (Math.PI / 2));
  return ((turns % 4) + 4) % 4;
}

/**
 * Map a screen arrow to the world direction that visually matches the current
 * camera quarter turn.
 *
 * @param {"up" | "right" | "down" | "left"} screenDirection
 * @param {number} cameraQuarterTurns
 */
export function cameraRelativeDirection(screenDirection, cameraQuarterTurns) {
  const screenIndex = DIRECTIONS.indexOf(screenDirection);
  if (screenIndex < 0) return screenDirection;
  const turns = ((Math.round(cameraQuarterTurns) % 4) + 4) % 4;
  return DIRECTIONS[(screenIndex - turns + 4) % 4];
}
