import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  cropVoxelsToWorld,
  rotateVoxelsClockwise,
  rotateWorldClockwise,
} from "../../apps/web/app/worldBounds.mjs";

const project = JSON.parse(await readFile(
  new URL("../../project-data/voxelbench-project.json", import.meta.url),
  "utf8",
));
const wasm = await readFile(
  new URL("../../apps/web/public/physics/voxel_physics.wasm", import.meta.url),
);
const { instance } = await WebAssembly.instantiate(wasm, {});
const engine = instance.exports;
const encoder = new TextEncoder();

function roleCode(roleId) {
  const bytes = encoder.encode(roleId);
  assert.ok(bytes.length <= engine.role_buffer_capacity());
  new Uint8Array(engine.memory.buffer, engine.role_buffer(), bytes.length).set(bytes);
  return engine.role_code(bytes.length);
}

const roleCodes = new Map(project.roles.map((role) => [role.id, roleCode(role.id)]));
const blockRoles = new Map(project.blocks.map((block) => [
  block.id,
  roleCodes.get(block.roleId) ?? 0,
]));
const genericRoles = new Set(project.roles.filter((role) => role.generic).map((role) => role.id));
const genericBlocks = new Set(
  project.blocks.filter((block) => genericRoles.has(block.roleId)).map((block) => block.id),
);

function simulateFrames(voxels, direction, world) {
  assert.equal(engine.physics_abi_version(), 3);
  const stride = engine.voxel_stride();
  assert.equal(stride, 5);
  assert.ok(voxels.length <= engine.voxel_capacity());
  const buffer = new Int32Array(
    engine.memory.buffer,
    engine.voxel_buffer(),
    voxels.length * stride,
  );
  voxels.forEach((voxel, index) => {
    buffer.set([
      voxel.x,
      voxel.y,
      voxel.z,
      blockRoles.get(voxel.blockId) ?? 0,
      genericBlocks.has(voxel.blockId) ? Math.max(0, Math.floor(voxel.genericId ?? 0)) : -1,
    ], index * stride);
  });
  const readFrame = () => voxels.map((voxel, index) => ({
    ...voxel,
    x: buffer[index * stride],
    y: buffer[index * stride + 1],
    z: buffer[index * stride + 2],
  }));
  const frames = [];
  let tick = 0;
  engine.reset_command();
  for (;;) {
    const status = engine.step_command_tick(
      voxels.length, world.width, world.height, direction);
    assert.ok(status === 0 || status === 1);
    if (engine.command_tick() !== tick) {
      tick = engine.command_tick();
      frames.push(readFrame());
    }
    if (status === 0) return frames;
  }
}

function identity(voxel) {
  return `${voxel.x},${voxel.y},${voxel.z}:${voxel.blockId}:${voxel.genericId ?? -1}`;
}

function summarize(voxels) {
  if (voxels.length === 0) return "none";
  const shown = voxels.slice(0, 8).map(identity).join("; ");
  return voxels.length > 8 ? `${shown}; +${voxels.length - 8} more` : shown;
}

function frameDifference(expected, actual, world) {
  const expectedMap = new Map(cropVoxelsToWorld(expected, world).map((voxel) => [identity(voxel), voxel]));
  const actualMap = new Map(cropVoxelsToWorld(actual, world).map((voxel) => [identity(voxel), voxel]));
  return {
    missing: [...expectedMap].filter(([key]) => !actualMap.has(key)).map(([, voxel]) => voxel),
    unexpected: [...actualMap].filter(([key]) => !expectedMap.has(key)).map(([, voxel]) => voxel),
  };
}

for (const authoredTest of project.tests) {
  test(`authored C++ suite: ${authoredTest.name}`, () => {
    const failures = [];
    for (let quarterTurns = 0; quarterTurns < 4; quarterTurns += 1) {
      const world = rotateWorldClockwise(authoredTest.world, quarterTurns);
      const start = rotateVoxelsClockwise(authoredTest.start.voxels, authoredTest.world, quarterTurns);
      const expected = rotateVoxelsClockwise(authoredTest.expected.voxels, authoredTest.world, quarterTurns);
      const expectedIntermediate = (authoredTest.intermediate ?? []).map((frame) =>
        rotateVoxelsClockwise(frame.voxels, authoredTest.world, quarterTurns));
      const actualFrames = simulateFrames(start, quarterTurns, world);
      const expectedTickCount = expectedIntermediate.length + 1;
      if (actualFrames.length !== expectedTickCount) {
        failures.push(
          `${quarterTurns * 90}°: expected ${expectedTickCount} tick frame(s), ` +
          `engine produced ${actualFrames.length}`,
        );
      }
      for (let index = 0; index < expectedIntermediate.length; index += 1) {
        const actualTick = actualFrames[index];
        if (!actualTick) {
          failures.push(`${quarterTurns * 90}° tick ${index + 1}: engine trace ended early`);
          break;
        }
        const tickDifference = frameDifference(expectedIntermediate[index], actualTick, world);
        if (tickDifference.missing.length || tickDifference.unexpected.length) {
          failures.push(
            `${quarterTurns * 90}° tick ${index + 1}: missing ${summarize(tickDifference.missing)} | ` +
            `unexpected ${summarize(tickDifference.unexpected)}`,
          );
          break;
        }
      }
      const actual = actualFrames.at(-1) ?? start;
      const difference = frameDifference(expected, actual, world);
      if (difference.missing.length || difference.unexpected.length) {
        failures.push(
          `${quarterTurns * 90}°: missing ${summarize(difference.missing)} | ` +
          `unexpected ${summarize(difference.unexpected)}`,
        );
      }
    }
    assert.equal(
      failures.length,
      0,
      `${authoredTest.description || authoredTest.name}\n${failures.join("\n")}`,
    );
  });
}
