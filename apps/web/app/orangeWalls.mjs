import { liftIsRaised } from "./visualVariants.mjs";

function nonnegativeInteger(value, fallback = 0) {
  const number = Number(value);
  return Number.isInteger(number) && number >= 0 ? number : fallback;
}

function definitionFor(definitions, blockId) {
  return definitions instanceof Map
    ? definitions.get(blockId)
    : definitions?.find?.((definition) => definition.id === blockId);
}

export function orangeWallMechanismDepth(wall) {
  if (wall?.mechanismDepth !== undefined) {
    return nonnegativeInteger(wall.mechanismDepth);
  }
  // Before binary wall states were introduced, stateId held the additive
  // lowering depth directly. Retaining this fallback migrates every authored
  // frame without rewriting its layout.
  return nonnegativeInteger(wall?.stateId);
}

export function orangeWallSupportZ(wall, voxels, definitions) {
  let supportZ = Number.NEGATIVE_INFINITY;
  for (const candidate of voxels) {
    if (
      candidate === wall || candidate.x !== wall.x || candidate.y !== wall.y ||
      candidate.z >= wall.z
    ) {
      continue;
    }
    const definition = definitionFor(definitions, candidate.blockId);
    if (!definition || definition.visual?.kind === "gem" ||
        definition.visual?.kind === "button" ||
        definition.visual?.kind === "orange-wall" ||
        definition.roleId === "player" ||
        definition.roleId === "pushable" ||
        definition.roleId === "weightless-pushable" ||
        (definition.visual?.kind === "lift" && !liftIsRaised(candidate.genericId))) {
      continue;
    }
    if (definition.occupancy === "solid" || definition.visual?.kind === "lift") {
      supportZ = Math.max(supportZ, candidate.z);
    }
  }
  return supportZ;
}

export function orangeWallPhysicalState(wall, voxels, definitions) {
  const mechanismDepth = orangeWallMechanismDepth(wall);
  const supportZ = orangeWallSupportZ(wall, voxels, definitions);
  const desiredZ = wall.z - mechanismDepth;
  const flattened = Number.isFinite(supportZ) && desiredZ <= supportZ;
  return {
    mechanismDepth,
    physicalZ: flattened ? supportZ + 1 : desiredZ,
    stateId: flattened ? 0 : 1,
    supportZ,
  };
}

export function orangeWallDepthForState(wall, voxels, definitions, stateId) {
  if (Number(stateId) !== 0) return 0;
  const supportZ = orangeWallSupportZ(wall, voxels, definitions);
  if (!Number.isFinite(supportZ) || supportZ >= wall.z) return null;
  return wall.z - supportZ;
}

export function normalizeOrangeWallFrame(frame, definitions) {
  const voxels = frame.voxels.map((voxel) => ({ ...voxel }));
  return {
    ...frame,
    voxels: voxels.map((voxel) => {
      const definition = definitionFor(definitions, voxel.blockId);
      if (definition?.visual?.kind !== "orange-wall") return voxel;
      const state = orangeWallPhysicalState(voxel, voxels, definitions);
      return {
        ...voxel,
        mechanismDepth: state.mechanismDepth,
        stateId: state.stateId,
      };
    }),
  };
}

export function orangeWallVisualFrame(frame, definitions) {
  const normalized = normalizeOrangeWallFrame(frame, definitions);
  const visibleVoxels = normalized.voxels.flatMap((voxel) => {
      const definition = definitionFor(definitions, voxel.blockId);
      if (definition?.visual?.kind !== "orange-wall") return [voxel];
      const state = orangeWallPhysicalState(voxel, normalized.voxels, definitions);
      const visible = { ...voxel, z: state.physicalZ, stateId: state.stateId };
      if (state.stateId === 1 && Number.isFinite(state.supportZ)) {
        const column = [];
        for (let z = state.supportZ + 1; z <= state.physicalZ; z += 1) {
          column.push({ ...visible, z, mechanismDepth: 0 });
        }
        return column;
      }
      const visibleDepth = state.stateId === 0
        ? orangeWallDepthForState(visible, normalized.voxels, definitions, 0) ?? 0
        : 0;
      return [{ ...visible, mechanismDepth: visibleDepth }];
    });
  const brickCells = new Set(visibleVoxels.flatMap((voxel) => {
    const definition = definitionFor(definitions, voxel.blockId);
    return definition?.visual?.kind === "orange-wall" && Number(voxel.stateId) === 1
      ? [`${voxel.x},${voxel.y},${voxel.z}`]
      : [];
  }));
  const retainedBrickCells = new Set();
  const retainedSurfaceCells = new Set();
  return {
    ...normalized,
    voxels: visibleVoxels.filter((voxel) => {
      const definition = definitionFor(definitions, voxel.blockId);
      if (definition?.visual?.kind !== "orange-wall") {
        return true;
      }
      const cell = `${voxel.x},${voxel.y},${voxel.z}`;
      if (Number(voxel.stateId) === 1) {
        if (retainedBrickCells.has(cell)) return false;
        retainedBrickCells.add(cell);
        return true;
      }
      if (brickCells.has(cell) || retainedSurfaceCells.has(cell)) return false;
      retainedSurfaceCells.add(cell);
      return true;
    }),
  };
}
