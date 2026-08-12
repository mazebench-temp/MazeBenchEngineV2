import assert from "node:assert/strict";
import test from "node:test";

import {
  boxEntities,
  candidateSignature,
  exactBlockClusters,
  makeCandidate,
  mulberry32,
  reverseScrambleCandidate,
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

test("Ice-only 16-row seeds are dense and begin with a reachable gem", () => {
  const iceOnlyBlocks = blocks.filter((block) =>
    ["floor", "ice", "player", "gem"].includes(block.id));
  const candidate = makeCandidate({
    ...configuration,
    terrainDensity: 45,
    minWeightlessBoxes: 0,
    maxWeightlessBoxes: 0,
    evolveHoles: false,
    blocks: iceOnlyBlocks,
    enabledBlockIds: iceOnlyBlocks.map((block) => block.id),
    seed: 20260812,
  }, mulberry32(20260812));
  const ice = candidate.voxels.filter((voxel) => voxel.blockId === "ice");
  assert.ok(ice.length >= 100);
  assert.ok(ice.filter((voxel) => voxel.z === 0).length >= 40);
  assert.ok(ice.some((voxel) => voxel.z > 0));

  const player = candidate.voxels.find((voxel) => voxel.blockId === "player");
  const gem = candidate.voxels.find((voxel) => voxel.blockId === "gem");
  const rigid = new Set(candidate.voxels
    .filter((voxel) => voxel.blockId !== "player" && voxel.blockId !== "gem")
    .map((voxel) => `${voxel.x},${voxel.y},${voxel.z}`));
  const reachable = new Set([`${player.x},${player.y},${player.z}`]);
  const queue = [player];
  for (let head = 0; head < queue.length; head += 1) {
    const cell = queue[head];
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const key = `${cell.x + dx},${cell.y + dy},${cell.z}`;
      const support = `${cell.x + dx},${cell.y + dy},${cell.z - 1}`;
      if (!rigid.has(key) && rigid.has(support) && !reachable.has(key)) {
        reachable.add(key);
        queue.push({ x: cell.x + dx, y: cell.y + dy, z: cell.z });
      }
    }
  }
  assert.ok(reachable.has(`${gem.x},${gem.y},${gem.z}`));
});

test("candidate signatures deduplicate reordered copies of the same board", () => {
  const candidate = makeCandidate(configuration, mulberry32(19));
  const reordered = {
    ...candidate,
    voxels: [...candidate.voxels].reverse(),
  };
  assert.equal(candidateSignature(candidate), candidateSignature(reordered));
});

test("reverse scrambling pulls boxes away from a solved classic-room state", () => {
  const classicBlocks = blocks.filter((block) => block.id !== "ice");
  const voxels = [];
  for (let y = 0; y < 8; y += 1) {
    for (let x = 0; x < 8; x += 1) {
      voxels.push({ x, y, z: 0, blockId: "floor" });
      if (x === 0 || y === 0 || x === 7 || y === 7) {
        voxels.push({ x, y, z: 1, blockId: "wall" });
      }
    }
  }
  voxels.push(
    { x: 3, y: 3, z: 1, blockId: "player" },
    { x: 3, y: 3, z: 1, blockId: "gem" },
    { x: 4, y: 3, z: 1, blockId: "weightless", genericId: 0 },
  );
  const candidate = {
    id: "scramble-test",
    world: { width: 8, height: 8, floorLayer: 0 },
    layers: 3,
    voxels,
    solution: [],
  };
  const pulls = reverseScrambleCandidate(candidate, {
    ...configuration,
    width: 8,
    depth: 8,
    layers: 3,
    targetMoves: 50,
    blocks: classicBlocks,
    enabledBlockIds: classicBlocks.map((block) => block.id),
  }, mulberry32(7));
  assert.ok(pulls > 0);
  assert.deepEqual(
    candidate.voxels.find((voxel) => voxel.blockId === "gem"),
    { x: 3, y: 3, z: 1, blockId: "gem" },
  );
  assert.notDeepEqual(
    candidate.voxels.find((voxel) => voxel.blockId === "player"),
    { x: 3, y: 3, z: 1, blockId: "player" },
  );
});

test("one row above the floor never generates Row-2 voxels", () => {
  const candidate = makeCandidate({
    ...configuration,
    layers: 1,
    terrainDensity: 70,
    minWeightlessBoxes: 4,
    maxWeightlessBoxes: 4,
    seed: 31,
  }, mulberry32(31));
  assert.ok(candidate.voxels.every((voxel) => voxel.z <= 1));
  assert.ok(candidate.voxels.some((voxel) => voxel.z === 1));
});

test("initial terrain can occupy every horizontal perimeter", () => {
  const enabledBlocks = blocks.filter((block) => block.id !== "weightless");
  const candidate = makeCandidate({
    ...configuration,
    layers: 1,
    terrainDensity: 70,
    minWeightlessBoxes: 0,
    maxWeightlessBoxes: 0,
    blocks: enabledBlocks,
    enabledBlockIds: enabledBlocks.map((block) => block.id),
    seed: 31,
  }, mulberry32(31));
  const terrain = candidate.voxels.filter((voxel) =>
    voxel.blockId !== "floor" || voxel.z !== 0);
  assert.ok(terrain.some((voxel) => voxel.x === 0));
  assert.ok(terrain.some((voxel) => voxel.x === configuration.width - 1));
  assert.ok(terrain.some((voxel) => voxel.y === 0));
  assert.ok(terrain.some((voxel) => voxel.y === configuration.depth - 1));
});
