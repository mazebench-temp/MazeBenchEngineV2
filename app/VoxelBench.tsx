"use client";

import {
  ChangeEvent,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import MazeBenchCanvas from "./MazeBenchCanvas";

type Behavior = "solid" | "player" | "pushable" | "goal" | "decor";
type Direction = "up" | "down" | "left" | "right";
type FrameKind = "start" | "expected";

type BlockDefinition = {
  id: string;
  name: string;
  color: string;
  behavior: Behavior;
};

type Voxel = { x: number; y: number; z: number; blockId: string };
type Frame = { voxels: Voxel[] };
type WorldSettings = { width: number; height: number; floorLayer: 0 };
type TestCase = {
  id: string;
  name: string;
  input: Direction;
  start: Frame;
  expected: Frame;
};

type TestResult = {
  actual: Frame;
  pass: boolean;
  missing: Voxel[];
  unexpected: Voxel[];
};

type EditSnapshot = {
  frame: Frame;
  frameKind: FrameKind;
  testId: string;
};

const STORAGE_KEY = "voxelbench-project-v1";
const DELETE_TOOL_ID = "__erase_top__";
const UNDO_STACK_LIMIT = 80;
const DIRECTIONS: Direction[] = ["up", "down", "left", "right"];
const BEHAVIOR_LABELS: Record<Behavior, string> = {
  solid: "Solid",
  player: "Player",
  pushable: "Pushable",
  goal: "Goal / floor",
  decor: "Decoration",
};

const DEFAULT_BLOCKS: BlockDefinition[] = [
  { id: "floor", name: "Limestone", color: "#D8CFC0", behavior: "solid" },
  { id: "wall", name: "Basalt wall", color: "#424957", behavior: "solid" },
  { id: "crate", name: "Amber crate", color: "#E9963A", behavior: "pushable" },
  { id: "player", name: "Player", color: "#5A67D8", behavior: "player" },
  { id: "goal", name: "Goal tile", color: "#48A985", behavior: "goal" },
];

function keyOf(voxel: Pick<Voxel, "x" | "y" | "z">) {
  return `${voxel.x},${voxel.y},${voxel.z}`;
}

function floorFrame(width = 8, height = 7): Voxel[] {
  const voxels: Voxel[] = [];
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      voxels.push({ x, y, z: 0, blockId: x === 5 && y === 3 ? "goal" : "floor" });
    }
  }
  return voxels;
}

function defaultRoomFloor(width: number, height: number): Frame {
  return {
    voxels: Array.from({ length: height }, (_, y) =>
      Array.from({ length: width }, (_, x): Voxel => ({ x, y, z: 0, blockId: "floor" })),
    ).flat(),
  };
}

function withActors(actors: Voxel[]) {
  return { voxels: [...floorFrame(), ...actors] };
}

const DEFAULT_TESTS: TestCase[] = [
  {
    id: "push-one",
    name: "Push crate onto goal",
    input: "right",
    start: withActors([
      { x: 3, y: 3, z: 1, blockId: "player" },
      { x: 4, y: 3, z: 1, blockId: "crate" },
    ]),
    expected: withActors([
      { x: 4, y: 3, z: 1, blockId: "player" },
      { x: 5, y: 3, z: 1, blockId: "crate" },
    ]),
  },
  {
    id: "wall-stop",
    name: "Wall blocks movement",
    input: "up",
    start: withActors([
      { x: 2, y: 3, z: 1, blockId: "player" },
      { x: 2, y: 2, z: 1, blockId: "wall" },
    ]),
    expected: withActors([
      { x: 2, y: 3, z: 1, blockId: "player" },
      { x: 2, y: 2, z: 1, blockId: "wall" },
    ]),
  },
  {
    id: "double-crate",
    name: "Two crates cannot be pushed",
    input: "left",
    start: withActors([
      { x: 5, y: 4, z: 1, blockId: "player" },
      { x: 4, y: 4, z: 1, blockId: "crate" },
      { x: 3, y: 4, z: 1, blockId: "crate" },
    ]),
    expected: withActors([
      { x: 5, y: 4, z: 1, blockId: "player" },
      { x: 4, y: 4, z: 1, blockId: "crate" },
      { x: 3, y: 4, z: 1, blockId: "crate" },
    ]),
  },
  {
    id: "future-ice",
    name: "Ice slides two spaces",
    input: "right",
    start: withActors([{ x: 2, y: 1, z: 1, blockId: "player" }]),
    expected: withActors([{ x: 4, y: 1, z: 1, blockId: "player" }]),
  },
];

const DEFAULT_WORLD: WorldSettings = { width: 8, height: 7, floorLayer: 0 };

function cloneFrame(frame: Frame): Frame {
  return { voxels: frame.voxels.map((voxel) => ({ ...voxel })) };
}

function sortVoxels(voxels: Voxel[]) {
  return [...voxels].sort((a, b) => {
    const coord = keyOf(a).localeCompare(keyOf(b));
    return coord || a.blockId.localeCompare(b.blockId);
  });
}

function compareFrames(expected: Frame, actual: Frame): TestResult {
  const expectedMap = new Map(expected.voxels.map((voxel) => [`${keyOf(voxel)}:${voxel.blockId}`, voxel]));
  const actualMap = new Map(actual.voxels.map((voxel) => [`${keyOf(voxel)}:${voxel.blockId}`, voxel]));
  const missing = [...expectedMap].filter(([key]) => !actualMap.has(key)).map(([, voxel]) => voxel);
  const unexpected = [...actualMap].filter(([key]) => !expectedMap.has(key)).map(([, voxel]) => voxel);
  return { actual, pass: missing.length === 0 && unexpected.length === 0, missing, unexpected };
}

// This intentionally small adapter is the seam where the full turn-based engine plugs in.
function simulateTurn(test: TestCase, definitions: BlockDefinition[]): Frame {
  const actual = cloneFrame(test.start);
  const behavior = new Map(definitions.map((definition) => [definition.id, definition.behavior]));
  const player = actual.voxels.find((voxel) => behavior.get(voxel.blockId) === "player");
  if (!player) return actual;

  const delta: Record<Direction, [number, number]> = {
    up: [0, -1],
    down: [0, 1],
    left: [-1, 0],
    right: [1, 0],
  };
  const [dx, dy] = delta[test.input];
  const occupantAt = (x: number, y: number, z: number) =>
    actual.voxels.find((voxel) => voxel.x === x && voxel.y === y && voxel.z === z);
  const target = occupantAt(player.x + dx, player.y + dy, player.z);

  if (!target) {
    player.x += dx;
    player.y += dy;
    return actual;
  }

  if (behavior.get(target.blockId) !== "pushable") return actual;
  const beyond = occupantAt(target.x + dx, target.y + dy, target.z);
  if (beyond) return actual;
  target.x += dx;
  target.y += dy;
  player.x += dx;
  player.y += dy;
  return actual;
}

function DirectionIcon({ direction }: { direction: Direction }) {
  return <span aria-hidden="true">{{ up: "↑", down: "↓", left: "←", right: "→" }[direction]}</span>;
}

export default function VoxelBench() {
  const [blocks, setBlocks] = useState<BlockDefinition[]>(DEFAULT_BLOCKS);
  const [tests, setTests] = useState<TestCase[]>(DEFAULT_TESTS);
  const [world, setWorld] = useState<WorldSettings>(DEFAULT_WORLD);
  const [activeId, setActiveId] = useState(DEFAULT_TESTS[0].id);
  const [frameKind, setFrameKind] = useState<FrameKind>("start");
  const [selectedBlock, setSelectedBlock] = useState("crate");
  const layer = 1;
  const [results, setResults] = useState<Record<string, TestResult>>({});
  const [showResult, setShowResult] = useState(false);
  const [toast, setToast] = useState("Ready");
  const [addingBlock, setAddingBlock] = useState(false);
  const [newBlock, setNewBlock] = useState({ name: "", color: "#D96B5F", behavior: "solid" as Behavior });
  const importRef = useRef<HTMLInputElement>(null);
  const undoStackRef = useRef<EditSnapshot[]>([]);
  const redoStackRef = useRef<EditSnapshot[]>([]);
  const paintGestureRef = useRef({ active: false, snapshotSaved: false });
  const [historyState, setHistoryState] = useState({ canRedo: false, canUndo: false });
  const activeTest = tests.find((test) => test.id === activeId) ?? tests[0];
  const activeFrame = activeTest?.[frameKind] ?? { voxels: [] };
  const activeResult = activeTest ? results[activeTest.id] : undefined;
  const selectedDefinition = selectedBlock === DELETE_TOOL_ID
    ? undefined
    : blocks.find((block) => block.id === selectedBlock) ?? blocks[0];
  const selectedToolName = selectedBlock === DELETE_TOOL_ID ? "Erase" : selectedDefinition?.name;

  useEffect(() => {
    const timer = window.setTimeout(() => {
      try {
        const saved = localStorage.getItem(STORAGE_KEY);
        if (!saved) return;
        const parsed = JSON.parse(saved) as { blocks: BlockDefinition[]; tests: TestCase[]; world: WorldSettings };
        if (parsed.blocks?.length && parsed.tests?.length && parsed.world) {
          setBlocks(parsed.blocks);
          setTests(parsed.tests);
          setWorld(parsed.world);
          setActiveId(parsed.tests[0].id);
          setToast("Restored local project");
        }
      } catch {
        setToast("Could not restore local project");
      }
    }, 0);
    return () => window.clearTimeout(timer);
  }, []);

  useEffect(() => {
    const timer = window.setTimeout(() => {
      localStorage.setItem(STORAGE_KEY, JSON.stringify({ schemaVersion: 1, blocks, tests, world }));
    }, 250);
    return () => window.clearTimeout(timer);
  }, [blocks, tests, world]);

  const runTest = useCallback((test: TestCase) => {
    const result = compareFrames(test.expected, simulateTurn(test, blocks));
    setResults((current) => ({ ...current, [test.id]: result }));
    if (test.id === activeId) setShowResult(true);
    setToast(result.pass ? `${test.name} passed` : `${test.name} has ${result.missing.length + result.unexpected.length} differences`);
    return result;
  }, [activeId, blocks]);

  const runSuite = useCallback(() => {
    const nextResults: Record<string, TestResult> = {};
    for (const test of tests) nextResults[test.id] = compareFrames(test.expected, simulateTurn(test, blocks));
    setResults(nextResults);
    const passed = Object.values(nextResults).filter((result) => result.pass).length;
    setShowResult(true);
    setToast(`${passed} of ${tests.length} tests passed`);
  }, [blocks, tests]);

  const syncHistoryState = useCallback(() => {
    setHistoryState({
      canRedo: redoStackRef.current.length > 0,
      canUndo: undoStackRef.current.length > 0,
    });
  }, []);

  const pushHistory = useCallback((stack: React.MutableRefObject<EditSnapshot[]>, snapshot: EditSnapshot) => {
    stack.current.push({ ...snapshot, frame: cloneFrame(snapshot.frame) });
    if (stack.current.length > UNDO_STACK_LIMIT) stack.current.shift();
    syncHistoryState();
  }, [syncHistoryState]);

  const clearHistory = useCallback(() => {
    undoStackRef.current = [];
    redoStackRef.current = [];
    paintGestureRef.current = { active: false, snapshotSaved: false };
    syncHistoryState();
  }, [syncHistoryState]);

  const restoreEditSnapshot = useCallback((snapshot: EditSnapshot, oppositeStack: React.MutableRefObject<EditSnapshot[]>) => {
    const currentTest = tests.find((test) => test.id === snapshot.testId);
    if (!currentTest) return false;
    pushHistory(oppositeStack, {
      testId: snapshot.testId,
      frameKind: snapshot.frameKind,
      frame: currentTest[snapshot.frameKind],
    });
    setTests((current) => current.map((test) =>
      test.id === snapshot.testId
        ? { ...test, [snapshot.frameKind]: cloneFrame(snapshot.frame) }
        : test,
    ));
    setActiveId(snapshot.testId);
    setFrameKind(snapshot.frameKind);
    setShowResult(false);
    setResults((current) => {
      const next = { ...current };
      delete next[snapshot.testId];
      return next;
    });
    return true;
  }, [pushHistory, tests]);

  const undoPaint = useCallback(() => {
    const snapshot = undoStackRef.current.pop();
    if (!snapshot) {
      setToast("Nothing to undo");
      return;
    }
    if (restoreEditSnapshot(snapshot, redoStackRef)) {
      setToast("Undid the last paint stroke");
    }
    syncHistoryState();
  }, [restoreEditSnapshot, syncHistoryState]);

  const redoPaint = useCallback(() => {
    const snapshot = redoStackRef.current.pop();
    if (!snapshot) {
      setToast("Nothing to redo");
      return;
    }
    if (restoreEditSnapshot(snapshot, undoStackRef)) {
      setToast("Redid the paint stroke");
    }
    syncHistoryState();
  }, [restoreEditSnapshot, syncHistoryState]);

  const beginPaintGesture = useCallback(() => {
    paintGestureRef.current = { active: true, snapshotSaved: false };
  }, []);

  const endPaintGesture = useCallback(() => {
    paintGestureRef.current = { active: false, snapshotSaved: false };
  }, []);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement;
      if (["INPUT", "SELECT", "TEXTAREA"].includes(target.tagName)) return;
      const key = event.key.toLowerCase();
      if ((event.metaKey || event.ctrlKey) && key === "z") {
        event.preventDefault();
        if (event.shiftKey) redoPaint();
        else undoPaint();
        return;
      }
      if (!event.metaKey && !event.ctrlKey && !event.altKey && key === "u") {
        event.preventDefault();
        undoPaint();
        return;
      }
      if (!event.metaKey && !event.ctrlKey && !event.altKey && key === "e") {
        event.preventDefault();
        setSelectedBlock(DELETE_TOOL_ID);
        setToast("Erase tool selected");
        return;
      }
      if (!event.metaKey && !event.ctrlKey && !event.altKey && /^[1-9]$/.test(key)) {
        const definition = blocks[Number(key) - 1];
        if (definition) {
          event.preventDefault();
          setSelectedBlock(definition.id);
          setToast(`${definition.name} selected`);
          return;
        }
      }
      const directionByKey: Record<string, Direction | undefined> = {
        ArrowUp: "up", ArrowDown: "down", ArrowLeft: "left", ArrowRight: "right",
      };
      const direction = directionByKey[event.key];
      if (direction && activeTest) {
        event.preventDefault();
        setTests((current) => current.map((test) => test.id === activeTest.id ? { ...test, input: direction } : test));
        setToast(`Input set to ${direction}`);
      }
      if ((event.metaKey || event.ctrlKey) && event.key === "Enter" && activeTest) {
        event.preventDefault();
        runTest(activeTest);
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [activeTest, blocks, redoPaint, runTest, undoPaint]);

  const paint = (x: number, y: number, z: number, blockId: string | null) => {
    if (!activeTest) return;
    const currentFrame = activeTest[frameKind];
    const currentVoxel = currentFrame.voxels.find((voxel) => keyOf(voxel) === `${x},${y},${z}`);
    if ((!blockId && !currentVoxel) || currentVoxel?.blockId === blockId) return;
    if (!paintGestureRef.current.active || !paintGestureRef.current.snapshotSaved) {
      pushHistory(undoStackRef, {
        testId: activeTest.id,
        frameKind,
        frame: currentFrame,
      });
      redoStackRef.current = [];
      syncHistoryState();
      paintGestureRef.current.snapshotSaved = true;
    }
    setTests((current) => current.map((test) => {
      if (test.id !== activeTest.id) return test;
      const frame = cloneFrame(test[frameKind]);
      frame.voxels = frame.voxels.filter((voxel) => keyOf(voxel) !== `${x},${y},${z}`);
      if (blockId) frame.voxels.push({ x, y, z, blockId });
      return { ...test, [frameKind]: frame };
    }));
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
  };

  const updateActive = (patch: Partial<TestCase>) => {
    if (!activeTest) return;
    setTests((current) => current.map((test) => test.id === activeTest.id ? { ...test, ...patch } : test));
  };

  const addTest = () => {
    const id = `test-${Date.now()}`;
    const source = activeTest?.start ?? { voxels: floorFrame(world.width, world.height) };
    const test: TestCase = {
      id,
      name: `Untitled test ${tests.length + 1}`,
      input: "right",
      start: cloneFrame(source),
      expected: cloneFrame(source),
    };
    setTests((current) => [...current, test]);
    setActiveId(id);
    setFrameKind("start");
    setShowResult(false);
    setToast("New test created from the current start frame");
  };

  const duplicateFrame = () => {
    if (!activeTest) return;
    updateActive({ expected: cloneFrame(activeTest.start) });
    setFrameKind("expected");
    setToast("Start frame copied to expected");
  };

  const resetRoom = () => {
    if (!activeTest) return;
    pushHistory(undoStackRef, {
      testId: activeTest.id,
      frameKind,
      frame: activeTest[frameKind],
    });
    redoStackRef.current = [];
    syncHistoryState();
    setTests((current) => current.map((test) =>
      test.id === activeTest.id
        ? { ...test, [frameKind]: defaultRoomFloor(world.width, world.height) }
        : test,
    ));
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
    setShowResult(false);
    setToast(`${frameKind === "start" ? "Start" : "Expected"} room reset to floor tiles`);
  };

  const addBlock = () => {
    const name = newBlock.name.trim();
    if (!name) return;
    const id = `${name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "block"}-${Date.now().toString().slice(-4)}`;
    setBlocks((current) => [...current, { id, ...newBlock, name }]);
    setSelectedBlock(id);
    setNewBlock({ name: "", color: "#D96B5F", behavior: "solid" });
    setAddingBlock(false);
    setToast(`${name} block created`);
  };

  const exportProject = () => {
    const payload = JSON.stringify({
      schemaVersion: 1,
      coordinateSystem: { horizontalAxes: ["x", "y"], verticalAxis: "z", floorLayer: 0 },
      world,
      blocks,
      tests: tests.map((test) => ({ ...test, start: { voxels: sortVoxels(test.start.voxels) }, expected: { voxels: sortVoxels(test.expected.voxels) } })),
    }, null, 2);
    const url = URL.createObjectURL(new Blob([payload], { type: "application/json" }));
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = "voxelbench-project.json";
    anchor.click();
    URL.revokeObjectURL(url);
    setToast("Project JSON exported");
  };

  const importProject = (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) return;
    file.text().then((text) => {
      try {
        const parsed = JSON.parse(text) as { blocks: BlockDefinition[]; tests: TestCase[]; world: WorldSettings };
        if (!parsed.blocks?.length || !parsed.tests?.length || !parsed.world) throw new Error("Invalid project");
        setBlocks(parsed.blocks);
        setTests(parsed.tests);
        setWorld(parsed.world);
        setActiveId(parsed.tests[0].id);
        setResults({});
        clearHistory();
        setToast(`Imported ${parsed.tests.length} tests`);
      } catch {
        setToast("That file is not a valid VoxelBench project");
      }
    });
    event.target.value = "";
  };

  const counts = useMemo(() => {
    const values = Object.values(results);
    return { passed: values.filter((result) => result.pass).length, failed: values.filter((result) => !result.pass).length };
  }, [results]);

  if (!activeTest) return null;

  return (
    <main className="app-shell">
      <header className="author-header">
        <div className="author-topbar">
          <nav className="author-nav" aria-label="Maze Bench navigation">
            <span className="brand-link">
              {/* Existing MazeBench brand asset; a plain image preserves its authored SVG sizing. */}
              {/* eslint-disable-next-line @next/next/no-img-element */}
              <img src="/favicon.svg" alt="" />Maze Bench
            </span>
            <span className="nav-link">Build</span>
            <span className="nav-link is-active">Tests</span>
          </nav>
          <div className="author-title">
            <span>PHYSICS WORKBENCH</span>
            <h1>Voxel Test Lab</h1>
          </div>
          <div className="author-actions">
            <button className="tool-button" onClick={() => importRef.current?.click()}>Import</button>
            <button className="tool-button" onClick={exportProject}>Export</button>
            <button className="tool-button tool-button--primary" onClick={runSuite}>Run suite</button>
          </div>
          <input ref={importRef} type="file" accept="application/json" hidden onChange={importProject} />
          <p className="author-status" role="status"><span />{toast}</p>
        </div>
      </header>

      <section className="author-layout">
        <section className="author-workspace">
          <section className="author-stage" aria-label="Voxel frame editor">
            <div className="stage-chrome stage-chrome--left">
              <div className="frame-switch" role="group" aria-label="Frame to edit">
                <button className={frameKind === "start" ? "active" : ""} onClick={() => setFrameKind("start")}><span>01</span> Start</button>
                <button className={frameKind === "expected" ? "active" : ""} onClick={() => setFrameKind("expected")}><span>02</span> Expected</button>
              </div>
              <div className="stage-history" role="group" aria-label="Paint history">
                <button type="button" aria-label="Undo paint" title="Undo paint · ⌘Z" disabled={!historyState.canUndo} onClick={undoPaint}>↶</button>
                <button type="button" aria-label="Redo paint" title="Redo paint · ⇧⌘Z" disabled={!historyState.canRedo} onClick={redoPaint}>↷</button>
              </div>
              <button type="button" className="reset-room-button" onClick={resetRoom}>Reset room</button>
            </div>
            <div className="stage-chrome stage-chrome--right">
              <span className="viewport-mode"><i /> PERSPECTIVE · MAZEBENCH THREE</span>
              <span className="coordinate-pill">{world.width} × {world.height} × ∞</span>
            </div>
            <MazeBenchCanvas frame={activeFrame} blocks={blocks} world={world} layer={layer} selectedBlock={selectedBlock} eraseMode={selectedBlock === DELETE_TOOL_ID} interactive onPaint={paint} onPaintGestureStart={beginPaintGesture} onPaintGestureEnd={endPaintGesture} />
            <div className="author-hotbar" aria-label="Block palette">
              <span className="author-hotbar__toolname is-visible">{selectedToolName}</span>
              <div className="author-hotbar__slots">
                <button type="button" className={`author-hotbar__slot author-hotbar__eraser ${selectedBlock === DELETE_TOOL_ID ? "is-active" : ""}`} aria-label="Erase tool" title="Erase cube · E" onClick={() => { setSelectedBlock(DELETE_TOOL_ID); setToast("Erase tool selected"); }}>
                  <span className="author-hotbar__key">E</span>
                  <svg className="author-tool-icon author-tool-icon--eraser" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" focusable="false"><path d="m7 21-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 1 1 2.5 0 3.4L13 21" /><path d="M22 21H7" /><path d="m5 11 9 9" /></svg>
                </button>
                {blocks.map((block, index) => (
                  <button key={block.id} className={`author-hotbar__slot ${selectedBlock === block.id ? "is-active" : ""}`} title={`${block.name} — ${BEHAVIOR_LABELS[block.behavior]}`} onClick={() => setSelectedBlock(block.id)}>
                    <span className="author-hotbar__key">{index + 1}</span>
                    <span className="swatch-cube" style={{ "--block-color": block.color } as React.CSSProperties} />
                  </button>
                ))}
                <span className="author-hotbar__divider" />
                <button className="author-hotbar__slot author-hotbar__add" aria-label="Define a new block" onClick={() => setAddingBlock(true)}>＋</button>
              </div>
            </div>
            {showResult && activeResult && (
              <div className={`result-console ${activeResult.pass ? "passed" : "failed"}`}>
                <div className="result-heading">
                  <div className="result-mark">{activeResult.pass ? "✓" : "!"}</div>
                  <div><span>ENGINE RESULT</span><h3>{activeResult.pass ? "Frames match" : `${activeResult.missing.length + activeResult.unexpected.length} voxel differences`}</h3></div>
                  <button aria-label="Close comparison" onClick={() => setShowResult(false)}>×</button>
                </div>
                <div className="compare-grid">
                  <div className="compare-card"><div><strong>EXPECTED</strong><span>{activeResult.missing.length ? `${activeResult.missing.length} missing` : "reference"}</span></div><MazeBenchCanvas frame={activeTest.expected} blocks={blocks} world={world} layer={layer} compact /></div>
                  <div className="compare-card"><div><strong>ENGINE OUTPUT</strong><span>{activeResult.unexpected.length ? `${activeResult.unexpected.length} unexpected` : "exact"}</span></div><MazeBenchCanvas frame={activeResult.actual} blocks={blocks} world={world} layer={layer} compact /></div>
                </div>
              </div>
            )}
          </section>
        </section>

        <aside className="author-sidebar">
          <details className="author-panel" open>
            <summary><span className="chevron">▸</span><span>Test Case</span><em>{activeTest.input} <DirectionIcon direction={activeTest.input} /></em></summary>
            <div className="author-panel__body">
              <label className="field"><span>Name</span><input value={activeTest.name} onChange={(event) => updateActive({ name: event.target.value })} /></label>
              <div className="field"><span>Movement input</span><div className="direction-picker">{DIRECTIONS.map((direction) => <button key={direction} aria-label={direction} className={activeTest.input === direction ? "active" : ""} onClick={() => updateActive({ input: direction })}><DirectionIcon direction={direction} /></button>)}</div></div>
              <div className="button-row"><button className="tool-button" onClick={duplicateFrame}>Start → expected</button><button className="tool-button tool-button--primary" onClick={() => runTest(activeTest)}>Run test</button></div>
            </div>
          </details>

          <details className="author-panel suite-panel" open>
            <summary><span className="chevron">▸</span><span>Test Suite</span><em className="suite-count">{Object.keys(results).length ? `${counts.passed}/${tests.length}` : tests.length}</em></summary>
            <div className="author-panel__body">
              <div className="summary-track"><i style={{ width: Object.keys(results).length ? `${(counts.passed / tests.length) * 100}%` : "0%" }} /></div>
              <div className="test-list">
                {tests.map((test, index) => {
                  const result = results[test.id];
                  return (
                    <button key={test.id} className={`test-card ${test.id === activeId ? "active" : ""}`} onClick={() => { setActiveId(test.id); setShowResult(Boolean(results[test.id])); }}>
                      <span className={`test-status ${!result ? "idle" : result.pass ? "pass" : "fail"}`}>{!result ? index + 1 : result.pass ? "✓" : "!"}</span>
                      <span className="test-copy"><strong>{test.name}</strong><small><DirectionIcon direction={test.input} /> {test.input} · {test.start.voxels.length} voxels</small></span>
                    </button>
                  );
                })}
              </div>
              <button className="tool-button full" onClick={addTest}>＋ New test case</button>
            </div>
          </details>

          <details className="author-panel" open>
            <summary><span className="chevron">▸</span><span>Block Definition</span><button type="button" className="panel-add" aria-label="Add block" onClick={(event) => { event.preventDefault(); setAddingBlock((value) => !value); }}>＋</button></summary>
            <div className="author-panel__body">
              {addingBlock ? (
                <div className="definition-form new-definition">
                  <label className="field"><span>New block name</span><input value={newBlock.name} placeholder="e.g. Ice" onChange={(event) => setNewBlock((value) => ({ ...value, name: event.target.value }))} /></label>
                  <div className="definition-row"><label className="field color-field"><span>Color</span><input type="color" value={newBlock.color} onChange={(event) => setNewBlock((value) => ({ ...value, color: event.target.value }))} /></label><label className="field"><span>Physics role</span><select value={newBlock.behavior} onChange={(event) => setNewBlock((value) => ({ ...value, behavior: event.target.value as Behavior }))}>{Object.entries(BEHAVIOR_LABELS).map(([value, label]) => <option key={value} value={value}>{label}</option>)}</select></label></div>
                  <button className="tool-button tool-button--primary full" onClick={addBlock}>Create block</button>
                </div>
              ) : selectedBlock === DELETE_TOOL_ID ? (
                <div className="eraser-description"><svg className="author-tool-icon author-tool-icon--eraser" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d="m7 21-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 1 1 2.5 0 3.4L13 21" /><path d="M22 21H7" /><path d="m5 11 9 9" /></svg><div><strong>Erase tool</strong><small>Click a visible cube to remove it. Press E to select.</small></div></div>
              ) : selectedDefinition ? (
                <div className="definition-form">
                  <div className="selected-block-title"><span className="swatch-cube large" style={{ "--block-color": selectedDefinition.color } as React.CSSProperties} /><div><strong>{selectedDefinition.name}</strong><small>{selectedDefinition.id}</small></div></div>
                  <label className="field"><span>Name</span><input value={selectedDefinition.name} onChange={(event) => { setBlocks((current) => current.map((block) => block.id === selectedBlock ? { ...block, name: event.target.value } : block)); setResults({}); }} /></label>
                  <div className="definition-row"><label className="field color-field"><span>Color</span><input type="color" value={selectedDefinition.color} onChange={(event) => setBlocks((current) => current.map((block) => block.id === selectedBlock ? { ...block, color: event.target.value } : block))} /></label><label className="field"><span>Physics role</span><select value={selectedDefinition.behavior} onChange={(event) => { setBlocks((current) => current.map((block) => block.id === selectedBlock ? { ...block, behavior: event.target.value as Behavior } : block)); setResults({}); }}>{Object.entries(BEHAVIOR_LABELS).map(([value, label]) => <option key={value} value={value}>{label}</option>)}</select></label></div>
                </div>
              ) : null}
            </div>
          </details>

          <details className="author-panel">
            <summary><span className="chevron">▸</span><span>World & Data</span><em>{world.width} × {world.height} × ∞</em></summary>
            <div className="author-panel__body">
              <div className="dimension-grid"><label className="field"><span>Width · X</span><input type="number" min="3" max="32" value={world.width} onChange={(event) => setWorld((value) => ({ ...value, width: Math.max(3, Number(event.target.value)) }))} /></label><label className="field"><span>Depth · Y</span><input type="number" min="3" max="32" value={world.height} onChange={(event) => setWorld((value) => ({ ...value, height: Math.max(3, Number(event.target.value)) }))} /></label></div>
              <p className="axis-note"><b>Z is unbounded.</b> Layer 0 is the floor; negative layers are stored normally in sparse voxel JSON.</p>
            </div>
          </details>
        </aside>
      </section>
    </main>
  );
}
