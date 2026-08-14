import assert from "node:assert/strict";
import test from "node:test";

import {
  liftOrientationIndex,
  normalizeLiftOrientation,
  normalizeSlopeDirection,
  offsetSlopeDirection,
  rotateLiftMetadata,
  rotateSlopeMetadata,
  slopeDirectionIndex,
} from "../app/visualVariants.mjs";

test("ice slopes have four stable authored directions", () => {
  assert.equal(normalizeSlopeDirection("north"), "up");
  assert.equal(normalizeSlopeDirection(undefined, 1), "right");
  assert.equal(slopeDirectionIndex("south"), 2);
  assert.equal(offsetSlopeDirection("left", 1), "up");
});

test("slope rotation updates both orientation and numbered visual variant", () => {
  assert.deepEqual(
    rotateSlopeMetadata({ blockId: "slope", orientation: "left", variantId: 3 }, 2),
    { blockId: "slope", orientation: "right", variantId: 1 },
  );
  assert.deepEqual(
    rotateSlopeMetadata({ blockId: "ordinary-cube", orientation: "none", variantId: 7 }, 1),
    { blockId: "ordinary-cube", orientation: "none", variantId: 7 },
  );
});

test("player lifts support top plus four wall-facing variants but not downward", () => {
  assert.equal(normalizeLiftOrientation("upward"), "top");
  assert.equal(normalizeLiftOrientation("front"), "north");
  assert.equal(normalizeLiftOrientation("right"), "east");
  assert.equal(normalizeLiftOrientation("back"), "south");
  assert.equal(normalizeLiftOrientation("left"), "west");
  assert.equal(normalizeLiftOrientation("down", 0), "top");
  assert.equal(liftOrientationIndex("west"), 4);
});

test("lift rotations preserve top mounting and rotate wall mountings", () => {
  assert.deepEqual(
    rotateLiftMetadata({ blockId: "player-lift", orientation: "top", variantId: 0 }, 3),
    { blockId: "player-lift", orientation: "top", variantId: 0 },
  );
  assert.deepEqual(
    rotateLiftMetadata({ blockId: "player-lift", orientation: "north", variantId: 1 }, 1),
    { blockId: "player-lift", orientation: "east", variantId: 2 },
  );
  assert.deepEqual(
    rotateLiftMetadata({ blockId: "player-lift", orientation: "west", variantId: 4 }, 2),
    { blockId: "player-lift", orientation: "east", variantId: 2 },
  );
});
