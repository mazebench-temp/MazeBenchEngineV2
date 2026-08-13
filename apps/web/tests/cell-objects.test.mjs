import assert from "node:assert/strict";
import test from "node:test";

import {
  blockCanShareCell,
  cellObjectSemanticKey,
  diffObjectMultisets,
  eraseOneObjectAtCell,
  normalizeOccupancyProfile,
  placeObjectInCell,
} from "../app/cellObjects.mjs";

const definitions = new Map([
  ["box", { roleId: "pushable", occupancy: "solid" }],
  ["wall", { roleId: "solid", occupancy: "solid" }],
  ["gem", { roleId: "goal", occupancy: "sensor" }],
  ["button", { roleId: "button", occupancy: "sensor" }],
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
    at("button", { stateId: 1 }),
    definitions,
  );
  assert.deepEqual(result.objects, [at("box"), at("gem"), at("button", { stateId: 1 })]);
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
