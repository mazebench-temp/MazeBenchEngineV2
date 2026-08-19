import assert from "node:assert/strict";
import test from "node:test";

import { deleteTestCase, runnableTestCases } from "../app/testSuite.mjs";

const tests = [
  { folderId: "movement", id: "wall" },
  { folderId: "push", id: "push-one" },
  { folderId: "push", id: "push-two" },
  { folderId: "ice", id: "slide" },
];

test("deleting the active test selects the next test in its folder", () => {
  assert.deepEqual(deleteTestCase(tests, "push-one", "push-one"), {
    activeId: "push-two",
    removed: tests[1],
    tests: [tests[0], tests[2], tests[3]],
  });
});

test("deleting another test preserves the active test", () => {
  assert.equal(deleteTestCase(tests, "wall", "slide").activeId, "slide");
});

test("the final test cannot be deleted", () => {
  const finalTest = [{ folderId: "movement", id: "wall" }];
  assert.deepEqual(deleteTestCase(finalTest, "wall", "wall"), {
    activeId: "wall",
    removed: null,
    tests: finalTest,
  });
});

test("hidden cases are retained in the project but excluded from physics runs", () => {
  const cases = [
    { id: "implemented", hidden: false },
    { id: "future", hidden: true },
    { id: "legacy-without-flag" },
  ];
  assert.deepEqual(
    runnableTestCases(cases).map((item) => item.id),
    ["implemented", "legacy-without-flag"],
  );
  assert.equal(cases.length, 3);
});
