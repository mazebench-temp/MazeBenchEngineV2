import assert from "node:assert/strict";
import test from "node:test";

import {
  blockCanShareCell,
  cellObjectSemanticKey,
  diffObjectMultisets,
  eraseOneObjectAtCell,
  normalizeOccupancyProfile,
  objectCanShareCell,
  placeObjectInCell,
} from "../app/cellObjects.mjs";

const definitions = new Map([
  ["box", { roleId: "pushable", occupancy: "solid" }],
  ["wall", { roleId: "solid", occupancy: "solid" }],
  ["gem", { roleId: "goal", occupancy: "sensor" }],
  ["button", { roleId: "button", occupancy: "sensor" }],
  ["hidden-button", {
    roleId: "orange-button",
    occupancy: "inactive",
    visual: { kind: "button", buttonForm: "hidden" },
  }],
  ["orange-wall", {
    roleId: "orange-wall",
    occupancy: "solid",
    visual: { kind: "orange-wall", orangeForm: "cube" },
  }],
  ["orange-face", {
    roleId: "orange-wall",
    occupancy: "support",
    visual: { kind: "orange-wall", orangeForm: "face" },
  }],
  ["orange-hidden", {
    roleId: "orange-wall",
    occupancy: "inactive",
    visual: { kind: "orange-wall", orangeForm: "hidden" },
  }],
]);

const at = (blockId, extras = {}) => ({ x: 2, y: 3, z: 1, blockId, ...extras });

test("legacy goals migrate to pass-through sensors", () => {
  assert.equal(normalizeOccupancyProfile(undefined, "goal"), "sensor");
  assert.equal(normalizeOccupancyProfile(undefined, "solid"), "solid");
  assert.equal(blockCanShareCell({ roleId: "goal" }), true);
});

test("a sensor joins an occupied solid cell without replacing the body", () => {
  const result = placeObjectInCell([at("box")], at("gem"), definitions);
  assert.equal(result.changed, true);
  assert.deepEqual(result.objects, [at("box"), at("gem")]);
});

test("a solid joins a sensor-only cell but replaces another solid", () => {
  const withSensor = placeObjectInCell([at("gem")], at("box"), definitions);
  assert.deepEqual(withSensor.objects, [at("gem"), at("box")]);

  const replacement = placeObjectInCell(withSensor.objects, at("wall"), definitions);
  assert.deepEqual(replacement.objects, [at("gem"), at("wall")]);
});

test("multiple distinct sensors may share a cell with one body", () => {
  const result = placeObjectInCell(
    [at("box"), at("gem")],
    at("button"),
    definitions,
  );
  assert.deepEqual(result.objects, [at("box"), at("gem"), at("button")]);
});

test("differently oriented buttons may share one cell", () => {
  const north = at("button", { orientation: "north", variantId: 1 });
  const east = at("button", { orientation: "east", variantId: 2 });
  const result = placeObjectInCell([north], east, definitions);
  assert.equal(result.changed, true);
  assert.deepEqual(result.objects, [north, east]);
});

test("an invisible Orange Button may share a solid object's cell", () => {
  const hidden = at("hidden-button", { orientation: "east", variantId: 2 });
  const result = placeObjectInCell([at("box")], hidden, definitions);
  assert.equal(result.changed, true);
  assert.deepEqual(result.objects, [at("box"), hidden]);
});

test("Orange Faces and hidden volumes are shareable regardless of number", () => {
  const cubeDefinition = definitions.get("orange-wall");
  const faceDefinition = definitions.get("orange-face");
  const hiddenDefinition = definitions.get("orange-hidden");
  assert.equal(objectCanShareCell(cubeDefinition, at("orange-wall", { stateId: 0 })), false);
  assert.equal(objectCanShareCell(cubeDefinition, at("orange-wall", { stateId: 1 })), false);
  assert.equal(objectCanShareCell(faceDefinition, at("orange-face", { stateId: 0 })), true);
  assert.equal(objectCanShareCell(faceDefinition, at("orange-face", { stateId: 1 })), true);
  assert.equal(objectCanShareCell(hiddenDefinition, at("orange-hidden", { stateId: 2 })), true);

  const lowered = at("orange-face", { stateId: 0, mechanismDepth: 2 });
  const result = placeObjectInCell(
    [lowered],
    at("box"),
    definitions,
  );
  assert.deepEqual(result.objects, [lowered, at("box")]);

  const buried = at("orange-hidden", { stateId: 2, mechanismDepth: 4 });
  const buriedResult = placeObjectInCell([at("wall")], buried, definitions);
  assert.deepEqual(buriedResult.objects, [at("wall"), buried]);
});

test("repainting an Orange Face replaces its remaining-rise number", () => {
  const definitions = new Map([
    ["orange-face", {
      roleId: "orange-wall",
      occupancy: "support",
      visual: { kind: "orange-wall", orangeForm: "face" },
    }],
  ]);
  const existing = at("orange-face", { stateId: 0, mechanismDepth: 1 });
  const replacement = at("orange-face", { stateId: 0, mechanismDepth: 4 });
  const result = placeObjectInCell([existing], replacement, definitions);
  assert.equal(result.changed, true);
  assert.deepEqual(result.objects, [replacement]);
});

test("semantic identity separates group, variant, state, and orientation", () => {
  const base = at("mechanism", { groupId: 3, variantId: 1, stateId: 0, orientation: "north" });
  assert.notEqual(cellObjectSemanticKey(base), cellObjectSemanticKey({ ...base, stateId: 1 }));
  assert.notEqual(cellObjectSemanticKey(base), cellObjectSemanticKey({ ...base, variantId: 2 }));
  assert.notEqual(cellObjectSemanticKey(base), cellObjectSemanticKey({ ...base, orientation: "east" }));
  assert.equal(
    cellObjectSemanticKey({ ...base, groupId: undefined, genericId: 3 }),
    cellObjectSemanticKey(base),
  );
});

test("frame differences count duplicate occupants instead of collapsing them", () => {
  const gem = at("gem");
  const difference = diffObjectMultisets([gem, { ...gem }], [gem]);
  assert.deepEqual(difference.missing, [gem]);
  assert.deepEqual(difference.unexpected, []);
});

test("erase removes one occupant and preserves the rest of the cell", () => {
  const result = eraseOneObjectAtCell(
    [at("box"), at("gem"), at("button")],
    { x: 2, y: 3, z: 1 },
  );
  assert.equal(result.changed, true);
  assert.deepEqual(result.removed, at("button"));
  assert.deepEqual(result.objects, [at("box"), at("gem")]);
});

test("erase may target the exact custom occupant picked inside an overlap", () => {
  const box = at("box", { instanceId: "box-a" });
  const gem = at("gem", { instanceId: "gem-a" });
  const result = eraseOneObjectAtCell(
    [box, gem],
    { x: 2, y: 3, z: -20 },
    "instance:gem-a",
  );
  assert.deepEqual(result.removed, gem);
  assert.deepEqual(result.objects, [box]);
});
