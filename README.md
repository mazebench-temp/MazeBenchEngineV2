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

The Search workspace runs a deterministic, MazeBenchEngine3-inspired entity-first evolutionary loop in a Web Worker. Choose any horizontal shape and vertical range that fits the C++ solver's 4,096-voxel scene capacity, along with initial terrain density, target command count, collectible count (one gem by default), population, generation budget, state limit, seed, allowed block definitions, and whether Row-0 holes can evolve. There is no longer an arbitrary 16-cell limit on width, depth, or height; search coordinates currently use the nonnegative 16-bit range. “Rows above floor: N” means the inclusive voxel range Row 0 through Row N; for example, `1` permits only Row 0 and Row 1, while `20` permits Row 20 but rejects Row 21. This ceiling is enforced after every mutation as well as during initial construction. Empty vertical space is never enumerated, so a tall sparse volume scales with its authored voxels rather than its bounding-box volume. Floor remains restricted to Row 0; when Floor is disabled but Ice is enabled, every Row-0 cell starts as Ice before optional holes are carved. When a weightless-pushable block is enabled, minimum and maximum settings control how many distinct polycube objects—with different numeric IDs—each candidate contains. Fresh candidates seed every numbered piece as a compact 2–5-cube domino-through-pentomino-scale polycube, matching MazeBenchEngine3's clean initial piece proportions. Two cubes is the search generator's lower bound, but there is no upper size cap: later mutations can keep growing a useful polycube until the selected volume or engine capacity is exhausted. Enabled wall terrain begins as a permanent Row-1 perimeter plus sparse interior walls and sparse vertical columns instead of an area-sized flat blob. Wall mutations now receive an explicit upward-growth opportunity in addition to connected 3D growth and shrinkage. Candidates may also begin with maze-like random-walk holes and several independent Ice clusters rooted on Row 0. Terrain, holes, actors, and polycubes use the entire horizontal grid, including all four perimeter coordinates. At the default 45% density, the deterministic 16×16 Ice-only seed contains more than 100 Ice cubes across Row 0 and raised layers. Ice, walls, and weightless polycubes can all gain or lose cubes; whole numbered pushbox polycubes can also be added or removed within the selected distinct-ID bounds. Generated boxes are rooted on the playable Row-1 surface, and ordinary seeds place the player and gems in one reachable component instead of stranding them on unrelated tower heights.

Classic non-Ice populations also include reverse-pull-scrambled seeds: they begin with the gem at a solved player position and apply inverse walks and box pushes, providing a known forward solution that actually interacts with polycubes. Evolution caches exact evaluations, rejects duplicate board hashes before reproduction, retains champions from several structural niches, injects fresh candidates, and increases mutation radius after stagnation. Walls, other terrain families, holes, the player-and-gem endpoint set, and each individual box receive entity-first mutation opportunity. Endpoint mutation relocates the player and all collectibles together onto a valid connected surface; unlike MazeBenchEngine3's current generator, endpoints can therefore improve inside a successful lineage instead of changing only when a fresh immigrant wins. Box mutation includes local movement, whole-shape relocation, compact reshaping, uncapped growth, and connected shrinkage. Fitness breaks equal-length solutions by required Ice/drop interactions and box pushes before considering raw search effort, mirroring MazeBenchEngine3's ordering. Up to four independent C++/WASM solver workers evaluate candidates concurrently while results are restored to population order, so a fixed seed remains reproducible regardless of which solver finishes first. Population defaults to MazeBenchEngine3's 256 candidates and can be raised to 1,024.

The live Search dashboard reports the incumbent's proven solution length explicitly and plots that best length once per generation. A separate live run clock identifies the active generation, and a second chart records the wall-clock duration of every completed generation with millisecond precision for fast runs. It reports attempted exact command simulations per second as its primary throughput metric, with global dynamic-body states per second alongside it. The C++ rate uses time spent inside the WASM solver; the end-to-end rate also includes JavaScript mutation, cloning, hashing, cache lookup, serialization, candidate construction, and parallel scheduling. Unique solves per second remains the most useful evolutionary metric for tiny searches. Both charts persist after a run completes so improvements, timing changes, and plateaus remain visible until the next evolution starts.

Each candidate is evaluated by one generalized C++ exact shortest-command solver. It runs uniform-cost Dijkstra (equivalently A* with a zero heuristic) over dynamic-body configurations while collapsing ordinary player reachability locally, so footsteps do not become global states. Every command uses the same arbitrary-3D physics semantics for Ice, holes, falling, walls, collectibles, riders, and polycubes. A shared prepared-scene accelerator handles exact player-only commands without creating a separate solver path; pushes and other body interactions fall through to the full tick kernel. A branch is pruned immediately when its player falls into the void and disappears. Solved candidates are ranked by the length of their proven optimum. Search records can be saved into the repo-backed project JSON, inspected in 3D, expanded into the engine's per-tick animation, and played with the arrow keys.

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
npm run benchmark:search:wasm
```

`benchmark:physics` reports both a prepared eight-voxel Ice push and the shared passive player-command accelerator. The native and WASM search benchmarks solve the same frozen 110-voxel, arbitrary-3D fixture at its proven 427-command optimum, replay the returned solution, and report exact solve latency plus clearly separated global-state, local-state, successor, and command-simulation rates. Native uses the host CPU instruction set while WASM is a generic browser target, so each is a platform baseline rather than a direct native-versus-WASM contest.
