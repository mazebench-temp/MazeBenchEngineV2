# Object, State, Overlap, and Custom-Shape Roadmap

This checklist turns the approved object-system plan into staged, testable work.
Every C++ physics/search change must be followed by the complete native, WASM,
web, and authored-project test suite. Search benchmarks must also be rerun when
scene storage, occupancy indexes, hashing, or transitions change.

## Phase 1 — Versioned object schema

- [x] Add a project/schema version for generalized cell objects.
- [x] Preserve the existing `voxels` JSON shape through a backward-compatible migration.
- [x] Give every newly placed object a stable `instanceId` for editing and animation.
- [ ] Lazily assign stable IDs to ambiguous overlapping legacy records without inflating ordinary floor data.
- [ ] Rename the meaning of the current numbered polycube value to `groupId` internally while accepting legacy `genericId` imports.
- [x] Add independent `variantId` storage for authored shape/style variants.
- [x] Add independent mutable `stateId` storage for off/on and N-state mechanics.
- [x] Add independent discrete orientation storage (the 24-rotation authoring UI remains pending).
- [ ] Add block-level occupancy/support metadata with state/variant overrides.
- [x] Keep Floor restricted to Row 0 after migration.
- [x] Round-trip old and new project files without data loss.
- [x] Keep repo autosave working when the project exceeds browser storage quota.

## Phase 2 — Multi-occupancy editor and visual tests

- [x] Replace coordinate-uniqueness assumptions with count-aware cell contents.
- [x] Allow sensor/pass-through objects to share a cell with solid bodies.
- [x] Prevent incompatible solid-volume overlaps without deleting sensors.
- [x] Let Group movement move players/boxes onto sensors.
- [x] Add a Cell Contents inspector for selecting one occupant at a coordinate.
- [x] Add an explicit “place in this cell” action for hidden/occupied targets.
- [x] Let erase/delete remove one selected occupant without clearing the cell.
- [x] Add an explicit clear-cell action.
- [ ] Preserve overlaps through undo/redo, tick copying, duplication, reset, resize, import, and export.
- [x] Make connected-group selection use group, variant, state, and orientation correctly.
- [ ] Show a stack-count marker for cells with multiple occupants.
- [ ] Add a sensor/X-ray display mode for hidden gems, buttons, and triggers.
- [x] Compare frames as semantic multisets rather than ordered arrays.
- [ ] Report occupant-level missing/unexpected differences.

## Phase 3 — States, variants, and rotation

- [ ] Add block-definition controls for a bounded state list.
- [ ] Add state names, symbols, colors/assets, initial state, and collision/support profile.
- [x] Add the first block-definition control for bounded lift mounting variants.
- [x] Keep lift authoring binary (`0` lowered / `1` raised) and derive the full directional ID from the clicked cube face.
- [x] Add a four-direction selector and hotbar workflow for the first bounded Ice-slope variant.
- [ ] Reuse the numbered toolbar entry workflow for bounded state/variant selection.
- [x] Keep `groupId`, `variantId`, `stateId`, mechanism depth, and orientation independently editable.
- [x] Add per-variant rotation mappings for Ice slopes and the five supported lift mountings.
- [x] Rotate authored Ice-slope direction metadata through all four automatic test rotations.
- [x] Rotate positions and authored horizontal orientations in all four automatic unit-test rotations.
- [ ] Show state transitions directly in expected-versus-engine differences.
- [ ] Materialize state values in generated intermediate tick frames.
- [ ] Reserve state scope/link metadata for instance, polycube group, and mechanism channel behavior.

## Phase 4 — Shared visual asset catalog

- [x] Create the first versioned object visual manifest.
- [x] Keep the existing outlined cube renderer as the default visual.
- [x] Add the MazeBench outlined wedge/ramp primitive with a full-cell editor pick volume.
- [ ] Add a six-direction face/plane primitive.
- [x] Add a six-face orange pressure-button cylinder primitive.
- [ ] Add cube-face symbols/decals.
- [ ] Support GLB models with anchor, scale, offset, orientation, and pick proxy metadata.
- [x] Import the MazeBench gem GLB with source/provenance documentation.
- [x] Render the imported gem with MazeBench's black-outline treatment.
- [x] Port the MazeBench purple player-lift slab/cube recipe as IDs `0/1` Up, `2/3` Front, `4/5` Right, `6/7` Back, and `8/9` Left; reserve the downward mounting for later.
- [ ] Port reusable visual recipes for gates, orange mechanisms, slopes, and punchers.
- [ ] Keep canonical assets in shared game data for the web and future Apple app.

## Phase 5 — Generalized C++ object/state ABI

- [ ] Extend engine scene records with group, variant, orientation, mutable state, and stable identity as needed.
- [ ] Keep render asset metadata out of the deterministic physics ABI.
- [ ] Build compact per-cell spans for bodies, supports, sensors, and decorations.
- [ ] Keep the common single-body lookup path fast.
- [ ] Support collision masks such as “blocks player but not boxes.”
- [ ] Keep `providesSupport` independent from `blocksMovement`.
- [ ] Include every mutable state value in search equality and hashing.
- [ ] Preserve state through search reconstruction and tick observers.
- [ ] Emit occupant/state changes in every animation frame.
- [ ] Benchmark native and WASM generalized search after the storage change.

## Phase 6 — Mechanics vertical slices

- [x] Gem: real model, editor sensor occupancy, existing C++ player collection, and box overlap.
- [x] Pressure button: single-state six-face cylinder whose hidden activation comes from overlapping rigid occupants.
- [x] Orange wall: additive lowering depth driven by the number of concurrently pressed buttons.
- [x] Orange hidden volume: overlap-safe per-cell metadata, translucent editor cube, and gameplay-hidden rendering.
- [ ] Replace inferred Orange Wall anchors with explicit cube → face → hidden-volume transitions in C++ once the revised authored traces are final.
- [x] Ice slope: four directions with discrete ramp traversal.
- [x] Detect exact whole-level slope cycles, show their tick interval, and roll the command back.
- [x] Keep falling momentum when a player or box is caught by a directional slope.
- [ ] Flat surface/platform: explicit support behavior without full-cell collision.
- [ ] Puncher: directional fixture and launch behavior.
- [x] Player lift: lowered/raised collision, player-triggered state toggle, blocked-headroom guard, rider movement, face-derived editor painting, attached-fixture carrying, trace persistence, and exact-search identity.
- [ ] Player gate: role-filtered collision.
- [ ] Attached/moving device variants.

## Required acceptance coverage

- [x] A box and gem coexist at exactly the same coordinate.
- [x] A player and button coexist and activate the mechanism without changing the button visual.
- [x] Multiple sensors coexist with one body.
- [x] Two incompatible solid bodies never coexist.
- [x] Removing one occupant preserves every other occupant.
- [ ] Undo, copy tick, export/import, reset, and resize preserve overlaps.
- [ ] Frame differences identify the exact missing state/variant/occupant.
- [x] Automatic rotations correctly transform authored ramps and their cycle traces.
- [ ] Automatic rotations correctly transform future punchers, face symbols, and unrestricted orientations.
- [x] Search distinguishes identical coordinates with different mechanism states.
- [x] A supported orange wall flattens into a surface; over a void it descends as a full cube.
- [x] Hidden Orange Wall volumes can be painted, selected, translated, and stored inside any occupied editor cell.
- [ ] Custom shapes remain pickable from above and below.
- [x] All 321 authored physics tests and their automatic rotations remain green.
- [x] Native C++ tests, WASM API tests, web tests, lint, and production build remain green.
