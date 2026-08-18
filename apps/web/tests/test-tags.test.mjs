import assert from "node:assert/strict";
import test from "node:test";

import {
  directTagsAreLocked,
  directTagsIncludeTag,
} from "../app/testTags.mjs";

const tags = new Map([
  ["physics", { id: "physics" }],
  ["ice", { id: "ice", parentId: "physics" }],
  ["slopes", { id: "slopes", parentId: "ice" }],
  ["orange", { id: "orange" }],
]);

test("a direct subtag assignment is visible through every parent tag", () => {
  assert.equal(directTagsIncludeTag(["slopes"], "slopes", tags), true);
  assert.equal(directTagsIncludeTag(["slopes"], "ice", tags), true);
  assert.equal(directTagsIncludeTag(["slopes"], "physics", tags), true);
  assert.equal(directTagsIncludeTag(["slopes"], "orange", tags), false);
});

test("multiple direct tags independently contribute inherited membership", () => {
  assert.equal(directTagsIncludeTag(["slopes", "orange"], "physics", tags), true);
  assert.equal(directTagsIncludeTag(["slopes", "orange"], "orange", tags), true);
});

test("any effectively locked direct tag locks organization for the test", () => {
  assert.equal(directTagsAreLocked(["slopes", "orange"], new Set(["orange"])), true);
  assert.equal(directTagsAreLocked(["slopes"], new Set(["orange"])), false);
});
