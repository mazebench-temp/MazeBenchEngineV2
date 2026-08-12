/** @typedef {{ x: number, y: number, z: number, blockId: string, genericId?: number }} Voxel */
/** @typedef {{ width: number, height: number }} HorizontalWorld */

const FACE_NEIGHBORS = [
  [1, 0, 0],
  [-1, 0, 0],
  [0, 1, 0],
  [0, -1, 0],
  [0, 0, 1],
  [0, 0, -1],
];

/**
 * @param {{ x: number, y: number, z: number }} voxel
 */
export function voxelCoordinateKey(voxel) {
  return `${voxel.x},${voxel.y},${voxel.z}`;
}

/**
 * Selects the complete six-neighbor component containing `origin`. "Exact
 * type" includes the generic object ID, so adjacent numbered polycubes remain
 * independently selectable.
 *
 * @param {Voxel[]} voxels
 * @param {{ x: number, y: number, z: number }} origin
 * @returns {string[]}
 */
export function selectConnectedVoxelGroup(voxels, origin) {
  const voxelsByCoordinate = new Map(voxels.map((voxel) => [voxelCoordinateKey(voxel), voxel]));
  const first = voxelsByCoordinate.get(voxelCoordinateKey(origin));
  if (!first) return [];

  const genericId = Number.isInteger(first.genericId) ? first.genericId : -1;
  const selected = new Set([voxelCoordinateKey(first)]);
  const pending = [first];

  while (pending.length) {
    const voxel = pending.pop();
    if (!voxel) break;
    for (const [dx, dy, dz] of FACE_NEIGHBORS) {
      const neighbor = voxelsByCoordinate.get(`${voxel.x + dx},${voxel.y + dy},${voxel.z + dz}`);
      if (!neighbor || selected.has(voxelCoordinateKey(neighbor))) continue;
      const neighborGenericId = Number.isInteger(neighbor.genericId) ? neighbor.genericId : -1;
      if (neighbor.blockId !== first.blockId || neighborGenericId !== genericId) continue;
      selected.add(voxelCoordinateKey(neighbor));
      pending.push(neighbor);
    }
  }

  return [...selected];
}

/**
 * Select a group, or remove the complete group when it is already selected.
 * Additive selection preserves other groups; a normal new selection replaces
 * them.
 *
 * @param {Iterable<string>} currentCoordinateKeys
 * @param {Iterable<string>} groupCoordinateKeys
 * @param {boolean} additive
 * @returns {{ deselected: boolean, keys: string[] }}
 */
export function toggleVoxelGroupSelection(currentCoordinateKeys, groupCoordinateKeys, additive) {
  const currentKeys = [...currentCoordinateKeys];
  const groupKeys = [...groupCoordinateKeys];
  const current = new Set(currentKeys);
  const deselected = groupKeys.length > 0 && groupKeys.every((key) => current.has(key));

  if (deselected) {
    const group = new Set(groupKeys);
    return { deselected: true, keys: currentKeys.filter((key) => !group.has(key)) };
  }

  return {
    deselected: false,
    keys: [...new Set([...(additive ? currentKeys : []), ...groupKeys])],
  };
}

/**
 * Remove every voxel whose coordinate is in the current group selection.
 *
 * @param {Voxel[]} voxels
 * @param {Iterable<string>} selectedCoordinateKeys
 * @returns {{ removedCount: number, voxels: Voxel[] }}
 */
export function removeSelectedVoxels(voxels, selectedCoordinateKeys) {
  const selected = new Set(selectedCoordinateKeys);
  const remaining = voxels.filter((voxel) => !selected.has(voxelCoordinateKey(voxel)));
  return {
    removedCount: voxels.length - remaining.length,
    voxels: remaining.map((voxel) => ({ ...voxel })),
  };
}

/**
 * Moves a selected component one cell if the complete destination is valid.
 * Horizontal room bounds are enforced; Z is deliberately unbounded.
 *
 * @param {Voxel[]} voxels
 * @param {Iterable<string>} selectedCoordinateKeys
 * @param {number} dx
 * @param {number} dy
 * @param {HorizontalWorld} world
 * @param {number} [dz]
 * @returns {{ moved: boolean, selectedKeys: string[], voxels: Voxel[] }}
 */
export function moveVoxelGroup(voxels, selectedCoordinateKeys, dx, dy, world, dz = 0) {
  const selected = new Set(selectedCoordinateKeys);
  const moving = voxels.filter((voxel) => selected.has(voxelCoordinateKey(voxel)));
  if (!moving.length || (dx === 0 && dy === 0 && dz === 0)) {
    return { moved: false, selectedKeys: [...selected], voxels: voxels.map((voxel) => ({ ...voxel })) };
  }

  const stationaryCoordinates = new Set(
    voxels
      .filter((voxel) => !selected.has(voxelCoordinateKey(voxel)))
      .map(voxelCoordinateKey),
  );
  const destinations = moving.map((voxel) => ({
    ...voxel,
    x: voxel.x + dx,
    y: voxel.y + dy,
    z: voxel.z + dz,
  }));
  const blocked = destinations.some((voxel) =>
    voxel.x < 0 ||
    voxel.x >= world.width ||
    voxel.y < 0 ||
    voxel.y >= world.height ||
    stationaryCoordinates.has(voxelCoordinateKey(voxel)),
  );
  if (blocked) {
    return { moved: false, selectedKeys: [...selected], voxels: voxels.map((voxel) => ({ ...voxel })) };
  }

  const movedBySource = new Map(
    moving.map((voxel, index) => [voxelCoordinateKey(voxel), destinations[index]]),
  );
  return {
    moved: true,
    selectedKeys: destinations.map(voxelCoordinateKey),
    voxels: voxels.map((voxel) => movedBySource.get(voxelCoordinateKey(voxel)) ?? { ...voxel }),
  };
}
