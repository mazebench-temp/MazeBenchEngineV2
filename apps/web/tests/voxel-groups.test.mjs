import assert from "node:assert/strict";
import test from "node:test";

import {
  moveVoxelGroup,
  removeSelectedVoxels,
  selectConnectedVoxelGroup,
  toggleVoxelGroupSelection,
} from "../app/voxelGroups.mjs";
import { cellObjectSelectionKey } from "../app/cellObjects.mjs";

const voxel = (x, y, z, blockId = "crate", genericId) =>
  genericId === undefined
    ? { x, y, z, blockId }
    : { x, y, z, blockId, genericId };

const selectionKeys = (voxels) => voxels.map(cellObjectSelectionKey);

test("selects only the six-neighbor component of the same exact type", () => {
  const voxels = [
    voxel(1, 1, 0, "generic", 4),
    voxel(2, 1, 0, "generic", 4),
    voxel(2, 1, 1, "generic", 4),
    voxel(3, 2, 1, "generic", 4), // diagonal
    voxel(2, 2, 1, "generic", 5), // touching, different generic ID
    voxel(1, 2, 0, "crate"), // touching, different block
  ];

  assert.deepEqual(
    new Set(selectConnectedVoxelGroup(voxels, { x: 1, y: 1, z: 0 })),
    new Set(selectionKeys(voxels.slice(0, 3))),
  );
});

test("connected selection separates authored state and variant values", () => {
  const first = { ...voxel(1, 1, 0, "switch"), stateId: 0, variantId: 2 };
  const same = { ...voxel(2, 1, 0, "switch"), stateId: 0, variantId: 2 };
  const otherState = { ...voxel(3, 1, 0, "switch"), stateId: 1, variantId: 2 };
  const otherVariant = { ...voxel(1, 2, 0, "switch"), stateId: 0, variantId: 3 };

  assert.deepEqual(
    new Set(selectConnectedVoxelGroup([first, same, otherState, otherVariant], first)),
    new Set(selectionKeys([first, same])),
  );
});

test("clicking an already selected group deselects only that group", () => {
  assert.deepEqual(
    toggleVoxelGroupSelection(
      ["1,1,0", "1,1,1", "5,5,0"],
      ["1,1,0", "1,1,1"],
      false,
    ),
    { deselected: true, keys: ["5,5,0"] },
  );

  assert.deepEqual(
    toggleVoxelGroupSelection(["5,5,0"], ["1,1,0", "1,1,1"], true),
    { deselected: false, keys: ["5,5,0", "1,1,0", "1,1,1"] },
  );
});

test("deletes every selected voxel while preserving unselected groups", () => {
  const result = removeSelectedVoxels(
    [voxel(1, 1, 0), voxel(1, 1, 1), voxel(4, 4, 0, "wall")],
    selectionKeys([voxel(1, 1, 0), voxel(1, 1, 1)]),
  );

  assert.equal(result.removedCount, 2);
  assert.deepEqual(result.voxels, [voxel(4, 4, 0, "wall")]);
});

test("moves a selected 3D component one horizontal cell", () => {
  const result = moveVoxelGroup(
    [voxel(1, 1, 0), voxel(1, 1, 1), voxel(4, 4, 0, "wall")],
    selectionKeys([voxel(1, 1, 0), voxel(1, 1, 1)]),
    1,
    0,
    { width: 6, height: 6 },
  );

  assert.equal(result.moved, true);
  assert.deepEqual(new Set(result.selectedKeys), new Set(selectionKeys([
    voxel(2, 1, 0), voxel(2, 1, 1),
  ])));
  assert.deepEqual(result.voxels, [voxel(2, 1, 0), voxel(2, 1, 1), voxel(4, 4, 0, "wall")]);
});

test("moves multiple disconnected selected groups together", () => {
  const result = moveVoxelGroup(
    [voxel(1, 1, 0, "crate"), voxel(3, 3, 0, "generic", 9), voxel(5, 5, 0, "wall")],
    selectionKeys([voxel(1, 1, 0, "crate"), voxel(3, 3, 0, "generic", 9)]),
    1,
    0,
    { width: 7, height: 7 },
  );

  assert.equal(result.moved, true);
  assert.deepEqual(new Set(result.selectedKeys), new Set(selectionKeys([
    voxel(2, 1, 0, "crate"), voxel(4, 3, 0, "generic", 9),
  ])));
  assert.deepEqual(result.voxels, [
    voxel(2, 1, 0, "crate"),
    voxel(4, 3, 0, "generic", 9),
    voxel(5, 5, 0, "wall"),
  ]);
});

test("raises and lowers a selected group on unbounded Z", () => {
  const raised = moveVoxelGroup(
    [voxel(2, 2, 0), voxel(2, 2, 1), voxel(4, 4, 0, "wall")],
    selectionKeys([voxel(2, 2, 0), voxel(2, 2, 1)]),
    0,
    0,
    { width: 6, height: 6 },
    1,
  );
  assert.equal(raised.moved, true);
  assert.deepEqual(new Set(raised.selectedKeys), new Set(selectionKeys([
    voxel(2, 2, 1), voxel(2, 2, 2),
  ])));

  const lowered = moveVoxelGroup(
    [voxel(2, 2, 0)],
    selectionKeys([voxel(2, 2, 0)]),
    0,
    0,
    { width: 6, height: 6 },
    -1,
  );
  assert.equal(lowered.moved, true);
  assert.deepEqual(lowered.selectedKeys, selectionKeys([voxel(2, 2, -1)]));
});

test("vertical group movement is blocked by an unselected cube", () => {
  const result = moveVoxelGroup(
    [voxel(2, 2, 0), voxel(2, 2, 1, "wall")],
    selectionKeys([voxel(2, 2, 0)]),
    0,
    0,
    { width: 6, height: 6 },
    1,
  );

  assert.equal(result.moved, false);
});

test("does not move a group beyond the room edge", () => {
  const voxels = [voxel(0, 1, 0), voxel(0, 1, 1)];
  const result = moveVoxelGroup(voxels, selectionKeys(voxels), -1, 0, { width: 6, height: 6 });

  assert.equal(result.moved, false);
  assert.deepEqual(result.voxels, voxels);
});

test("does not move a group into an unselected voxel", () => {
  const voxels = [voxel(1, 1, 0), voxel(2, 1, 0, "wall")];
  const result = moveVoxelGroup(voxels, selectionKeys([voxels[0]]), 1, 0, { width: 6, height: 6 });

  assert.equal(result.moved, false);
  assert.deepEqual(result.voxels, voxels);
});

test("moves a solid body onto a sensor without deleting either occupant", () => {
  const body = { ...voxel(1, 1, 1, "crate"), instanceId: "body" };
  const sensor = { ...voxel(2, 1, 1, "goal"), instanceId: "sensor" };
  const result = moveVoxelGroup(
    [body, sensor],
    [cellObjectSelectionKey(body)],
    1,
    0,
    { width: 6, height: 6 },
    0,
    new Set(["goal"]),
  );

  assert.equal(result.moved, true);
  assert.deepEqual(result.voxels, [{ ...body, x: 2 }, sensor]);
});
