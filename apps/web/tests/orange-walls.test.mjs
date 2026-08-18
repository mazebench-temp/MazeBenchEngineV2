import assert from "node:assert/strict";
import test from "node:test";

import {
  normalizeOrangeWallFrame,
  orangeWallDepthForState,
  orangeWallPhysicalState,
  orangeWallVisualFrame,
} from "../app/orangeWalls.mjs";

const definitions = new Map([
  ["floor", { id: "floor", occupancy: "solid", visual: { kind: "cube" } }],
  ["orange-wall", { id: "orange-wall", occupancy: "solid", visual: { kind: "orange-wall" } }],
]);

test("orange wall state is binary while mechanism depth remains additive", () => {
  const floor = { x: 2, y: 3, z: 0, blockId: "floor" };
  const wall = { x: 2, y: 3, z: 4, blockId: "orange-wall", mechanismDepth: 2, stateId: 1 };
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
});

test("legacy depth-in-state frames migrate without moving their authored anchors", () => {
  const floor = { x: 1, y: 1, z: 0, blockId: "floor" };
  const legacy = { x: 1, y: 1, z: 3, blockId: "orange-wall", stateId: 3 };
  const normalized = normalizeOrangeWallFrame({ voxels: [floor, legacy] }, definitions);
  assert.deepEqual(normalized.voxels[1], {
    ...legacy,
    mechanismDepth: 3,
    stateId: 0,
  });
});

test("painting binary surface state computes the required hidden depth", () => {
  const floor = { x: 5, y: 4, z: 0, blockId: "floor" };
  const wall = { x: 5, y: 4, z: 5, blockId: "orange-wall" };
  assert.equal(orangeWallDepthForState(wall, [floor], definitions, 0), 5);
  assert.equal(orangeWallDepthForState(wall, [floor], definitions, 1), 0);
  assert.equal(orangeWallDepthForState(wall, [], definitions, 0), null);
});

test("visual comparison collapses hidden anchors to the rendered binary frame", () => {
  const floor = { x: 1, y: 1, z: 0, blockId: "floor" };
  const engineBrick = {
    x: 1, y: 1, z: 3, blockId: "orange-wall", stateId: 1, mechanismDepth: 2,
  };
  const authoredBrick = {
    x: 1, y: 1, z: 1, blockId: "orange-wall", stateId: 1, mechanismDepth: 0,
  };
  assert.deepEqual(
    orangeWallVisualFrame({ voxels: [floor, engineBrick] }, definitions).voxels[1],
    authoredBrick,
  );
});

test("visual comparison hides flattened wall faces covered by a brick", () => {
  const floor = { x: 1, y: 1, z: 0, blockId: "floor" };
  const lowerWall = {
    x: 1, y: 1, z: 1, blockId: "orange-wall", stateId: 0, mechanismDepth: 1,
  };
  const upperWall = {
    x: 1, y: 1, z: 2, blockId: "orange-wall", stateId: 1, mechanismDepth: 1,
  };

  assert.deepEqual(
    orangeWallVisualFrame({ voxels: [floor, lowerWall, upperWall] }, definitions).voxels,
    [floor, { ...upperWall, z: 1, mechanismDepth: 0 }],
  );
});

test("visual comparison collapses coincident flattened wall faces", () => {
  const floor = { x: 1, y: 1, z: 0, blockId: "floor" };
  const first = {
    x: 1, y: 1, z: 1, blockId: "orange-wall", stateId: 0, mechanismDepth: 2,
  };
  const second = {
    x: 1, y: 1, z: 2, blockId: "orange-wall", stateId: 0, mechanismDepth: 2,
  };

  assert.equal(
    orangeWallVisualFrame({ voxels: [floor, first, second] }, definitions).voxels.length,
    2,
  );
});
