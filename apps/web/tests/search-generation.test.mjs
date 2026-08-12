import assert from "node:assert/strict";
import test from "node:test";

import {
  boxEntities,
  exactBlockClusters,
  makeCandidate,
  mulberry32,
  shrinkGenericBox,
  shrinkStaticCluster,
} from "../public/search-worker.js";

const blocks = [
  { id: "floor", roleId: "floor" },
  { id: "wall", roleId: "solid" },
  { id: "ice", roleId: "ice" },
  { id: "player", roleId: "player" },
  { id: "gem", roleId: "goal" },
  { id: "weightless", roleId: "weightless-pushable" },
];

const configuration = {
  width: 16,
  depth: 16,
  layers: 16,
  collectibles: 1,
  minWeightlessBoxes: 4,
  maxWeightlessBoxes: 4,
  evolveHoles: true,
  blocks,
  roles: [],
  enabledBlockIds: blocks.map((block) => block.id),
  seed: 1,
};

test("search seeds distinct uncapped weightless polycubes into 3D terrain", () => {
  const candidate = makeCandidate(configuration, mulberry32(configuration.seed));
  const boxes = boxEntities(candidate.voxels, [blocks.at(-1)]);
  assert.deepEqual(boxes.map((members) => members[0].genericId), [0, 1, 2, 3]);
  assert.ok(boxes.some((members) => members.length > 12));

  const floors = candidate.voxels.filter((voxel) => voxel.blockId === "floor");
  const ice = candidate.voxels.filter((voxel) => voxel.blockId === "ice");
  const walls = candidate.voxels.filter((voxel) => voxel.blockId === "wall");
  assert.ok(floors.every((voxel) => voxel.z === 0));
  assert.ok(ice.some((voxel) => voxel.z === 0));
  assert.ok(ice.some((voxel) => voxel.z > 0));
  assert.ok(walls.every((voxel) => voxel.z >= 1));
  assert.ok(exactBlockClusters(candidate.voxels, new Set(["ice"])).length >= 2);
  assert.ok(exactBlockClusters(candidate.voxels, new Set(["wall"])).length >= 2);

  const rowZeroTerrain = candidate.voxels.filter((voxel) =>
    voxel.z === 0 && (voxel.blockId === "floor" || voxel.blockId === "ice"));
  assert.ok(rowZeroTerrain.length < configuration.width * configuration.depth);
});

test("polycube mutations can remove non-leaf cubes without disconnecting the blob", () => {
  const generic = [
    { x: 1, y: 1, z: 1, blockId: "weightless", genericId: 7 },
    { x: 2, y: 1, z: 1, blockId: "weightless", genericId: 7 },
    { x: 1, y: 2, z: 1, blockId: "weightless", genericId: 7 },
    { x: 2, y: 2, z: 1, blockId: "weightless", genericId: 7 },
  ];
  assert.equal(shrinkGenericBox(generic, [...generic], () => 0), true);
  assert.equal(generic.length, 3);

  const structure = [
    { x: 1, y: 1, z: 1, blockId: "wall" },
    { x: 2, y: 1, z: 1, blockId: "wall" },
    { x: 1, y: 2, z: 1, blockId: "wall" },
    { x: 2, y: 2, z: 1, blockId: "wall" },
  ];
  assert.equal(shrinkStaticCluster(
    structure,
    [...structure],
    () => 0,
    blocks[0],
  ), true);
  assert.equal(structure.length, 3);
});
