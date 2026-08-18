export const OCCUPANCY_PROFILES = Object.freeze([
  Object.freeze({ id: "solid", label: "Solid volume", shareable: false }),
  Object.freeze({ id: "sensor", label: "Sensor / pass-through", shareable: true }),
  Object.freeze({ id: "support", label: "Support surface", shareable: true }),
  Object.freeze({ id: "decoration", label: "Decoration / pass-through", shareable: true }),
  Object.freeze({ id: "inactive", label: "Inactive / empty", shareable: true }),
]);

const OCCUPANCY_IDS = new Set(OCCUPANCY_PROFILES.map((profile) => profile.id));

/**
 * Old projects did not store editor occupancy. Goals were already non-rigid in
 * C++, so migrating them to sensors preserves their established semantics.
 * Other roles stay solid until the author explicitly changes the definition.
 *
 * @param {unknown} value
 * @param {string} roleId
 */
export function normalizeOccupancyProfile(value, roleId) {
  const requested = String(value ?? "");
  if (OCCUPANCY_IDS.has(requested)) return requested;
  if (roleId === "goal") return "sensor";
  if (roleId === "decor") return "decoration";
  return "solid";
}

/** @param {{ occupancy?: string, roleId?: string } | undefined} block */
export function blockCanShareCell(block) {
  return normalizeOccupancyProfile(block?.occupancy, String(block?.roleId ?? "")) !== "solid";
}

/**
 * Orange-wall form belongs to its block definition. Remaining-rise/state data
 * must never turn the dedicated Orange Cube into a shareable face. Orange
 * Faces and editor-visible hidden volumes deliberately share occupied cells.
 *
 * @param {{ occupancy?: string, roleId?: string } | undefined} block
 * @param {{ stateId?: number } | undefined} object
 */
export function objectCanShareCell(block, object) {
  if (block?.roleId === "orange-wall") {
    return block?.visual?.orangeForm === "face" ||
      block?.visual?.orangeForm === "hidden";
  }
  return blockCanShareCell(block);
}

/** @param {{ x: number, y: number, z: number }} object */
export function cellCoordinateKey(object) {
  return `${object.x},${object.y},${object.z}`;
}

/**
 * A semantic key deliberately ignores instanceId: visual unit tests compare
 * what occupies a cell, not the editor identity or array order of that record.
 * Legacy genericId and the new groupId normalize to the same value.
 *
 * @param {Record<string, unknown> & { x: number, y: number, z: number, blockId: string }} object
 */
export function cellObjectSemanticKey(object) {
  const groupId = Number.isInteger(object.groupId)
    ? object.groupId
    : Number.isInteger(object.genericId)
      ? object.genericId
      : -1;
  const variantId = Number.isInteger(object.variantId) ? object.variantId : 0;
  const stateId = Number.isInteger(object.stateId) ? object.stateId : 0;
  const mechanismDepth = Number.isInteger(object.mechanismDepth)
    ? object.mechanismDepth
    : -1;
  const orientation = String(object.orientation ?? "none");
  return [
    cellCoordinateKey(object),
    object.blockId,
    groupId,
    variantId,
    stateId,
    mechanismDepth,
    orientation,
  ].join(":");
}

/**
 * Stable IDs let two otherwise identical occupants remain independently
 * selectable. Legacy data falls back to its semantic identity.
 *
 * @param {Record<string, unknown> & { x: number, y: number, z: number, blockId: string }} object
 */
export function cellObjectSelectionKey(object) {
  const instanceId = String(object.instanceId ?? "").trim();
  return instanceId ? `instance:${instanceId}` : `semantic:${cellObjectSemanticKey(object)}`;
}

/**
 * @template T
 * @param {T[]} expected
 * @param {T[]} actual
 * @param {(value: T) => string} [identity]
 * @returns {{ missing: T[], unexpected: T[] }}
 */
export function diffObjectMultisets(expected, actual, identity = cellObjectSemanticKey) {
  const remainingActual = new Map();
  for (const value of actual) {
    const key = identity(value);
    const values = remainingActual.get(key) ?? [];
    values.push(value);
    remainingActual.set(key, values);
  }

  const missing = [];
  for (const value of expected) {
    const key = identity(value);
    const matches = remainingActual.get(key);
    if (!matches?.length) {
      missing.push(value);
      continue;
    }
    matches.pop();
    if (!matches.length) remainingActual.delete(key);
  }

  return {
    missing,
    unexpected: [...remainingActual.values()].flat(),
  };
}

/**
 * Adds or replaces a placement according to editor occupancy. A solid volume
 * replaces only other solid volumes at the target cell; sensors, supports,
 * decorations, and inactive fixtures remain. A shareable placement simply
 * joins the cell unless the exact same semantic object is already present.
 *
 * @template {Record<string, unknown> & { x: number, y: number, z: number, blockId: string }} T
 * @param {T[]} objects
 * @param {T} placement
 * @param {Map<string, { occupancy?: string, roleId?: string }>} definitions
 * @returns {{ changed: boolean, objects: T[] }}
 */
export function placeObjectInCell(objects, placement, definitions) {
  const coordinate = cellCoordinateKey(placement);
  const semantic = cellObjectSemanticKey(placement);
  const occupants = objects.filter((object) => cellCoordinateKey(object) === coordinate);
  if (occupants.some((object) => cellObjectSemanticKey(object) === semantic)) {
    return { changed: false, objects: objects.map((object) => ({ ...object })) };
  }

  const placementShareable = objectCanShareCell(
    definitions.get(placement.blockId), placement);
  const kept = placementShareable
    ? definitions.get(placement.blockId)?.roleId === "orange-wall"
      ? objects.filter((object) =>
          cellCoordinateKey(object) !== coordinate ||
          object.blockId !== placement.blockId)
      : objects
    : objects.filter((object) =>
      cellCoordinateKey(object) !== coordinate ||
      objectCanShareCell(definitions.get(object.blockId), object));
  return {
    changed: true,
    objects: [...kept.map((object) => ({ ...object })), { ...placement }],
  };
}

/**
 * Erase removes one occupant instead of silently clearing an entire stack.
 * The last painted/rendered record is the default; Cell Contents can remove a
 * specifically chosen record.
 *
 * @template {Record<string, unknown> & { x: number, y: number, z: number, blockId: string }} T
 * @param {T[]} objects
 * @param {{ x: number, y: number, z: number }} coordinate
 * @param {string | undefined} selectionKey
 * @returns {{ changed: boolean, objects: T[], removed: T | null }}
 */
export function eraseOneObjectAtCell(objects, coordinate, selectionKey) {
  const key = cellCoordinateKey(coordinate);
  let removeIndex = -1;
  for (let index = objects.length - 1; index >= 0; index -= 1) {
    if (selectionKey
      ? cellObjectSelectionKey(objects[index]) === selectionKey
      : cellCoordinateKey(objects[index]) === key) {
      removeIndex = index;
      break;
    }
  }
  if (removeIndex < 0) {
    return { changed: false, objects: objects.map((object) => ({ ...object })), removed: null };
  }
  return {
    changed: true,
    objects: objects.filter((_, index) => index !== removeIndex).map((object) => ({ ...object })),
    removed: { ...objects[removeIndex] },
  };
}
