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

const PERMANENT_FACE_OFFSET = 2;

export function orangeWallIsDedicatedFace(wall, definitions) {
  return definitionFor(definitions, wall?.blockId)?.visual?.orangeForm === "face";
}

export function orangeWallIsHiddenVolume(wall, definitions) {
  return definitionFor(definitions, wall?.blockId)?.visual?.orangeForm === "hidden";
}

// Negative mechanism values are private ABI encoding, not user-facing IDs.
// -1 remains the ordinary "no generic value" sentinel. Values <= -2 identify
// a dedicated Orange Face while preserving its nonnegative remaining rise.
export function orangeWallMechanismValue(wall, definitions) {
  const depth = orangeWallMechanismDepth(wall);
  return orangeWallIsDedicatedFace(wall, definitions)
    ? -PERMANENT_FACE_OFFSET - depth
    : depth;
}

export function orangeWallDepthFromMechanismValue(value) {
  const encoded = Number(value);
  if (!Number.isInteger(encoded)) return 0;
  return encoded <= -PERMANENT_FACE_OFFSET
    ? Math.max(0, -encoded - PERMANENT_FACE_OFFSET)
    : Math.max(0, encoded);
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
  if (orangeWallIsHiddenVolume(wall, definitions)) {
    return {
      mechanismDepth,
      physicalZ: wall.z,
      stateId: 2,
      supportZ,
    };
  }
  if (orangeWallIsDedicatedFace(wall, definitions)) {
    return {
      mechanismDepth,
      physicalZ: wall.z,
      stateId: 0,
      supportZ,
    };
  }
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
        stateId: orangeWallIsHiddenVolume(voxel, definitions)
          ? 2
          : orangeWallIsDedicatedFace(voxel, definitions) ? 0 : 1,
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
      if (orangeWallIsHiddenVolume(voxel, definitions)) {
        return [{ ...voxel, z: state.physicalZ, stateId: 2 }];
      }
      const visible = {
        ...voxel,
        z: state.physicalZ,
        stateId: orangeWallIsDedicatedFace(voxel, definitions) ? 0 : 1,
      };
      if (visible.stateId === 1 && Number.isFinite(state.supportZ)) {
        const column = [];
        for (let z = state.supportZ + 1; z <= state.physicalZ; z += 1) {
          column.push({ ...visible, z, mechanismDepth: 0 });
        }
        return column;
      }
      const visibleDepth = visible.stateId === 0
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
      if (orangeWallIsHiddenVolume(voxel, definitions)) return true;
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
