# MazeBench Voxel Test Lab

A local visual unit-test editor for 3D, turn-based Sokoban-like mechanics. It uses MazeBenchEngine's perspective Three.js polycube renderer for authoring sparse 3D start frames and expected end frames, and executes turn physics in a C++ engine compiled to WebAssembly.

New projects begin with a 16 × 16 × ∞ test world. Existing tests retain their individually saved dimensions.

## Run locally

```bash
npm install
npm run dev
```

Open [http://localhost:3000](http://localhost:3000).

## Editor controls

- Click a cube face to paint; drag stays locked to the first logical layer.
- Select the eraser or press `E` to remove visible cubes.
- Press `←` / `→` to move through the toolbar. On a selected generic block, the arrows decrement or increment its object ID instead; pressing `←` at ID 0 moves to the toolbar item on its left.
- Press `G` once (or click the Select group toolbar tile), then click a cube to select every face-touching cube of the exact same block type and generic ID. Press `G` again to clear all highlighted groups without leaving Group mode. Click a selected group again to deselect it. Hold Shift and drag a visible rectangle to add every group touched by the rectangle. Arrow keys move all selected 3D groups together by one screen-relative cell, adjusted to the camera's nearest quarter-turn; collisions and room edges block the complete move. Press `Shift+↑` to raise selected groups one Z layer or `Shift+↓` to lower them one layer. Press `Delete` or `Backspace` to erase all selected groups as one undoable edit.
- Hold `W` / `S` to tilt smoothly through top, side, and underside views.
- Press `A` / `D` for eased quarter-turn rotation.
- Press `N` or click the center compass button to point the camera north.
- Shift-drag to orbit and scroll to zoom.
- Press `Cmd/Ctrl+Z` to undo a paint stroke and `Cmd/Ctrl+Shift+Z` to redo it.
- Reset room restores the active frame to row-0 floor tiles and is undoable.
- Tests are authored as Up and automatically checked in all four level rotations.
- Organize tests in persistent, renameable suite folders such as Push boxes and Ice.
- Use the Suite folder field to move a test, or a folder's `+` button to create one there.
- Collapse a suite folder with its disclosure triangle.
- Lock an individual test to protect its name, folder assignment, and both frames; lock a suite folder to protect every test inside it.
- Give every test a description of its intended transition and invariants so debugging agents have explicit behavioral context.
- Reorder tests within a suite folder, duplicate a test with its description and both authored frames, or delete an unlocked test after confirmation. The final remaining test is retained so the editor always has an active room.
- Expected frames can be replaced with an undoable copy of their start frame.
- Every test owns its width and depth. Start and Expected share that test-local size and resize together; shrinking permanently crops out-of-bounds voxels only from that test. Duplicated and newly derived tests inherit the source test's dimensions.
- Test World dimensions are drafts: typing never changes the room. Save dimensions validates and applies both fields together; Cancel restores the saved values. Valid dimensions are whole numbers from 3 through 32.

Logical Z is stored sparsely and is unbounded in both directions. Empty-hole painting starts at row 0.

## Physics roles and engine

Physics roles are project data with a stable internal key plus an editable display name and description. Blocks reference a role key, so renaming a role does not disconnect existing blocks. New roles can be created in the editor; their behavior is added explicitly in `engine/src/physics.cpp`.

All movement, collision, room-boundary, pushing, and Ice rules live in the C++ engine. `apps/web/app/physicsEngine.ts` only transfers frame and role data into the compiled WebAssembly module and reads the resulting coordinates back.

Ice follows MazeBenchEngine3 command semantics: a player entering Ice continues in the same direction until reaching normal floor or an obstacle, and a pushed object slides while supported by Ice.

Roles may be marked as a **generic numbered family**. Blocks using that role carry a non-negative object ID (`0`, `1`, `2`, and beyond), providing the data model needed for distinct weightless polycubes.

Generic IDs belong to painted voxels, not block definitions. Generic tools show `N` in the bottom toolbar; select one, type the desired ID in the prompt, and press Enter. Its toolbar cube shows that number while selected and returns to `N` when another tool is chosen. Every exposed face of the painted polycube displays its ID.

## Repository layout

- `engine/`: shared platform-independent C++ library, tests, WebAssembly API, and benchmarks
- `apps/web/`: browser editor, renderer integration, and web tests
- `apps/apple/`: reserved home for the future iOS/macOS client
- `scripts/`: repository-level build, test, and benchmark commands

## Verification

```bash
npm test
npm run lint
```

Every physics change must finish with a clean `npm test`. Record a performance baseline with:

```bash
npm run benchmark:physics
```
