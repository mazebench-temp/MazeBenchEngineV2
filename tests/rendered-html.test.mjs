import assert from "node:assert/strict";
import test from "node:test";

async function render() {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);

  return worker.fetch(
    new Request("http://localhost/", { headers: { accept: "text/html" } }),
    { ASSETS: { fetch: async () => new Response("Not found", { status: 404 }) } },
    { waitUntil() {}, passThroughOnException() {} },
  );
}

test("server-renders the VoxelBench editor", async () => {
  const response = await render();
  assert.equal(response.status, 200);
  assert.match(response.headers.get("content-type") ?? "", /^text\/html\b/i);

  const html = await response.text();
  assert.match(html, /<title>VoxelBench/);
  assert.match(html, /PHYSICS WORKBENCH/i);
  assert.match(html, /Voxel Test Lab/);
  assert.match(html, /Block Definition/);
  assert.match(html, /Interactive MazeBench perspective polycube editor/);
  assert.match(html, /PERSPECTIVE · MAZEBENCH THREE/);
  assert.match(html, /Erase tool/);
  assert.match(html, /Undo paint/);
  assert.match(html, /Redo paint/);
  assert.match(html, /Reset room/);
  assert.match(html, /add to face/);
  assert.match(html, /Camera controls/);
  assert.match(html, /01<\/span> Start/);
  assert.match(html, /02<\/span> Expected/);
  assert.match(html, /Run suite/);
  assert.match(html, /Z is unbounded/);
  assert.doesNotMatch(html, /codex-preview|react-loading-skeleton|Your site is taking shape/i);
});
