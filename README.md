# MazeBench Voxel Test Lab

A local visual unit-test editor for 3D, turn-based Sokoban-like mechanics. It uses MazeBenchEngine's perspective Three.js polycube renderer for authoring sparse 3D start frames and expected end frames.

## Run locally

```bash
npm install
npm run dev
```

Open [http://localhost:3000](http://localhost:3000).

## Editor controls

- Click a cube face to paint; drag stays locked to the first logical layer.
- Select the eraser or press `E` to remove visible cubes.
- Hold `W` / `S` to tilt smoothly through top, side, and underside views.
- Press `A` / `D` for eased quarter-turn rotation.
- Shift-drag to orbit and scroll to zoom.
- Press `Cmd/Ctrl+Z` to undo a paint stroke and `Cmd/Ctrl+Shift+Z` to redo it.
- Reset room restores the active frame to row-0 floor tiles and is undoable.
- Arrow keys choose the movement input for the active test.

Logical Z is stored sparsely and is unbounded in both directions. Empty-hole painting starts at row 0.

## Verification

```bash
npm test
npm run lint
```
