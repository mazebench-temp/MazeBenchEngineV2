import assert from "node:assert/strict";
import test from "node:test";

import {
  cameraRelativeDirection,
  cameraYawQuarterTurns,
} from "../app/cameraNavigation.mjs";

test("camera yaw snaps to the nearest normalized quarter turn", () => {
  assert.equal(cameraYawQuarterTurns(0), 0);
  assert.equal(cameraYawQuarterTurns(Math.PI / 2), 1);
  assert.equal(cameraYawQuarterTurns(-Math.PI / 2), 3);
  assert.equal(cameraYawQuarterTurns(Math.PI * 2), 0);
});

test("screen arrows follow the rotated camera", () => {
  assert.equal(cameraRelativeDirection("up", 0), "up");
  assert.equal(cameraRelativeDirection("up", 1), "left");
  assert.equal(cameraRelativeDirection("right", 1), "up");
  assert.equal(cameraRelativeDirection("down", 2), "up");
  assert.equal(cameraRelativeDirection("left", 3), "up");
});
