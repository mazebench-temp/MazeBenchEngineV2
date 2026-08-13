# MazeBench Voxel Test Lab

A local visual unit-test editor for 3D, turn-based Sokoban-like mechanics. It uses MazeBenchEngine's perspective Three.js polycube renderer for authoring sparse 3D start frames and expected end frames, and executes turn physics in a C++ engine compiled to WebAssembly.

While the local development server is running, every project edit is atomically saved to `project-data/voxelbench-project.json`. That tracked repo file is loaded before the browser fallback, making authored visual tests directly available to Git and native C++ tooling.

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

Physics roles are project data with a stable internal key plus an editable display name and description. Blocks reference a role key, so renaming a role does not disconnect existing blocks. New roles can be created in the editor; their behavior is added explicitly in the focused C++ modules under `engine/src/physics/`.

All movement, collision, room-boundary, pushing, and Ice rules live in the C++ engine. `apps/web/app/physicsEngine.ts` only transfers frame and role data into the compiled WebAssembly module and reads the resulting coordinates back.

Ice follows MazeBenchEngine3 command semantics: a player entering Ice continues in the same direction until reaching normal floor or an obstacle, and a pushed object slides while supported by Ice.

Roles may be marked as a **generic numbered family**. Blocks using that role carry a non-negative object ID (`0`, `1`, `2`, and beyond), providing the data model needed for distinct weightless polycubes.

Generic IDs belong to painted voxels, not block definitions. Generic tools show `N` in the bottom toolbar; select one, type the desired ID in the prompt, and press Enter. Its toolbar cube shows that number while selected and returns to `N` when another tool is chosen. Every exposed face of the painted polycube displays its ID.

Dynamic entities cannot begin a command in midair. The tick engine first settles unsupported players and pushable cubes or polycubes one row per tick, then applies the requested horizontal input. A body above the bottomless void continues through the normal visible-fall and disappearance rules.

The built-in `goal` role is a literal non-rigid collectible gem. It occupies the same voxel space as moving bodies, does not support or block them, and is removed only when the player finishes an entire command on its exact coordinate. A box may overlap the gem without collecting it, and sliding across it mid-command leaves it in place.

## Evolutionary search

The Search workspace runs a deterministic, MazeBenchEngine3-inspired entity-first evolutionary loop in a Web Worker. Choose a 3D volume of up to 16 rows above the floor, initial terrain density, target command count, collectible count (one gem by default), population, generation budget, state limit, seed, allowed block definitions, and whether Row-0 holes can evolve. “Rows above floor: N” means the inclusive voxel range Row 0 through Row N; for example, `1` permits only Row 0 and Row 1. Floor remains restricted to Row 0; when Floor is disabled but Ice is enabled, every Row-0 cell starts as Ice before optional holes are carved. When a weightless-pushable block is enabled, minimum and maximum settings control how many distinct polycube objects—with different numeric IDs—each candidate contains. Fresh candidates seed every numbered piece as a compact 2–5-cube domino-through-pentomino-scale polycube, matching MazeBenchEngine3's clean initial piece proportions. Two cubes is the search generator's lower bound, but there is no upper size cap: later mutations can keep growing a useful polycube until the selected volume or engine capacity is exhausted. Enabled wall terrain begins as a permanent Row-1 perimeter plus sparse interior walls instead of area-sized random blobs; walls collectively receive one mutation opportunity and can still evolve upward in 3D. Candidates may also begin with maze-like random-walk holes and several independent Ice clusters rooted on Row 0. Terrain, holes, actors, and polycubes use the entire horizontal grid, including all four perimeter coordinates. At the default 45% density, the deterministic 16×16 Ice-only seed contains more than 100 Ice cubes across Row 0 and raised layers. Ice, walls, and weightless polycubes can all gain or lose cubes; whole numbered pushbox polycubes can also be added or removed within the selected distinct-ID bounds. Generated boxes are rooted on the playable Row-1 surface, and ordinary seeds place the player and gems in one reachable component instead of stranding them on unrelated tower heights.

Classic non-Ice populations also include reverse-pull-scrambled seeds: they begin with the gem at a solved player position and apply inverse walks and box pushes, providing a known forward solution that actually interacts with polycubes. Evolution caches exact evaluations, rejects duplicate board hashes before reproduction, retains champions from several structural niches, injects fresh candidates, and increases mutation radius after stagnation. Walls, other terrain families, holes, and each individual box receive entity-first mutation opportunity. Box mutation includes local movement, whole-shape relocation, compact reshaping, uncapped growth, and connected shrinkage; endpoints vary between fresh candidates rather than consuming the geometry mutation budget in every lineage. Fitness breaks equal-length solutions by required Ice/drop interactions and box pushes before considering raw search effort, mirroring MazeBenchEngine3's ordering. Up to four independent C++/WASM solver workers evaluate candidates concurrently while results are restored to population order, so a fixed seed remains reproducible regardless of which solver finishes first. Population defaults to MazeBenchEngine3's 256 candidates and can be raised to 1,024.

The live Search dashboard reports the incumbent's proven solution length explicitly and plots that best length once per generation. The chart persists after a run completes so improvements and plateaus remain visible until the next evolution starts.

Each candidate is evaluated by the C++ exact shortest-command solver. Non-Ice single-gem rooms use macro-move A*: ordinary reachable walking is collapsed locally, while globally stored edges contain the shortest walk to a push plus that push's exact command cost. Ice and multi-gem rooms retain exact command-state search because a command may slide several cells or alter partial collection state. Both paths store only moving entity translations while sharing immutable terrain. A branch is pruned immediately when its player falls into the void and disappears. The imported MazeBenchEngine3 16×16 classic regression is proven at the same 317-command optimum while expanding only 79 macro states. Solved candidates are ranked by the length of their proven optimum. Search records can be saved into the repo-backed project JSON, inspected in 3D, expanded into the engine's per-tick animation, and played with the arrow keys.

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
npm run benchmark:search
```

`benchmark:physics` measures a specialized flat-Ice turn workload. `benchmark:search` separately reports exact solves, expanded macro nodes, and generated successors for the 317-command 16×16 MazeBenchEngine3 regression; the rates are intentionally not presented as interchangeable.
