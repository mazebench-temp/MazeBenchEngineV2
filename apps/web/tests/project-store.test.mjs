import assert from "node:assert/strict";
import { mkdtemp, readFile, readdir, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import {
  readProjectDirectory,
  writeProjectDirectory,
} from "../../../scripts/lib/project-store.mjs";

function makeTest(id, blockId) {
  return {
    id,
    name: id,
    description: "",
    folderId: "folder",
    locked: false,
    input: "up",
    world: { width: 16, height: 16, floorLayer: 0 },
    start: { voxels: [{ x: 1, y: 1, z: 0, blockId }] },
    intermediate: [],
    expected: { voxels: [{ x: 1, y: 2, z: 0, blockId }] },
  };
}

function makeProject(tests) {
  return {
    schemaVersion: 12,
    coordinateSystem: { floorLayer: 0 },
    roles: [],
    blocks: [],
    folders: [{ id: "folder", name: "Folder" }],
    searches: [],
    tests,
  };
}

test("split project store writes one compact file per test and removes stale files", async () => {
  const directory = await mkdtemp(join(tmpdir(), "voxelbench-project-store-"));
  try {
    const original = makeProject([
      makeTest("same/id", "player"),
      makeTest("same id", "box"),
    ]);
    const first = await writeProjectDirectory(directory, original);
    assert.equal(first.tests, 2);

    const manifest = JSON.parse(await readFile(join(directory, "project.json"), "utf8"));
    assert.equal(manifest.storageFormat, "voxelbench-split-project-v1");
    assert.equal(manifest.tests.length, 2);
    assert.notEqual(manifest.tests[0].file, manifest.tests[1].file);
    assert.equal((await readdir(join(directory, "tests"))).length, 2);

    const restored = await readProjectDirectory(new URL(`file://${directory}/`));
    assert.deepEqual(restored, original);

    const reduced = makeProject([original.tests[1]]);
    const second = await writeProjectDirectory(directory, reduced);
    assert.equal(second.staleTestsRemoved, 1);
    assert.equal((await readdir(join(directory, "tests"))).length, 1);
    assert.deepEqual(await readProjectDirectory(directory), reduced);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
