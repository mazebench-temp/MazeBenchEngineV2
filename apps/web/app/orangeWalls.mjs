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

export function orangeWallIsDedicatedFace(wall, definitions) {
  return definitionFor(definitions, wall?.blockId)?.visual?.orangeForm === "face";
}

export function orangeWallIsHiddenVolume(wall, definitions) {
  return definitionFor(definitions, wall?.blockId)?.visual?.orangeForm === "hidden";
}

export function orangeWallMechanismValue(wall, definitions) {
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

function orangeWallForm(wall, definitions) {
  const form = definitionFor(definitions, wall?.blockId)?.visual?.orangeForm;
  return form === "face" || form === "hidden" ? form : "cube";
}

function orangeWallBlockIdForForm(wall, definitions, form) {
  const all = definitions instanceof Map
    ? [...definitions.values()]
    : definitions ?? [];
  return all.find((definition) =>
    definition?.roleId === "orange-wall" &&
    (definition.visual?.orangeForm ?? "cube") === form)?.id ?? wall.blockId;
}

// C++ stores a stable fully-raised anchor for each Orange Wall voxel. Authored
// frames instead store exactly what the editor displays. Converting at the ABI
// boundary keeps C++ state fixed-size while allowing cube/face/hidden records
// to change form at every mechanism tick.
export function orangeWallEngineAnchorZ(wall, definitions) {
  const depth = orangeWallMechanismDepth(wall);
  const form = orangeWallForm(wall, definitions);
  if (form === "face") return wall.z + Math.max(0, depth - 1);
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
      const form = Number.isFinite(supportZ) && desiredZ === supportZ
        ? "face"
        : Number.isFinite(supportZ) && desiredZ < supportZ
          ? "hidden"
          : "cube";
      return {
        ...voxel,
        blockId: orangeWallBlockIdForForm(voxel, definitions, form),
        mechanismDepth: depth,
        stateId: form === "hidden" ? 2 : form === "face" ? 0 : 1,
        z: form === "face" ? supportZ + 1 : desiredZ,
      };
    }),
  };
}

export function orangeWallVisualFrame(frame, definitions) {
  const normalized = normalizeOrangeWallFrame(frame, definitions);
  const visibleVoxels = normalized.voxels.map((voxel) => ({ ...voxel }));
  const brickCells = new Set(visibleVoxels.flatMap((voxel) => {
    const definition = definitionFor(definitions, voxel.blockId);
    return definition?.visual?.kind === "orange-wall" &&
      (definition.visual.orangeForm ?? "cube") === "cube"
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
      if ((definition.visual.orangeForm ?? "cube") === "cube") {
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
