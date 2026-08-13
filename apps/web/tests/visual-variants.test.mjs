import assert from "node:assert/strict";
import test from "node:test";

import {
  normalizeSlopeDirection,
  offsetSlopeDirection,
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
