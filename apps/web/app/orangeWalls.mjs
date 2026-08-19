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

export function orangeWallIsDedicatedFace(wall) {
  return Number(wall?.stateId) === 0;
}

export function orangeWallIsHiddenVolume(wall) {
  return Number(wall?.stateId) === 2;
}

export function orangeWallMechanismValue(wall) {
  return orangeWallMechanismDepth(wall);
}

export function orangeWallDepthFromMechanismValue(value) {
  const encoded = Number(value);
  if (!Number.isInteger(encoded)) return 0;
  // Read the old private Orange Face encoding for project/WASM compatibility,
  // but every newly written engine value is the ordinary nonnegative depth.
  return encoded <= -2 ? Math.max(0, -encoded - 2) : Math.max(0, encoded);
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
  const lowestAnchorZ = voxels.reduce((lowest, candidate) => {
    const definition = definitionFor(definitions, candidate.blockId);
    if (definition?.roleId !== "orange-wall" ||
        candidate.x !== wall.x || candidate.y !== wall.y) {
      return lowest;
    }
    return Math.min(lowest, orangeWallEngineAnchorZ(candidate, definitions));
  }, orangeWallEngineAnchorZ(wall, definitions));
  let supportZ = Number.NEGATIVE_INFINITY;
  for (const candidate of voxels) {
    if (
      candidate === wall || candidate.x !== wall.x || candidate.y !== wall.y ||
      candidate.z >= lowestAnchorZ
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

function orangeWallSupportZFromEngineAnchors(wall, voxels, definitions) {
  const lowestAnchorZ = voxels.reduce((lowest, candidate) => {
    const definition = definitionFor(definitions, candidate.blockId);
    return definition?.roleId === "orange-wall" &&
      candidate.x === wall.x && candidate.y === wall.y
      ? Math.min(lowest, candidate.z)
      : lowest;
  }, wall.z);
  let supportZ = Number.NEGATIVE_INFINITY;
  for (const candidate of voxels) {
    if (candidate === wall || candidate.x !== wall.x || candidate.y !== wall.y ||
        candidate.z >= lowestAnchorZ) {
      continue;
    }
    const definition = definitionFor(definitions, candidate.blockId);
    if (!definition || definition.visual?.kind === "gem" ||
        definition.visual?.kind === "button" ||
        definition.visual?.kind === "orange-wall" ||
        definition.roleId === "player" || definition.roleId === "pushable" ||
        definition.roleId === "weightless-pushable" ||
        (definition.visual?.kind === "lift" &&
         !liftIsRaised(candidate.genericId))) {
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
      const storedState = Number(voxel.stateId);
      return {
        ...voxel,
        mechanismDepth: state.mechanismDepth,
        // Legacy face/hidden IDs migrate to this internal physical-state bit.
        // It is not an editor object type and does not alter the cube visual.
        stateId: storedState === 0 || storedState === 2 ? storedState : 1,
      };
    }),
  };
}

// C++ stores a stable fully-raised anchor for each Orange Wall voxel. Authored
// frames instead store exactly what the editor displays. Converting at the ABI
// boundary keeps C++ state fixed-size while allowing cube/face/hidden records
// to change form at every mechanism tick.
export function orangeWallEngineAnchorZ(wall) {
  const depth = orangeWallMechanismDepth(wall);
  if (Number(wall?.stateId) === 0) return wall.z + Math.max(0, depth - 1);
  return wall.z + depth;
}

export function orangeWallFrameFromEngine(frame, definitions) {
  const engineVoxels = frame.voxels.map((voxel) => ({ ...voxel }));
  return {
    ...frame,
    voxels: engineVoxels.map((voxel) => {
      const definition = definitionFor(definitions, voxel.blockId);
      if (definition?.roleId !== "orange-wall") return voxel;
      const depth = orangeWallMechanismDepth(voxel);
      const supportZ = orangeWallSupportZFromEngineAnchors(
        voxel, engineVoxels, definitions);
      const desiredZ = voxel.z - depth;
      const stateId = Number.isFinite(supportZ) && desiredZ === supportZ
        ? 0
        : Number.isFinite(supportZ) && desiredZ < supportZ ? 2 : 1;
      return {
        ...voxel,
        blockId: "orange-wall",
        mechanismDepth: depth,
        stateId,
        z: stateId === 0 ? supportZ + 1 : desiredZ,
      };
    }),
  };
}

export function orangeWallVisualFrame(frame, definitions) {
  return normalizeOrangeWallFrame(frame, definitions);
}
