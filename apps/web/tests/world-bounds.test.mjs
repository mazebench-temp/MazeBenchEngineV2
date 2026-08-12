import assert from "node:assert/strict";
import test from "node:test";

import {
  applyWorldToTest,
  cropVoxelsToWorld,
  isVoxelInsideWorld,
  parseWorldDimensionDraft,
  rotateVoxelsClockwise,
  rotateWorldClockwise,
} from "../app/worldBounds.mjs";

test("dimension drafts are unrestricted until explicitly validated for save", () => {
  assert.equal(parseWorldDimensionDraft("12"), 12);
  assert.equal(parseWorldDimensionDraft(" 7 "), 7);
  assert.equal(parseWorldDimensionDraft(""), null);
  assert.equal(parseWorldDimensionDraft("anything"), null);
  assert.equal(parseWorldDimensionDraft("12.5"), null);
  assert.equal(parseWorldDimensionDraft("2"), null);
  assert.equal(parseWorldDimensionDraft("33"), null);
});

test("each test owns a world shared by its Start and Expected frames", () => {
  const first = {
    id: "small-room",
    start: { voxels: [{ x: 1, y: 1, z: 0 }, { x: 4, y: 1, z: 0 }] },
    intermediate: [{ voxels: [{ x: 1, y: 1, z: 0 }, { x: 5, y: 1, z: 0 }] }],
    expected: { voxels: [{ x: 2, y: 1, z: 0 }, { x: 4, y: 1, z: 0 }] },
  };
  const second = {
    id: "large-room",
    world: { width: 8, height: 7, floorLayer: 0 },
    start: { voxels: [{ x: 7, y: 6, z: 0 }] },
    expected: { voxels: [{ x: 7, y: 6, z: 0 }] },
  };

  const resizedFirst = applyWorldToTest(first, { width: 3, height: 2 });

  assert.deepEqual(resizedFirst.world, { width: 3, height: 2, floorLayer: 0 });
  assert.deepEqual(resizedFirst.start.voxels, [{ x: 1, y: 1, z: 0 }]);
  assert.deepEqual(resizedFirst.intermediate[0].voxels, [{ x: 1, y: 1, z: 0 }]);
  assert.deepEqual(resizedFirst.expected.voxels, [{ x: 2, y: 1, z: 0 }]);
  assert.deepEqual(second.world, { width: 8, height: 7, floorLayer: 0 });
  assert.equal(second.start.voxels.length, 1);
});

test("shrinking a room removes stale horizontal voxels but keeps every vertical layer", () => {
  const world = { width: 3, height: 2 };
  const inside = [
    { x: 0, y: 0, z: 0, blockId: "floor" },
    { x: 2, y: 1, z: -12, blockId: "crate" },
    { x: 1, y: 0, z: 300, blockId: "wall" },
  ];
  const stale = Array.from({ length: 269 }, (_, index) => ({
    x: 3 + index,
    y: index % 2,
    z: index - 134,
    blockId: "floor",
  }));

  const cropped = cropVoxelsToWorld([...inside, ...stale], world);

  assert.deepEqual(cropped, inside);
  assert.equal(stale.some((voxel) => isVoxelInsideWorld(voxel, world)), false);
});

test("negative horizontal coordinates are outside the room", () => {
  const world = { width: 8, height: 7 };
  assert.equal(isVoxelInsideWorld({ x: -1, y: 0 }, world), false);
  assert.equal(isVoxelInsideWorld({ x: 0, y: -1 }, world), false);
});

test("clockwise level rotations also rotate rectangular room bounds", () => {
  const world = { width: 3, height: 2 };
  const northEdge = [{ x: 0, y: 0, z: -4, blockId: "player", genericId: 712 }];

  assert.deepEqual(rotateWorldClockwise(world, 1), { width: 2, height: 3 });
  assert.deepEqual(rotateVoxelsClockwise(northEdge, world, 1), [
    { x: 1, y: 0, z: -4, blockId: "player", genericId: 712 },
  ]);
  assert.deepEqual(rotateVoxelsClockwise(northEdge, world, 2), [
    { x: 2, y: 1, z: -4, blockId: "player", genericId: 712 },
  ]);
  assert.deepEqual(rotateVoxelsClockwise(northEdge, world, 3), [
    { x: 0, y: 2, z: -4, blockId: "player", genericId: 712 },
  ]);
});
