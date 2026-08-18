import assert from "node:assert/strict";
import test from "node:test";

import {
  normalizeOrangeWallFrame,
  orangeWallDepthFromMechanismValue,
  orangeWallDepthForState,
  orangeWallFrameFromEngine,
  orangeWallIsDedicatedFace,
  orangeWallIsHiddenVolume,
  orangeWallMechanismValue,
  orangeWallPhysicalState,
  orangeWallVisualFrame,
} from "../app/orangeWalls.mjs";

const definitions = new Map([
  ["floor", { id: "floor", occupancy: "solid", visual: { kind: "cube" } }],
  ["orange-wall-face", {
    id: "orange-wall-face",
    occupancy: "support",
    roleId: "orange-wall",
    visual: { kind: "orange-wall", orangeForm: "face" },
  }],
  ["orange-wall", {
    id: "orange-wall",
    occupancy: "solid",
    roleId: "orange-wall",
    visual: { kind: "orange-wall", orangeForm: "cube" },
  }],
  ["orange-wall-hidden", {
    id: "orange-wall-hidden",
    occupancy: "inactive",
    roleId: "orange-wall",
    visual: { kind: "orange-wall", orangeForm: "hidden" },
  }],
]);

test("Orange Face ABI values use the same nonnegative depth as every form", () => {
  const face = {
    x: 1, y: 1, z: 1, blockId: "orange-wall-face", stateId: 0, mechanismDepth: 7,
  };
  assert.equal(orangeWallMechanismValue(face, definitions), 7);
  // Old project/WASM snapshots remain readable during migration.
  assert.equal(orangeWallDepthFromMechanismValue(-9), 7);
  assert.equal(orangeWallPhysicalState(face, [face], definitions).stateId, 0);
});

test("Orange Cube and Orange Face definitions stay distinct at every depth", () => {
  const floor = { x: 2, y: 3, z: 0, blockId: "floor" };
  const wall = { x: 2, y: 3, z: 4, blockId: "orange-wall", mechanismDepth: 2, stateId: 1 };
  assert.equal(orangeWallIsDedicatedFace(wall, definitions), false);
  assert.deepEqual(orangeWallPhysicalState(wall, [floor, wall], definitions), {
    mechanismDepth: 2,
    physicalZ: 2,
    stateId: 1,
    supportZ: 0,
  });

  const flattened = { ...wall, mechanismDepth: 4 };
  assert.deepEqual(orangeWallPhysicalState(flattened, [floor, flattened], definitions), {
    mechanismDepth: 4,
    physicalZ: 1,
    stateId: 0,
    supportZ: 0,
  });
  assert.equal(orangeWallIsDedicatedFace(flattened, definitions), false);
});

test("hidden Orange Wall volumes preserve their authored cells and rise metadata", () => {
  const floor = { x: 2, y: 3, z: -3, blockId: "floor" };
  const hidden = {
    x: 2,
    y: 3,
    z: -2,
    blockId: "orange-wall-hidden",
    mechanismDepth: 5,
    stateId: 2,
  };
  assert.equal(orangeWallIsHiddenVolume(hidden, definitions), true);
  assert.deepEqual(orangeWallPhysicalState(hidden, [floor, hidden], definitions), {
    mechanismDepth: 5,
    physicalZ: -2,
    stateId: 2,
    supportZ: -3,
  });
  assert.deepEqual(
    orangeWallVisualFrame({ voxels: [floor, hidden] }, definitions).voxels,
    [floor, hidden],
  );
});

test("legacy depth-in-state frames migrate without moving their authored anchors", () => {
  const floor = { x: 1, y: 1, z: 0, blockId: "floor" };
  const legacy = { x: 1, y: 1, z: 3, blockId: "orange-wall", stateId: 3 };
  const normalized = normalizeOrangeWallFrame({ voxels: [floor, legacy] }, definitions);
  assert.deepEqual(normalized.voxels[1], {
    ...legacy,
    mechanismDepth: 3,
    stateId: 1,
  });
});

test("painting binary surface state computes the required hidden depth", () => {
  const floor = { x: 5, y: 4, z: 0, blockId: "floor" };
  const wall = { x: 5, y: 4, z: 5, blockId: "orange-wall" };
  assert.equal(orangeWallDepthForState(wall, [floor], definitions, 0), 5);
  assert.equal(orangeWallDepthForState(wall, [floor], definitions, 1), 0);
  assert.equal(orangeWallDepthForState(wall, [], definitions, 0), null);
});

test("engine anchors project to authored cells without erasing rise debt", () => {
  const floor = { x: 1, y: 1, z: 0, blockId: "floor" };
  const engineBrick = {
    x: 1, y: 1, z: 3, blockId: "orange-wall", stateId: 1, mechanismDepth: 2,
  };
  const authoredBrick = {
    x: 1, y: 1, z: 1, blockId: "orange-wall", stateId: 1, mechanismDepth: 2,
  };
  assert.deepEqual(
    orangeWallFrameFromEngine(
      { voxels: [floor, engineBrick] }, definitions,
    ).voxels[1],
    authoredBrick,
  );
});

test("visual comparison hides an explicit Orange Face covered by a brick", () => {
  const floor = { x: 1, y: 1, z: 0, blockId: "floor" };
  const lowerWall = {
    x: 1, y: 1, z: 1, blockId: "orange-wall-face", stateId: 0, mechanismDepth: 1,
  };
  const upperWall = {
    x: 1, y: 1, z: 1, blockId: "orange-wall", stateId: 1, mechanismDepth: 1,
  };

  assert.deepEqual(
    orangeWallVisualFrame({ voxels: [floor, lowerWall, upperWall] }, definitions).voxels,
    [floor, upperWall],
  );
});

test("visual comparison collapses coincident explicit Orange Faces", () => {
  const floor = { x: 1, y: 1, z: 0, blockId: "floor" };
  const first = {
    x: 1, y: 1, z: 1, blockId: "orange-wall-face", stateId: 0, mechanismDepth: 2,
  };
  const second = {
    x: 1, y: 1, z: 1, blockId: "orange-wall-face", stateId: 0, mechanismDepth: 2,
  };

  assert.equal(
    orangeWallVisualFrame({ voxels: [floor, first, second] }, definitions).voxels.length,
    2,
  );
});

test("one raised Orange Wall record renders exactly one cube", () => {
  const floor = { x: 1, y: 1, z: 0, blockId: "floor" };
  const wall = {
    x: 1, y: 1, z: 3, blockId: "orange-wall", stateId: 1, mechanismDepth: 0,
  };

  assert.deepEqual(
    orangeWallVisualFrame({ voxels: [floor, wall] }, definitions).voxels
      .filter((voxel) => voxel.blockId === "orange-wall")
      .map((voxel) => voxel.z),
    [3],
  );
});
