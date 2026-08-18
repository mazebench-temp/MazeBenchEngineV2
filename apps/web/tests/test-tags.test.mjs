import assert from "node:assert/strict";
import test from "node:test";

import {
  defaultSubtagId,
  directTagsAreLocked,
  directTagsIncludeTag,
  ensureDefaultSubtags,
  normalizeTestTagPlacement,
  rootTagId,
} from "../app/testTags.mjs";

const tagList = [
  { id: "physics", name: "Physics" },
  { default: true, id: "physics-default", name: "Default", parentId: "physics" },
  { id: "ice", name: "Ice", parentId: "physics" },
  { id: "slopes", name: "Slopes", parentId: "ice" },
  { id: "orange", name: "Orange" },
  { default: true, id: "orange-default", name: "Default", parentId: "orange" },
];
const tags = new Map([
  ["physics", tagList[0]],
  ["physics-default", tagList[1]],
  ["ice", tagList[2]],
  ["slopes", tagList[3]],
  ["orange", tagList[4]],
  ["orange-default", tagList[5]],
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

test("tag placement keeps only subtags from one parent group", () => {
  assert.deepEqual(normalizeTestTagPlacement({
    preferredTagId: "physics",
    directTagIds: ["slopes", "orange-default"],
  }, tagList), {
    groupTagId: "physics",
    tagIds: ["slopes"],
  });
});

test("root-only placement migrates into the group's Default subtag", () => {
  assert.deepEqual(normalizeTestTagPlacement({
    preferredTagId: "physics",
    directTagIds: ["physics"],
  }, tagList), {
    groupTagId: "physics",
    tagIds: ["physics-default"],
  });
  assert.equal(defaultSubtagId("physics", tagList), "physics-default");
  assert.equal(rootTagId("slopes", tags), "physics");
});

test("Default is removed when a real subtag is present", () => {
  assert.deepEqual(normalizeTestTagPlacement({
    preferredTagId: "physics",
    directTagIds: ["physics-default", "ice"],
  }, tagList).tagIds, ["ice"]);
});

test("every parent group receives exactly one stable Default subtag", () => {
  const seeded = ensureDefaultSubtags([
    { id: "one", name: "One" },
    { id: "two", name: "Two" },
    { id: "two-default", name: "Default", parentId: "two" },
  ]);
  assert.equal(defaultSubtagId("one", seeded), "one-default");
  assert.equal(defaultSubtagId("two", seeded), "two-default");
  assert.equal(seeded.filter((tag) => tag.default).length, 2);
  assert.deepEqual(ensureDefaultSubtags(seeded), seeded);
});
