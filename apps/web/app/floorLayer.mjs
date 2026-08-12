export function enforceFloorLayer(voxels, floorBlockIds) {
  return voxels.filter((voxel) => !floorBlockIds.has(voxel.blockId) || voxel.z === 0);
}

export function selectionContainsFloor(voxels, selectedKeys, floorBlockIds) {
  const selected = new Set(selectedKeys);
  return voxels.some((voxel) =>
    selected.has(`${voxel.x},${voxel.y},${voxel.z}`) && floorBlockIds.has(voxel.blockId));
}
