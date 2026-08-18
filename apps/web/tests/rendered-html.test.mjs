import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
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
  assert.match(html, /Physics Roles/);
  assert.match(html, /Stable engine key preserved/);
  assert.match(html, /C\+\+ engine/);
  assert.match(html, /Generic numbered family/);
  assert.match(html, /Select its N tool below, type an object ID, and press Enter before painting/);
  assert.doesNotMatch(html, /Generic object ID/);
  assert.match(html, /Ice/);
  assert.match(html, /Ice slope/);
  assert.match(html, /Outlined slope · 4 directions/);
  assert.match(html, /Interactive MazeBench perspective polycube editor/);
  assert.match(html, /Erase tool/);
  assert.match(html, /Block palette · Left and right arrows choose tools/);
  assert.match(html, /←\/→ chooses tools/);
  assert.match(html, /Select group tool/);
  assert.match(html, /Camera-relative arrows · Shift\+↑\/↓ changes layer · Delete erases/);
  assert.match(html, /Undo paint/);
  assert.match(html, /Redo paint/);
  assert.match(html, /Reset room/);
  assert.match(html, /canonical up/i);
  assert.match(html, /Description/);
  assert.match(html, /Moving Up pushes the single crate one cell onto the goal/);
  assert.match(html, /automatically checked as/);
  assert.match(html, /Camera controls/);
  assert.match(html, /Point camera north/);
  assert.match(html, /01<\/span> Start/);
  assert.match(html, /02<\/span> Expected/);
  assert.match(html, /Suite folder/);
  assert.match(html, /Movement &amp; walls/);
  assert.match(html, /Push boxes/);
  assert.match(html, />Test Suite<\/button>/);
  assert.match(html, /Open Test Suite/);
  assert.match(html, /Browse, search, reorder, and organize the complete test library on its own page/);
  assert.doesNotMatch(html, /aria-label="Collapse Movement &amp; walls"/);
  assert.match(html, /Run suite/);
  assert.match(html, /Z is unbounded/);
  assert.match(html, /Test World/);
  assert.match(html, /aria-label="Test room width draft"/);
  assert.match(html, /aria-label="Test room depth draft"/);
  assert.match(html, /aria-label="Test room width draft"[^>]*value="16"/);
  assert.match(html, /aria-label="Test room depth draft"[^>]*value="16"/);
  assert.match(html, /Save dimensions/);
  assert.match(html, /Cancel restores the saved values/);
  assert.match(html, /This size belongs only to/);
  assert.match(html, /other tests keep their own dimensions/);
  assert.match(html, /Shrinking crops this test(?:&#x27;|')s out-of-bounds voxels/);
  assert.doesNotMatch(html, /editable project-wide|out-of-bounds voxels from every test/);
  assert.doesNotMatch(html, /Start (?:→|-&gt;) expected/i);
  assert.doesNotMatch(html, /PERSPECTIVE · MAZEBENCH THREE|add to face|Restored local project/i);
  assert.doesNotMatch(html, /codex-preview|react-loading-skeleton|Your site is taking shape/i);
});

test("MazeBench renderer prints each generic ID on exposed polycube faces", async () => {
  const renderer = await readFile(
    new URL("../public/mazebench-runtime/play-render-three.js", import.meta.url),
    "utf8",
  );
  assert.match(renderer, /descriptor\.layer\?\.genericLabel/);
  assert.match(renderer, /addWeightlessGroupFaceLabels\([\s\S]*?descriptor\.layer\.genericLabel/);
  assert.match(renderer, /modelAssetsReady/);
  assert.match(renderer, /modelAssetsFailed/);
});

test("the suite uses proper lock icons and one lazy serialized 3D preview renderer", async () => {
  const [component, styles] = await Promise.all([
    readFile(new URL("../app/VoxelBench.tsx", import.meta.url), "utf8"),
    readFile(new URL("../app/globals.css", import.meta.url), "utf8"),
  ]);
  assert.match(component, /function LockIcon/);
  assert.match(component, /M7 11V7a5 5 0 0 1 10 0v4/);
  assert.match(component, /M7 11V7a5 5 0 0 1 9\.9-1/);
  assert.match(component, /new IntersectionObserver/);
  assert.match(component, /className="suite-preview-capture"[\s\S]*?<MazeBenchCanvas/);
  assert.match(component, /previewQueue/);
  assert.match(component, /suitePreviewFrames\(test\)/);
  assert.match(component, /cameraLayerRangeForFrames\(suitePreviewFrames\(previewTest\)\)/);
  assert.match(component, /cameraLayerRange=\{cameraLayerRangeForFrames/);
  assert.match(component, /className="suite-test-preview__pager"/);
  assert.match(component, /className="suite-test-row__details"/);
  assert.match(component, /onUpdateTest\(test\.id, \{ name: event\.target\.value \}\)/);
  assert.match(component, /onUpdateTest\(test\.id, \{ description: event\.target\.value \}\)/);
  assert.match(component, /Title[\s\S]*?disabled=\{folderLocked\}[\s\S]*?onUpdateTest\(test\.id, \{ name:/);
  assert.match(component, /Description[\s\S]*?disabled=\{folderLocked\}[\s\S]*?onUpdateTest\(test\.id, \{ description:/);
  assert.match(component, /if \(!test \|\| lockedFolderIds\.has\(test\.folderId\)\) return;/);
  assert.match(component, /className="suite-test-row__folder"[\s\S]*?disabled=\{lockedFolderIds\.has\(test\.folderId\)\}/);
  assert.match(component, /disabled=\{lockedFolderIds\.has\(test\.folderId\) \|\| folderIndex === 0\}/);
  assert.doesNotMatch(component, /Locked tests cannot be reordered/);
  assert.match(component, /Unlock both folders before moving this test/);
  assert.match(component, /Move \$\{test\.name\} left/);
  assert.match(component, /Move \$\{test\.name\} right/);
  assert.match(styles, /\.suite-test-table[^}]*grid-template-columns: repeat\(auto-fill, minmax\(320px, 1fr\)\)/);
  assert.match(styles, /\.suite-test-preview__scene img[^}]*object-fit: contain/);
  assert.match(styles, /\.suite-test-preview[^}]*grid-template-rows: minmax\(0, 1fr\) 34px/);
  assert.match(styles, /\.suite-test-row__identity[^}]*grid-template-columns/);
  assert.match(styles, /\.suite-test-row__details input:focus/);
  assert.match(styles, /button\.is-locked[^}]*color: var\(--amber\)/);
  assert.doesNotMatch(styles, /\.lock-glyph/);
});

test("non-cube editor meshes use exact full-cell picking proxies", async () => {
  const renderer = await readFile(
    new URL("../public/mazebench-runtime/play-render-three.js", import.meta.url),
    "utf8",
  );
  assert.match(renderer, /function addNonCubeEditorPickVolume/);
  assert.match(renderer, /addNonCubeEditorPickVolume\(center\.x, center\.z, baseY, editorPick\)/);
  assert.match(renderer, /descriptor\.isLoweredPlayerLift[\s\S]*?addNonCubeEditorPickVolume/);
  assert.match(renderer, /selectionKey: actor\.selectionKey/);
  assert.match(renderer, /selectionKey: cell\.selectionKey \|\| pick\.selectionKey/);
});

test("non-cube hover and selection are drawn on their real geometry", async () => {
  const [renderer, canvas] = await Promise.all([
    readFile(
      new URL("../public/mazebench-runtime/play-render-three.js", import.meta.url),
      "utf8",
    ),
    readFile(new URL("../app/MazeBenchCanvas.tsx", import.meta.url), "utf8"),
  ]);

  assert.match(renderer, /if \(editorHoverTarget\.highlightShape\) \{\s*return;/);
  assert.match(renderer, /function editorGeometryColor/);
  assert.match(renderer, /highlightShape: "surface"/);
  assert.match(renderer, /highlightShape: "geometry"/);
  assert.match(renderer, /terrainColor\(descriptor\.type, descriptor\)/);
  assert.match(renderer, /editorGeometryColor\("#f59e0b", selectionKey, selected\)/);
  assert.match(renderer, /actor\.selected === true/);
  assert.match(canvas, /selected: selectedVoxelKeys\.has\(cellObjectSelectionKey\(voxel\)\)/);
});

test("lowered orange walls are zero-thickness, paintable support faces", async () => {
  const renderer = await readFile(
    new URL("../public/mazebench-runtime/play-render-three.js", import.meta.url),
    "utf8",
  );
  assert.match(renderer, /isLoweredOrangeSurface\s*\n\s*\? 0/);
  assert.match(renderer, /supportSurface: descriptor\.isLoweredOrangeSurface/);
  assert.match(renderer, /rightSurface - leftSurface/);
});

test("lowered Orange Face numbers are drawn inside their top surfaces", async () => {
  const renderer = await readFile(
    new URL("../public/mazebench-runtime/play-render-three.js", import.meta.url),
    "utf8",
  );
  assert.match(renderer, /function addFlatOrangeSurfaceLabels/);
  assert.match(
    renderer,
    /descriptor\.isLoweredOrangeSurface[\s\S]*?addFlatOrangeSurfaceLabels\([\s\S]*?descriptor\.layer\.genericLabel/,
  );
  assert.match(renderer, /topY \+ normalOffset/);
});

test("Orange Face and Orange Cube rendering ignores remaining-rise depth", async () => {
  const canvas = await readFile(
    new URL("../app/MazeBenchCanvas.tsx", import.meta.url),
    "utf8",
  );
  assert.match(
    canvas,
    /definition\.visual\.kind === "orange-wall"[\s\S]*?orangeForm !== "face"/,
  );
  assert.match(canvas, /elevation: voxel\.z \+ layerOffset/);
  assert.doesNotMatch(canvas, /elevation: \(orangeWall\?\.physicalZ \?\? voxel\.z\)/);
});

test("hidden Orange Wall volumes are translucent only in editor rendering", async () => {
  const [canvas, renderer] = await Promise.all([
    readFile(new URL("../app/MazeBenchCanvas.tsx", import.meta.url), "utf8"),
    readFile(
      new URL("../public/mazebench-runtime/play-render-three.js", import.meta.url),
      "utf8",
    ),
  ]);
  assert.match(canvas, /orangeForm === "hidden"[\s\S]*?editorOnly: true[\s\S]*?editorOpacity/);
  assert.match(renderer, /layer\?\.editorOnly !== true \|\| isEditorRenderMode\(\)/);
  assert.match(renderer, /descriptor\.layer\?\.editorOpacity/);
});

test("the cell inspector imports its Orange Wall number formatter", async () => {
  const editor = await readFile(
    new URL("../app/VoxelBench.tsx", import.meta.url),
    "utf8",
  );
  assert.match(
    editor,
    /import \{[\s\S]*?orangeWallMechanismDepth[\s\S]*?\} from "\.\/orangeWalls\.mjs"/,
  );
  assert.match(editor, /rise \$\{orangeWallMechanismDepth\(voxel\)\}/);
});

test("the MazeBench GLTF loader has every local module dependency", async () => {
  const [loader, threeModule, threeCore, geometryUtils, skeletonUtils] = await Promise.all([
    readFile(new URL("../public/vendor/GLTFLoader.js", import.meta.url), "utf8"),
    readFile(new URL("../public/vendor/three.module.js", import.meta.url), "utf8"),
    readFile(new URL("../public/vendor/three.core.js", import.meta.url), "utf8"),
    readFile(new URL("../public/utils/BufferGeometryUtils.js", import.meta.url), "utf8"),
    readFile(new URL("../public/utils/SkeletonUtils.js", import.meta.url), "utf8"),
  ]);

  for (const source of [loader, geometryUtils, skeletonUtils]) {
    assert.match(source, /from '\/vendor\/three\.module\.js'/);
    assert.doesNotMatch(source, /from 'three'/);
  }
  assert.match(loader, /from '\.\.\/utils\/BufferGeometryUtils\.js'/);
  assert.match(loader, /from '\.\.\/utils\/SkeletonUtils\.js'/);
  assert.match(threeModule, /from '\.\/three\.core\.js'/);
  assert.match(threeCore, /const REVISION = '184'/);
});
