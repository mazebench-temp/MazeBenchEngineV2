"use client";

import {
  ChangeEvent,
  FormEvent,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import MazeBenchCanvas from "./MazeBenchCanvas";
import { cameraRelativeDirection } from "./cameraNavigation.mjs";
import { simulateCommandWithCpp } from "./physicsEngine";
import {
  applyWorldToTest,
  cropVoxelsToWorld,
  parseWorldDimensionDraft,
  rotateVoxelsClockwise,
  rotateWorldClockwise,
} from "./worldBounds.mjs";
import {
  moveVoxelGroup,
  removeSelectedVoxels,
  selectConnectedVoxelGroup,
  toggleVoxelGroupSelection,
} from "./voxelGroups.mjs";
import {
  genericToolbarLabel,
  offsetGenericObjectId,
  offsetToolbarIndex,
} from "./toolbarNavigation.mjs";
import { deleteTestCase } from "./testSuite.mjs";
import {
  insertIntermediateFrame,
  offsetTimelineSelection,
  previousExpectedFrame,
} from "./timelineFrames.mjs";

type Direction = "up" | "down" | "left" | "right";
type FrameKind = "start" | "expected";

type PhysicsRoleDefinition = {
  id: string;
  name: string;
  description: string;
  generic: boolean;
};

type BlockDefinition = {
  id: string;
  name: string;
  color: string;
  roleId: string;
};

type StoredBlockDefinition = Omit<BlockDefinition, "roleId"> & {
  behavior?: string;
  genericId?: number;
  roleId?: string;
};

type Voxel = { x: number; y: number; z: number; blockId: string; genericId?: number };
type Frame = { voxels: Voxel[] };
type WorldSettings = { width: number; height: number; floorLayer: 0 };
type TestFolder = { collapsed: boolean; id: string; locked: boolean; name: string };
type StoredTestFolder = Omit<TestFolder, "collapsed" | "locked"> & {
  collapsed?: boolean;
  locked?: boolean;
};
type TestCase = {
  description: string;
  folderId: string;
  id: string;
  locked: boolean;
  name: string;
  input: Direction;
  intermediate: Frame[];
  start: Frame;
  expected: Frame;
  world: WorldSettings;
};
type StoredTestCase = Omit<TestCase, "description" | "folderId" | "intermediate" | "locked" | "world"> & {
  description?: string;
  folderId?: string;
  intermediate?: Frame[];
  locked?: boolean;
  world?: WorldSettings;
};

type FrameComparison = {
  actual: Frame;
  pass: boolean;
  missing: Voxel[];
  unexpected: Voxel[];
};

type RotationDegrees = 0 | 90 | 180 | 270;
type RotationCheck = FrameComparison & {
  expected: Frame;
  input: Direction;
  rotation: RotationDegrees;
  tick?: number;
  world: WorldSettings;
};
type TestResult = RotationCheck & { checks: RotationCheck[] };

type EditSnapshot = {
  frame: Frame;
  frameKind: FrameKind;
  intermediateIndex: number | null;
  testId: string;
};

type GroupSelection = {
  frameKind: FrameKind;
  intermediateIndex: number | null;
  keys: string[];
  testId: string;
};

const STORAGE_KEY = "voxelbench-project-v1";
const LOCAL_PROJECT_ENDPOINT = "/api/local-project";
const DELETE_TOOL_ID = "__erase_top__";
const GROUP_TOOL_ID = "__select_group__";
const UNDO_STACK_LIMIT = 80;
const ROTATION_CASES: Array<{
  degrees: RotationDegrees;
  input: Direction;
  quarterTurns: 0 | 1 | 2 | 3;
}> = [
  { degrees: 0, input: "up", quarterTurns: 0 },
  { degrees: 90, input: "right", quarterTurns: 1 },
  { degrees: 180, input: "down", quarterTurns: 2 },
  { degrees: 270, input: "left", quarterTurns: 3 },
];
const DEFAULT_ROLES: PhysicsRoleDefinition[] = [
  { id: "solid", name: "Solid", description: "Occupies space and blocks movement.", generic: false },
  { id: "player", name: "Player", description: "The actor moved by the canonical directional input.", generic: false },
  { id: "pushable", name: "Pushable", description: "Moves when pushed and slides while supported by Ice.", generic: false },
  { id: "floor", name: "Floor", description: "Solid support whose edge the player may deliberately walk off.", generic: false },
  { id: "ice", name: "Ice", description: "A support tile that continues movement until normal floor or an obstacle.", generic: false },
  { id: "goal", name: "Goal / floor", description: "A floor marker with no movement behavior of its own.", generic: false },
  { id: "decor", name: "Decoration", description: "A visible object with no special movement behavior.", generic: false },
];

const DEFAULT_BLOCKS: BlockDefinition[] = [
  { id: "floor", name: "Limestone", color: "#D8CFC0", roleId: "floor" },
  { id: "wall", name: "Basalt wall", color: "#424957", roleId: "solid" },
  { id: "crate", name: "Amber crate", color: "#E9963A", roleId: "pushable" },
  { id: "player", name: "Player", color: "#5A67D8", roleId: "player" },
  { id: "ice", name: "Ice", color: "#72D7FF", roleId: "ice" },
  { id: "goal", name: "Goal tile", color: "#48A985", roleId: "goal" },
];

const DEFAULT_FOLDERS: TestFolder[] = [
  { collapsed: false, id: "general", locked: false, name: "Movement & walls" },
  { collapsed: false, id: "push-boxes", locked: false, name: "Push boxes" },
  { collapsed: false, id: "ice", locked: false, name: "Ice" },
];

function normalizeRoles(roles?: PhysicsRoleDefinition[]) {
  if (!roles?.length) return DEFAULT_ROLES.map((role) => ({ ...role }));
  const normalized = roles
    .filter((role) => role?.id)
    .map((role) => ({
      id: String(role.id),
      name: String(role.name ?? "").trim() || "Untitled role",
      description: String(role.description ?? ""),
      generic: Boolean(role.generic),
    }));
  return normalized.length ? normalized : DEFAULT_ROLES.map((role) => ({ ...role }));
}

function normalizeBlocks(
  blocks: StoredBlockDefinition[],
  roles: PhysicsRoleDefinition[],
): BlockDefinition[] {
  const roleIds = new Set(roles.map((role) => role.id));
  const fallbackRoleId = roleIds.has("solid") ? "solid" : roles[0].id;
  return blocks.map((block) => {
    const requestedRoleId = block.roleId ?? block.behavior ?? fallbackRoleId;
    return {
      id: block.id,
      name: block.name,
      color: block.color,
      roleId: roleIds.has(requestedRoleId) ? requestedRoleId : fallbackRoleId,
    };
  });
}

function normalizeFolders(folders?: StoredTestFolder[]) {
  if (!folders?.length) return DEFAULT_FOLDERS.map((folder) => ({ ...folder }));
  const seen = new Set<string>();
  const normalized = folders.flatMap((folder) => {
    const id = String(folder?.id ?? "").trim();
    if (!id || seen.has(id)) return [];
    seen.add(id);
    return [{
      collapsed: Boolean(folder.collapsed),
      id,
      locked: Boolean(folder.locked),
      name: String(folder.name ?? "").trim() || "Untitled folder",
    }];
  });
  return normalized.length ? normalized : DEFAULT_FOLDERS.map((folder) => ({ ...folder }));
}

function normalizeGenericIds(
  frame: Frame,
  blocks: BlockDefinition[],
  roles: PhysicsRoleDefinition[],
  legacyBlocks: StoredBlockDefinition[] = [],
): Frame {
  const genericRoleIds = new Set(roles.filter((role) => role.generic).map((role) => role.id));
  const blocksById = new Map(blocks.map((block) => [block.id, block]));
  const legacyIds = new Map(legacyBlocks.map((block) => [block.id, block.genericId]));
  return {
    voxels: frame.voxels.map((voxel) => {
      const block = blocksById.get(voxel.blockId);
      if (block && genericRoleIds.has(block.roleId)) {
        const value = Number(voxel.genericId ?? legacyIds.get(voxel.blockId) ?? 0);
        return {
          ...voxel,
          genericId: Number.isInteger(value) && value >= 0 ? value : 0,
        };
      }
      return { x: voxel.x, y: voxel.y, z: voxel.z, blockId: voxel.blockId };
    }),
  };
}

function setGenericModeForBlocks(
  frame: Frame,
  blockIds: ReadonlySet<string>,
  generic: boolean,
): Frame {
  return {
    voxels: frame.voxels.map((voxel) => {
      if (!blockIds.has(voxel.blockId)) return { ...voxel };
      return generic
        ? { ...voxel, genericId: Math.max(0, Math.floor(Number(voxel.genericId) || 0)) }
        : { x: voxel.x, y: voxel.y, z: voxel.z, blockId: voxel.blockId };
    }),
  };
}

function normalizeTests(
  tests: StoredTestCase[],
  folders: TestFolder[],
  blocks: BlockDefinition[],
  roles: PhysicsRoleDefinition[],
  legacyBlocks: StoredBlockDefinition[] = [],
  legacyWorld: WorldSettings = DEFAULT_WORLD,
): TestCase[] {
  const folderIds = new Set(folders.map((folder) => folder.id));
  const fallbackFolderId = folderIds.has("general") ? "general" : folders[0].id;
  return tests.map((test) => {
    const world = normalizeWorld(test.world ?? legacyWorld);
    return cropTestToWorld({
      ...test,
      description: String(test.description ?? ""),
      folderId: test.folderId && folderIds.has(test.folderId) ? test.folderId : fallbackFolderId,
      locked: Boolean(test.locked),
      intermediate: (test.intermediate ?? []).map((frame) =>
        normalizeGenericIds(frame, blocks, roles, legacyBlocks)),
      start: normalizeGenericIds(test.start, blocks, roles, legacyBlocks),
      expected: normalizeGenericIds(test.expected, blocks, roles, legacyBlocks),
      world,
    });
  });
}

function keyOf(voxel: Pick<Voxel, "x" | "y" | "z">) {
  return `${voxel.x},${voxel.y},${voxel.z}`;
}

const DEFAULT_WORLD: WorldSettings = { width: 16, height: 16, floorLayer: 0 };

function floorFrame(width = DEFAULT_WORLD.width, height = DEFAULT_WORLD.height): Voxel[] {
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

function withIceStrip(actors: Voxel[]) {
  const iceRows = new Set([1, 2, 3]);
  return {
    voxels: [
      ...floorFrame().map((voxel) =>
        voxel.x === 2 && iceRows.has(voxel.y) ? { ...voxel, blockId: "ice" } : voxel),
      ...actors,
    ],
  };
}

const DEFAULT_TESTS: TestCase[] = [
  {
    description: "Moving Up pushes the single crate one cell onto the goal, and the player occupies the crate's previous cell.",
    folderId: "push-boxes",
    id: "push-one",
    locked: false,
    name: "Push crate onto goal",
    input: "up",
    intermediate: [],
    world: { ...DEFAULT_WORLD },
    start: withActors([
      { x: 5, y: 5, z: 1, blockId: "player" },
      { x: 5, y: 4, z: 1, blockId: "crate" },
    ]),
    expected: withActors([
      { x: 5, y: 4, z: 1, blockId: "player" },
      { x: 5, y: 3, z: 1, blockId: "crate" },
    ]),
  },
  {
    description: "Moving Up into the basalt wall changes nothing; both the player and wall remain in their starting cells.",
    folderId: "general",
    id: "wall-stop",
    locked: false,
    name: "Wall blocks movement",
    input: "up",
    intermediate: [],
    world: { ...DEFAULT_WORLD },
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
    description: "Moving Up cannot push two adjacent crates, so the player and both crates remain unchanged.",
    folderId: "push-boxes",
    id: "double-crate",
    locked: false,
    name: "Two crates cannot be pushed",
    input: "up",
    intermediate: [],
    world: { ...DEFAULT_WORLD },
    start: withActors([
      { x: 4, y: 5, z: 1, blockId: "player" },
      { x: 4, y: 4, z: 1, blockId: "crate" },
      { x: 4, y: 3, z: 1, blockId: "crate" },
    ]),
    expected: withActors([
      { x: 4, y: 5, z: 1, blockId: "player" },
      { x: 4, y: 4, z: 1, blockId: "crate" },
      { x: 4, y: 3, z: 1, blockId: "crate" },
    ]),
  },
  {
    description: "One Up command carries the player across every contiguous Ice tile and stops on the normal floor beyond the strip.",
    folderId: "ice",
    id: "future-ice",
    locked: false,
    name: "Ice continues one command",
    input: "up",
    intermediate: [],
    world: { ...DEFAULT_WORLD },
    start: withIceStrip([{ x: 2, y: 4, z: 1, blockId: "player" }]),
    expected: withIceStrip([{ x: 2, y: 0, z: 1, blockId: "player" }]),
  },
];

function cloneFrame(frame: Frame): Frame {
  return { voxels: frame.voxels.map((voxel) => ({ ...voxel })) };
}

function editableFrame(
  test: TestCase,
  frameKind: FrameKind,
  intermediateIndex: number | null,
) {
  return intermediateIndex === null
    ? test[frameKind]
    : test.intermediate[intermediateIndex] ?? test.expected;
}

function replaceEditableFrame(
  test: TestCase,
  frameKind: FrameKind,
  intermediateIndex: number | null,
  frame: Frame,
): TestCase {
  if (intermediateIndex === null) return { ...test, [frameKind]: frame };
  const intermediate = [...test.intermediate];
  intermediate[intermediateIndex] = frame;
  return { ...test, intermediate };
}

function sortVoxels(voxels: Voxel[]) {
  return [...voxels].sort((a, b) => {
    const coord = keyOf(a).localeCompare(keyOf(b));
    return coord || a.blockId.localeCompare(b.blockId);
  });
}

function clampWorldDimension(value: number) {
  return Math.min(32, Math.max(3, Math.round(Number.isFinite(value) ? value : 3)));
}

function normalizeWorld(world: WorldSettings): WorldSettings {
  return {
    width: clampWorldDimension(world.width),
    height: clampWorldDimension(world.height),
    floorLayer: 0,
  };
}

function cropFrameToWorld(frame: Frame, world: WorldSettings): Frame {
  return { voxels: cropVoxelsToWorld(frame.voxels, world) };
}

function cropTestToWorld(test: TestCase, world: WorldSettings = test.world): TestCase {
  return applyWorldToTest({ ...test, input: "up" }, world) as TestCase;
}

function cropTestsToWorld(tests: TestCase[]) {
  return tests.map((test) => cropTestToWorld(test));
}

function compareFrames(expected: Frame, actual: Frame, world: WorldSettings): FrameComparison {
  const boundedExpected = cropFrameToWorld(expected, world);
  const boundedActual = cropFrameToWorld(actual, world);
  const identity = (voxel: Voxel) => `${keyOf(voxel)}:${voxel.blockId}:${voxel.genericId ?? -1}`;
  const expectedMap = new Map(boundedExpected.voxels.map((voxel) => [identity(voxel), voxel]));
  const actualMap = new Map(boundedActual.voxels.map((voxel) => [identity(voxel), voxel]));
  const missing = [...expectedMap].filter(([key]) => !actualMap.has(key)).map(([, voxel]) => voxel);
  const unexpected = [...actualMap].filter(([key]) => !expectedMap.has(key)).map(([, voxel]) => voxel);
  return { actual: boundedActual, pass: missing.length === 0 && unexpected.length === 0, missing, unexpected };
}

function rotateFrame(frame: Frame, world: WorldSettings, quarterTurns: number): Frame {
  return { voxels: rotateVoxelsClockwise(frame.voxels, world, quarterTurns) };
}

async function runRotationalTest(
  test: TestCase,
  definitions: BlockDefinition[],
  roles: PhysicsRoleDefinition[],
): Promise<TestResult> {
  const world = test.world;
  const canonicalTest = cropTestToWorld(test);
  const checks: RotationCheck[] = [];
  for (const { degrees, input, quarterTurns } of ROTATION_CASES) {
    const horizontalWorld = rotateWorldClockwise(world, quarterTurns);
    const rotatedWorld: WorldSettings = { ...horizontalWorld, floorLayer: 0 };
    const rotatedTest: TestCase = {
      ...canonicalTest,
      input,
      world: rotatedWorld,
      start: rotateFrame(canonicalTest.start, world, quarterTurns),
      expected: rotateFrame(canonicalTest.expected, world, quarterTurns),
      intermediate: canonicalTest.intermediate.map((frame) =>
        rotateFrame(frame, world, quarterTurns)),
    };
    const simulation = await simulateCommandWithCpp(
      rotatedTest.start, input, definitions, roles, rotatedWorld);
    let comparison: FrameComparison = compareFrames(
      rotatedTest.expected, simulation.final, rotatedWorld);
    let tick: number | undefined;
    for (let index = 0; index < rotatedTest.intermediate.length; index += 1) {
      const expectedTick = rotatedTest.intermediate[index];
      const actualTick = simulation.frames[index];
      if (!actualTick) {
        comparison = compareFrames(expectedTick, simulation.final, rotatedWorld);
        tick = index + 1;
        break;
      }
      const tickComparison = compareFrames(expectedTick, actualTick, rotatedWorld);
      if (!tickComparison.pass) {
        comparison = tickComparison;
        tick = index + 1;
        break;
      }
    }
    checks.push({
      ...comparison,
      expected: tick === undefined
        ? rotatedTest.expected
        : rotatedTest.intermediate[tick - 1],
      input,
      rotation: degrees,
      tick,
      world: rotatedWorld,
    });
  }
  const representative = checks.find((check) => !check.pass) ?? checks[0];
  return {
    ...representative,
    checks,
    pass: checks.every((check) => check.pass),
  };
}

function DirectionIcon({ direction }: { direction: Direction }) {
  return <span aria-hidden="true">{{ up: "↑", down: "↓", left: "←", right: "→" }[direction]}</span>;
}

function TestWorldEditor({
  locked,
  onSave,
  test,
}: {
  locked: boolean;
  onSave: (width: number, height: number) => void;
  test: TestCase;
}) {
  const [widthDraft, setWidthDraft] = useState(String(test.world.width));
  const [heightDraft, setHeightDraft] = useState(String(test.world.height));
  const [error, setError] = useState("");

  const cancel = () => {
    setWidthDraft(String(test.world.width));
    setHeightDraft(String(test.world.height));
    setError("");
  };

  const save = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const width = parseWorldDimensionDraft(widthDraft);
    const height = parseWorldDimensionDraft(heightDraft);
    if (width === null || height === null) {
      setError("Width and depth must be whole numbers from 3 to 32.");
      return;
    }
    setError("");
    onSave(width, height);
  };

  return (
    <form className="dimension-editor" onSubmit={save}>
      <div className="dimension-grid">
        <label className="field"><span>Width · X</span><input aria-label="Test room width draft" aria-describedby={error ? "dimension-draft-error" : undefined} aria-invalid={Boolean(error)} type="text" inputMode="numeric" autoComplete="off" disabled={locked} value={widthDraft} onChange={(event) => { setWidthDraft(event.target.value); setError(""); }} /></label>
        <label className="field"><span>Depth · Y</span><input aria-label="Test room depth draft" aria-describedby={error ? "dimension-draft-error" : undefined} aria-invalid={Boolean(error)} type="text" inputMode="numeric" autoComplete="off" disabled={locked} value={heightDraft} onChange={(event) => { setHeightDraft(event.target.value); setError(""); }} /></label>
      </div>
      {error && <p className="dimension-error" id="dimension-draft-error" role="alert">{error}</p>}
      <div className="dimension-actions">
        <button className="tool-button" type="button" disabled={locked} onClick={cancel}>Cancel</button>
        <button className="tool-button tool-button--primary" type="submit" disabled={locked}>Save dimensions</button>
      </div>
    </form>
  );
}

function TimelineSnapshotStrip({
  blocks,
  final,
  frames,
  genericBlockIds,
  layer,
  start,
  world,
}: {
  blocks: BlockDefinition[];
  final: Frame;
  frames: Frame[];
  genericBlockIds: ReadonlySet<string>;
  layer: number;
  start: Frame;
  world: WorldSettings;
}) {
  const items = frames.length
    ? [{ frame: start, label: "START" }, ...frames.map((frame, index) => ({
      frame,
      label: `TICK ${index + 1}`,
    }))]
    : [{ frame: start, label: "START" }, { frame: final, label: "FINAL" }];
  const [snapshots, setSnapshots] = useState<Array<string | undefined>>([]);
  const [captureIndex, setCaptureIndex] = useState(0);
  const capture = items[captureIndex];
  const acceptSnapshot = useCallback((dataUrl: string) => {
    setSnapshots((current) => {
      const next = [...current];
      next[captureIndex] = dataUrl;
      return next;
    });
    setCaptureIndex((current) => current + 1);
  }, [captureIndex]);

  return (
    <div className="timeline-review__strip">
      {items.map((item, index) => (
        <article className="timeline-review__card" key={item.label}>
          <small>{item.label}</small>
          {snapshots[index] ? (
            // Data URLs are generated locally from the shared sequential
            // WebGL capture; an image optimizer cannot improve this source.
            // eslint-disable-next-line @next/next/no-img-element
            <img alt={`${item.label.toLowerCase()} voxel preview`} draggable={false} src={snapshots[index]} />
          ) : captureIndex === index && capture ? (
            <MazeBenchCanvas
              key={`capture-${index}`}
              frame={capture.frame}
              blocks={blocks}
              genericBlockIds={genericBlockIds}
              world={world}
              layer={layer}
              compact
              onSnapshot={acceptSnapshot}
            />
          ) : (
            <span className={`timeline-review__snapshot-status ${snapshots[index] === "" ? "failed" : ""}`}>
              {snapshots[index] === "" ? "Preview unavailable" : "Rendering…"}
            </span>
          )}
        </article>
      ))}
    </div>
  );
}

export default function VoxelBench() {
  const [roles, setRoles] = useState<PhysicsRoleDefinition[]>(DEFAULT_ROLES);
  const [blocks, setBlocks] = useState<BlockDefinition[]>(DEFAULT_BLOCKS);
  const [folders, setFolders] = useState<TestFolder[]>(DEFAULT_FOLDERS);
  const [tests, setTests] = useState<TestCase[]>(DEFAULT_TESTS);
  const [activeId, setActiveId] = useState(DEFAULT_TESTS[0].id);
  const [frameKind, setFrameKind] = useState<FrameKind>("start");
  const [intermediateIndex, setIntermediateIndex] = useState<number | null>(null);
  const [generatedTimeline, setGeneratedTimeline] = useState<{
    final: Frame;
    frames: Frame[];
    generationId: number;
    testId: string;
  } | null>(null);
  const [selectedBlock, setSelectedBlock] = useState("crate");
  const [groupToolPinned, setGroupToolPinned] = useState(false);
  const [groupSelection, setGroupSelection] = useState<GroupSelection | null>(null);
  const [selectedGenericIds, setSelectedGenericIds] = useState<Record<string, number>>({});
  const [genericPrompt, setGenericPrompt] = useState<{ blockId: string; value: string } | null>(null);
  const [genericPromptError, setGenericPromptError] = useState("");
  const layer = 1;
  const [results, setResults] = useState<Record<string, TestResult>>({});
  const [showResult, setShowResult] = useState(false);
  const [toast, setToast] = useState("");
  const [addingBlock, setAddingBlock] = useState(false);
  const [newBlock, setNewBlock] = useState({ name: "", color: "#D96B5F", roleId: "solid" });
  const [addingRole, setAddingRole] = useState(false);
  const [selectedRoleId, setSelectedRoleId] = useState("pushable");
  const [newRole, setNewRole] = useState({ name: "", description: "", generic: false });
  const [addingFolder, setAddingFolder] = useState(false);
  const [newFolderName, setNewFolderName] = useState("");
  const importRef = useRef<HTMLInputElement>(null);
  const genericIdInputRef = useRef<HTMLInputElement>(null);
  const undoStackRef = useRef<EditSnapshot[]>([]);
  const redoStackRef = useRef<EditSnapshot[]>([]);
  const paintGestureRef = useRef({ active: false, snapshotSaved: false });
  const [historyState, setHistoryState] = useState({ canRedo: false, canUndo: false });
  const [cameraQuarterTurns, setCameraQuarterTurns] = useState(0);
  const [projectLoaded, setProjectLoaded] = useState(false);
  const activeTest = tests.find((test) => test.id === activeId) ?? tests[0];
  const lockedFolderIds = useMemo(
    () => new Set(folders.filter((folder) => folder.locked).map((folder) => folder.id)),
    [folders],
  );
  const isTestLocked = useCallback(
    (test: TestCase) => test.locked || lockedFolderIds.has(test.folderId),
    [lockedFolderIds],
  );
  const activeTestLocked = activeTest ? isTestLocked(activeTest) : false;
  const activeWorld = activeTest?.world ?? DEFAULT_WORLD;
  const activeFrame = activeTest
    ? cropFrameToWorld(editableFrame(activeTest, frameKind, intermediateIndex), activeWorld)
    : { voxels: [] };
  const activeGroupSelection = groupSelection &&
    groupSelection.testId === activeTest?.id &&
    groupSelection.frameKind === frameKind &&
    groupSelection.intermediateIndex === intermediateIndex
    ? groupSelection
    : null;
  const selectedVoxelKeys = useMemo(
    () => new Set(activeGroupSelection?.keys ?? []),
    [activeGroupSelection],
  );
  const groupSelectionMode = groupToolPinned;
  const activeResult = activeTest ? results[activeTest.id] : undefined;
  const genericRoleIds = useMemo(
    () => new Set(roles.filter((role) => role.generic).map((role) => role.id)),
    [roles],
  );
  const genericBlockIds = useMemo(
    () => new Set(blocks.filter((block) => genericRoleIds.has(block.roleId)).map((block) => block.id)),
    [blocks, genericRoleIds],
  );
  const selectedDefinition = selectedBlock === DELETE_TOOL_ID
    ? undefined
    : blocks.find((block) => block.id === selectedBlock) ?? blocks[0];
  const selectedRole = roles.find((role) => role.id === selectedRoleId) ?? roles[0];
  const selectedGenericId = selectedDefinition && genericBlockIds.has(selectedDefinition.id)
    ? selectedGenericIds[selectedDefinition.id] ?? 0
    : null;
  const selectedToolName = groupSelectionMode
    ? "Select group"
    : activeGroupSelection
      ? `${activeGroupSelection.keys.length} cubes selected`
      : selectedBlock === DELETE_TOOL_ID
        ? "Erase"
        : `${selectedDefinition?.name ?? "Block"}${selectedGenericId === null ? "" : ` · ${selectedGenericId}`}`;

  useEffect(() => {
    let cancelled = false;

    const restoreProject = async () => {
      let repoProject: string | null = null;
      try {
        const response = await fetch(LOCAL_PROJECT_ENDPOINT, { cache: "no-store" });
        if (response.ok) repoProject = await response.text();
      } catch {
        // Repo persistence is local-development-only. Browser storage remains
        // the fallback for production previews and temporarily offline runs.
      }

      const browserProject = localStorage.getItem(STORAGE_KEY);
      const candidates = [repoProject, browserProject].filter((value): value is string => Boolean(value));
      let restored = false;

      for (const saved of candidates) {
        try {
          const parsed = JSON.parse(saved) as {
          blocks: StoredBlockDefinition[];
          folders?: StoredTestFolder[];
          roles?: PhysicsRoleDefinition[];
          tests: StoredTestCase[];
          world?: WorldSettings;
          };
          if (!parsed.blocks?.length || !parsed.tests?.length) continue;
          const legacyWorld = normalizeWorld(parsed.world ?? DEFAULT_WORLD);
          const restoredRoles = normalizeRoles(parsed.roles);
          if (cancelled) return;
          setRoles(restoredRoles);
          setSelectedRoleId(restoredRoles.find((role) => role.id === "pushable")?.id ?? restoredRoles[0].id);
          setNewBlock((current) => ({
            ...current,
            roleId: restoredRoles.some((role) => role.id === current.roleId)
              ? current.roleId
              : restoredRoles[0].id,
          }));
          const restoredBlocks = normalizeBlocks(parsed.blocks, restoredRoles);
          setBlocks(restoredBlocks);
          const restoredFolders = normalizeFolders(parsed.folders);
          const restoredTests = normalizeTests(
            parsed.tests,
            restoredFolders,
            restoredBlocks,
            restoredRoles,
            parsed.blocks,
            legacyWorld,
          );
          setFolders(restoredFolders);
          setTests(restoredTests);
          setActiveId(restoredTests[0].id);
          restored = true;
          break;
        } catch {
          // Try the browser fallback if the repo copy is incomplete.
        }
      }

      if (!cancelled) {
        if (!restored && candidates.length) {
          setToast("Could not restore saved project data");
        }
        setProjectLoaded(true);
      }
    };

    void restoreProject();
    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    if (!projectLoaded) return;
    let cancelled = false;
    const timer = window.setTimeout(() => {
      const project = {
        schemaVersion: 8,
        coordinateSystem: { horizontalAxes: ["x", "y"], verticalAxis: "z", floorLayer: 0 },
        roles,
        blocks,
        folders,
        tests: cropTestsToWorld(tests).map((test) => ({
          ...test,
          start: { voxels: sortVoxels(test.start.voxels) },
          intermediate: test.intermediate.map((frame) => ({
            voxels: sortVoxels(frame.voxels),
          })),
          expected: { voxels: sortVoxels(test.expected.voxels) },
        })),
      };
      const serialized = JSON.stringify(project);
      localStorage.setItem(STORAGE_KEY, serialized);
      void fetch(LOCAL_PROJECT_ENDPOINT, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: serialized,
      }).then((response) => {
        if (!response.ok && response.status !== 404 && !cancelled) {
          setToast("Repo save failed · browser backup preserved");
        }
      }).catch(() => {
        // Expected outside local development; localStorage already succeeded.
      });
    }, 350);
    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, [blocks, folders, projectLoaded, roles, tests]);

  const runTest = useCallback(async (test: TestCase) => {
    setToast(`Running ${test.name} through the C++ engine…`);
    try {
      const result = await runRotationalTest(test, blocks, roles);
      setResults((current) => ({ ...current, [test.id]: result }));
      if (test.id === activeId) setShowResult(true);
      const failedRotations = result.checks.filter((check) => !check.pass).length;
      setToast(result.pass
        ? `${test.name} passed all 4 rotations in C++`
        : `${test.name} failed ${failedRotations} of 4 rotations · first at ${result.rotation}° ${result.input}${result.tick === undefined ? "" : ` · tick ${result.tick}`}`);
      return result;
    } catch (error) {
      setToast(error instanceof Error ? error.message : "The C++ physics engine could not run");
      return null;
    }
  }, [activeId, blocks, roles]);

  const runSuite = useCallback(async () => {
    setToast(`Running ${tests.length * 4} rotated checks through the C++ engine…`);
    try {
      const nextResults: Record<string, TestResult> = {};
      for (const test of tests) nextResults[test.id] = await runRotationalTest(test, blocks, roles);
      setResults(nextResults);
      const passed = Object.values(nextResults).filter((result) => result.pass).length;
      setShowResult(true);
      setToast(`${passed} of ${tests.length} tests passed in C++`);
    } catch (error) {
      setToast(error instanceof Error ? error.message : "The C++ physics engine could not run");
    }
  }, [blocks, roles, tests]);

  const generateTimeline = useCallback(async () => {
    if (!activeTest) return;
    setToast(`Generating ${activeTest.name} tick frames in C++…`);
    try {
      const simulation = await simulateCommandWithCpp(
        cropFrameToWorld(activeTest.start, activeTest.world),
        "up",
        blocks,
        roles,
        activeTest.world,
      );
      setGeneratedTimeline({
        final: cropFrameToWorld(simulation.final, activeTest.world),
        frames: simulation.frames.map((frame) =>
          cropFrameToWorld(frame, activeTest.world)),
        generationId: Date.now(),
        testId: activeTest.id,
      });
      setToast(`${simulation.frames.length} C++ tick ${simulation.frames.length === 1 ? "frame" : "frames"} generated · review before accepting`);
    } catch (error) {
      setToast(error instanceof Error ? error.message : "The C++ timeline could not be generated");
    }
  }, [activeTest, blocks, roles]);

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

  const acceptGeneratedTimeline = useCallback(() => {
    if (!activeTest || !generatedTimeline || generatedTimeline.testId !== activeTest.id) return;
    if (activeTestLocked) {
      setToast("This test is locked");
      return;
    }
    const frames = generatedTimeline.frames;
    const final = generatedTimeline.final;
    const intermediate = frames.length && compareFrames(
      frames[frames.length - 1], final, activeTest.world).pass
      ? frames.slice(0, -1)
      : frames;
    setTests((current) => current.map((test) => test.id === activeTest.id
      ? {
        ...test,
        intermediate: intermediate.map(cloneFrame),
        expected: cloneFrame(final),
      }
      : test));
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
    setGeneratedTimeline(null);
    setFrameKind("expected");
    setIntermediateIndex(null);
    setShowResult(false);
    clearHistory();
    setToast(`${intermediate.length} intermediate ${intermediate.length === 1 ? "frame" : "frames"} and the final frame accepted`);
  }, [activeTest, activeTestLocked, clearHistory, generatedTimeline]);

  const activateGroupTool = useCallback(() => {
    setGenericPrompt(null);
    setGenericPromptError("");
    if (groupToolPinned) {
      const selectedCount = activeGroupSelection?.keys.length ?? 0;
      setGroupSelection(null);
      setToast(selectedCount
        ? `${selectedCount} ${selectedCount === 1 ? "cube" : "cubes"} deselected · Group tool remains active`
        : "Group selection is already clear · Group tool remains active");
      return;
    }
    setGroupToolPinned(true);
    setToast("Select group tool selected · press G again to clear selection");
  }, [activeGroupSelection, groupToolPinned]);

  const requestBlockSelection = useCallback((blockId: string) => {
    const block = blocks.find((definition) => definition.id === blockId);
    if (!block) return;
    setGroupToolPinned(false);
    setGroupSelection(null);
    if (genericRoleIds.has(block.roleId)) {
      setGenericPrompt({
        blockId,
        value: String(selectedGenericIds[blockId] ?? 0),
      });
      setGenericPromptError("");
      return;
    }
    setGenericPrompt(null);
    setSelectedBlock(blockId);
    setToast(`${block.name} selected`);
  }, [blocks, genericRoleIds, selectedGenericIds]);

  const selectToolbarRelative = useCallback((direction: -1 | 1) => {
    const slots = [DELETE_TOOL_ID, GROUP_TOOL_ID, ...blocks.map((block) => block.id)];
    const currentSlot = groupToolPinned ? GROUP_TOOL_ID : selectedBlock;
    const targetSlot = slots[offsetToolbarIndex(slots.indexOf(currentSlot), slots.length, direction)];
    setGenericPrompt(null);
    setGenericPromptError("");
    setGroupSelection(null);

    if (targetSlot === DELETE_TOOL_ID) {
      setGroupToolPinned(false);
      setSelectedBlock(DELETE_TOOL_ID);
      setToast("Erase tool selected");
      return;
    }
    if (targetSlot === GROUP_TOOL_ID) {
      setGroupToolPinned(true);
      setToast("Select group tool selected · click toggles · Shift-drag box-selects");
      return;
    }

    const block = blocks.find((definition) => definition.id === targetSlot);
    if (!block) return;
    setGroupToolPinned(false);
    setSelectedBlock(block.id);
    const genericId = genericBlockIds.has(block.id)
      ? selectedGenericIds[block.id] ?? 0
      : null;
    setToast(genericId === null ? `${block.name} selected` : `${block.name} ${genericId} selected`);
  }, [blocks, genericBlockIds, groupToolPinned, selectedBlock, selectedGenericIds]);

  const handleHorizontalToolbarKey = useCallback((direction: -1 | 1) => {
    if (!groupToolPinned && selectedDefinition && genericBlockIds.has(selectedDefinition.id)) {
      const currentId = selectedGenericIds[selectedDefinition.id] ?? 0;
      const nextId = offsetGenericObjectId(currentId, direction);
      if (nextId !== null) {
        setSelectedGenericIds((current) => ({ ...current, [selectedDefinition.id]: nextId }));
        setToast(`${selectedDefinition.name} ${nextId} selected`);
        return;
      }
    }
    selectToolbarRelative(direction);
  }, [genericBlockIds, groupToolPinned, selectToolbarRelative, selectedDefinition, selectedGenericIds]);

  const confirmGenericSelection = () => {
    if (!genericPrompt) return;
    const id = Number(genericPrompt.value.trim());
    if (!Number.isInteger(id) || id < 0 || id > 2147483647) {
      setGenericPromptError("Enter a whole number from 0 to 2,147,483,647.");
      return;
    }
    const block = blocks.find((definition) => definition.id === genericPrompt.blockId);
    if (!block) {
      setGenericPrompt(null);
      return;
    }
    setSelectedGenericIds((current) => ({ ...current, [block.id]: id }));
    setGroupToolPinned(false);
    setGroupSelection(null);
    setSelectedBlock(block.id);
    setGenericPrompt(null);
    setGenericPromptError("");
    setToast(`${block.name} ${id} selected · paint cubes to join generic object ${id}`);
  };

  const genericPromptBlockId = genericPrompt?.blockId ?? null;
  useEffect(() => {
    if (!genericPromptBlockId) return;
    const frameId = window.requestAnimationFrame(() => {
      genericIdInputRef.current?.focus();
      genericIdInputRef.current?.select();
    });
    return () => window.cancelAnimationFrame(frameId);
  }, [genericPromptBlockId]);

  const saveWorldDimensions = useCallback((width: number, height: number) => {
    if (!activeTest || activeTestLocked) {
      setToast("This test is locked");
      return;
    }
    const nextWorld = normalizeWorld({ width, height, floorLayer: 0 });
    if (nextWorld.width === activeTest.world.width && nextWorld.height === activeTest.world.height) {
      setToast(`${activeTest.name} dimensions unchanged`);
      return;
    }

    const boundedTest = cropTestToWorld(activeTest, nextWorld);
    const previousVoxelCount = activeTest.start.voxels.length +
      activeTest.intermediate.reduce((sum, frame) => sum + frame.voxels.length, 0) +
      activeTest.expected.voxels.length;
    const nextVoxelCount = boundedTest.start.voxels.length +
      boundedTest.intermediate.reduce((sum, frame) => sum + frame.voxels.length, 0) +
      boundedTest.expected.voxels.length;
    const removedVoxelCount = previousVoxelCount - nextVoxelCount;

    setTests((current) => current.map((test) => test.id === activeTest.id ? boundedTest : test));
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
    setShowResult(false);
    setGroupSelection(null);
    clearHistory();
    setToast(removedVoxelCount > 0
      ? `${activeTest.name} resized to ${nextWorld.width} × ${nextWorld.height} · removed ${removedVoxelCount} out-of-bounds voxels`
      : `${activeTest.name} resized to ${nextWorld.width} × ${nextWorld.height}`);
  }, [activeTest, activeTestLocked, clearHistory]);

  const restoreEditSnapshot = useCallback((snapshot: EditSnapshot, oppositeStack: React.MutableRefObject<EditSnapshot[]>) => {
    const currentTest = tests.find((test) => test.id === snapshot.testId);
    if (!currentTest || isTestLocked(currentTest)) return false;
    pushHistory(oppositeStack, {
      testId: snapshot.testId,
      frameKind: snapshot.frameKind,
      intermediateIndex: snapshot.intermediateIndex,
      frame: editableFrame(
        currentTest, snapshot.frameKind, snapshot.intermediateIndex),
    });
    setTests((current) => current.map((test) =>
      test.id === snapshot.testId
        ? replaceEditableFrame(
          test,
          snapshot.frameKind,
          snapshot.intermediateIndex,
          cloneFrame(snapshot.frame),
        )
        : test,
    ));
    setActiveId(snapshot.testId);
    setFrameKind(snapshot.frameKind);
    setIntermediateIndex(snapshot.intermediateIndex);
    setGroupSelection(null);
    setShowResult(false);
    setResults((current) => {
      const next = { ...current };
      delete next[snapshot.testId];
      return next;
    });
    return true;
  }, [isTestLocked, pushHistory, tests]);

  const undoPaint = useCallback(() => {
    if (activeTestLocked) {
      setToast("This test is locked");
      return;
    }
    const snapshot = undoStackRef.current.pop();
    if (!snapshot) {
      setToast("Nothing to undo");
      return;
    }
    if (restoreEditSnapshot(snapshot, redoStackRef)) {
      setToast("Undid the last paint stroke");
    }
    syncHistoryState();
  }, [activeTestLocked, restoreEditSnapshot, syncHistoryState]);

  const redoPaint = useCallback(() => {
    if (activeTestLocked) {
      setToast("This test is locked");
      return;
    }
    const snapshot = redoStackRef.current.pop();
    if (!snapshot) {
      setToast("Nothing to redo");
      return;
    }
    if (restoreEditSnapshot(snapshot, undoStackRef)) {
      setToast("Redid the paint stroke");
    }
    syncHistoryState();
  }, [activeTestLocked, restoreEditSnapshot, syncHistoryState]);

  const beginPaintGesture = useCallback(() => {
    paintGestureRef.current = { active: true, snapshotSaved: false };
  }, []);

  const endPaintGesture = useCallback(() => {
    paintGestureRef.current = { active: false, snapshotSaved: false };
  }, []);

  const selectVoxelGroup = useCallback((x: number, y: number, z: number, additive: boolean) => {
    if (!activeTest) return;
    const keys = selectConnectedVoxelGroup(activeFrame.voxels, { x, y, z });
    if (!keys.length) {
      if (!additive) setGroupSelection(null);
      setToast("No cube at that position");
      return;
    }
    const currentKeys = activeGroupSelection?.keys ?? [];
    const selection = toggleVoxelGroupSelection(currentKeys, keys, additive);
    if (selection.deselected) {
      setGroupSelection(selection.keys.length
        ? { frameKind, intermediateIndex, keys: selection.keys, testId: activeTest.id }
        : null);
      setToast(`${keys.length} ${keys.length === 1 ? "cube" : "cubes"} deselected`);
      return;
    }
    const previousKeys = additive ? currentKeys : [];
    const combinedKeys = selection.keys;
    setGroupSelection({ frameKind, intermediateIndex, keys: combinedKeys, testId: activeTest.id });
    const addedCount = combinedKeys.length - previousKeys.length;
    setToast(additive
      ? addedCount > 0
        ? `${addedCount} ${addedCount === 1 ? "cube" : "cubes"} added · ${combinedKeys.length} selected`
        : "That group is already selected"
      : `${keys.length} touching ${keys.length === 1 ? "cube" : "cubes"} selected · click again to deselect`);
  }, [activeFrame.voxels, activeGroupSelection, activeTest, frameKind, intermediateIndex]);

  const selectVoxelGroups = useCallback((origins: Array<{ x: number; y: number; z: number }>, additive: boolean) => {
    if (!activeTest) return;
    const selectedKeys = new Set<string>();
    let groupCount = 0;

    for (const origin of origins) {
      const originKey = `${origin.x},${origin.y},${origin.z}`;
      if (selectedKeys.has(originKey)) continue;
      const groupKeys = selectConnectedVoxelGroup(activeFrame.voxels, origin);
      if (!groupKeys.length) continue;
      groupCount += 1;
      groupKeys.forEach((key) => selectedKeys.add(key));
    }

    if (!selectedKeys.size) {
      setToast("No groups inside that rectangle");
      return;
    }

    const previousKeys = additive ? activeGroupSelection?.keys ?? [] : [];
    const combinedKeys = [...new Set([...previousKeys, ...selectedKeys])];
    setGroupSelection({ frameKind, intermediateIndex, keys: combinedKeys, testId: activeTest.id });
    const addedCount = combinedKeys.length - previousKeys.length;
    setToast(addedCount > 0
      ? `${groupCount} ${groupCount === 1 ? "group" : "groups"} boxed · ${addedCount} ${addedCount === 1 ? "cube" : "cubes"} added`
      : "Every group in that rectangle is already selected");
  }, [activeFrame.voxels, activeGroupSelection, activeTest, frameKind, intermediateIndex]);

  const moveSelectedGroup = useCallback((direction: Direction, verticalDelta = 0) => {
    if (!activeTest || !activeGroupSelection) return;
    if (activeTestLocked) {
      setToast("This test is locked");
      return;
    }
    const worldDirection = verticalDelta === 0
      ? cameraRelativeDirection(direction, cameraQuarterTurns) as Direction
      : direction;
    const delta = verticalDelta === 0 ? {
      up: { dx: 0, dy: -1 },
      right: { dx: 1, dy: 0 },
      down: { dx: 0, dy: 1 },
      left: { dx: -1, dy: 0 },
    }[worldDirection] : { dx: 0, dy: 0 };
    const currentFrame = editableFrame(activeTest, frameKind, intermediateIndex);
    const movement = moveVoxelGroup(
      currentFrame.voxels,
      activeGroupSelection.keys,
      delta.dx,
      delta.dy,
      activeWorld,
      verticalDelta,
    );
    if (!movement.moved) {
      setToast(verticalDelta
        ? "Group is blocked by another cube on that layer"
        : "Group is blocked by the room edge or another cube");
      return;
    }

    pushHistory(undoStackRef, {
      testId: activeTest.id,
      frameKind,
      intermediateIndex,
      frame: currentFrame,
    });
    redoStackRef.current = [];
    syncHistoryState();
    setTests((current) => current.map((test) =>
      test.id === activeTest.id
        ? replaceEditableFrame(
          test, frameKind, intermediateIndex, { voxels: movement.voxels })
        : test,
    ));
    setGroupSelection({
      testId: activeTest.id,
      frameKind,
      intermediateIndex,
      keys: movement.selectedKeys,
    });
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
    setShowResult(false);
    const movementLabel = verticalDelta > 0
      ? "raised one layer"
      : verticalDelta < 0
        ? "lowered one layer"
        : `moved ${direction} relative to camera`;
    setToast(`${movement.selectedKeys.length} ${movement.selectedKeys.length === 1 ? "cube" : "cubes"} ${movementLabel}`);
  }, [activeGroupSelection, activeTest, activeTestLocked, activeWorld, cameraQuarterTurns, frameKind, intermediateIndex, pushHistory, syncHistoryState]);

  const deleteSelectedGroup = useCallback(() => {
    if (!activeTest || !activeGroupSelection || !groupToolPinned) return;
    if (activeTestLocked) {
      setToast("This test is locked");
      return;
    }

    const currentFrame = editableFrame(activeTest, frameKind, intermediateIndex);
    const deletion = removeSelectedVoxels(currentFrame.voxels, activeGroupSelection.keys);
    if (!deletion.removedCount) {
      setGroupSelection(null);
      setToast("The selected cubes are no longer in this frame");
      return;
    }

    pushHistory(undoStackRef, {
      testId: activeTest.id,
      frameKind,
      intermediateIndex,
      frame: currentFrame,
    });
    redoStackRef.current = [];
    syncHistoryState();
    setTests((current) => current.map((test) =>
      test.id === activeTest.id
        ? replaceEditableFrame(
          test, frameKind, intermediateIndex, { voxels: deletion.voxels })
        : test,
    ));
    setGroupSelection(null);
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
    setShowResult(false);
    setToast(`${deletion.removedCount} ${deletion.removedCount === 1 ? "cube" : "cubes"} deleted · undo to restore`);
  }, [activeGroupSelection, activeTest, activeTestLocked, frameKind, groupToolPinned, intermediateIndex, pushHistory, syncHistoryState]);

  const selectTimelineRelative = useCallback((offset: -1 | 1) => {
    if (!activeTest) return;
    const selection = offsetTimelineSelection(
      frameKind,
      intermediateIndex,
      activeTest.intermediate.length,
      offset,
    );
    setFrameKind(selection.frameKind as FrameKind);
    setIntermediateIndex(selection.intermediateIndex);
    setGroupSelection(null);
    const label = selection.intermediateIndex === null
      ? selection.frameKind === "start" ? "Start" : "Expected"
      : `Tick ${selection.intermediateIndex + 1}`;
    setToast(`${label} frame selected`);
  }, [activeTest, frameKind, intermediateIndex]);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement;
      if (["INPUT", "SELECT", "TEXTAREA"].includes(target.tagName)) return;
      const key = event.key.toLowerCase();
      if (!event.metaKey && !event.ctrlKey && !event.altKey && key === "g") {
        event.preventDefault();
        if (event.repeat) return;
        activateGroupTool();
        return;
      }
      if (
        !event.metaKey &&
        !event.ctrlKey &&
        !event.altKey &&
        (key === "delete" || key === "backspace") &&
        groupToolPinned &&
        activeGroupSelection
      ) {
        event.preventDefault();
        if (!event.repeat) deleteSelectedGroup();
        return;
      }
      const groupDirection = {
        arrowup: "up",
        arrowright: "right",
        arrowdown: "down",
        arrowleft: "left",
      }[key] as Direction | undefined;
      if (
        !event.metaKey &&
        !event.ctrlKey &&
        !event.altKey &&
        event.shiftKey &&
        (key === "arrowleft" || key === "arrowright")
      ) {
        event.preventDefault();
        if (!event.repeat) selectTimelineRelative(key === "arrowleft" ? -1 : 1);
        return;
      }
      if (!event.metaKey && !event.ctrlKey && !event.altKey && groupDirection && activeGroupSelection) {
        event.preventDefault();
        const verticalDelta = event.shiftKey && (groupDirection === "up" || groupDirection === "down")
          ? groupDirection === "up" ? 1 : -1
          : 0;
        moveSelectedGroup(groupDirection, verticalDelta);
        return;
      }
      if (!event.metaKey && !event.ctrlKey && !event.altKey && (key === "arrowleft" || key === "arrowright")) {
        event.preventDefault();
        handleHorizontalToolbarKey(key === "arrowleft" ? -1 : 1);
        return;
      }
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
        setGenericPrompt(null);
        setGroupToolPinned(false);
        setGroupSelection(null);
        setSelectedBlock(DELETE_TOOL_ID);
        setToast("Erase tool selected");
        return;
      }
      if (!event.metaKey && !event.ctrlKey && !event.altKey && /^[1-9]$/.test(key)) {
        const definition = blocks[Number(key) - 1];
        if (definition) {
          event.preventDefault();
          requestBlockSelection(definition.id);
          return;
        }
      }
      if ((event.metaKey || event.ctrlKey) && event.key === "Enter" && activeTest) {
        event.preventDefault();
        runTest(activeTest);
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [activateGroupTool, activeGroupSelection, activeTest, blocks, deleteSelectedGroup, groupToolPinned, handleHorizontalToolbarKey, moveSelectedGroup, redoPaint, requestBlockSelection, runTest, selectTimelineRelative, undoPaint]);

  const paint = (x: number, y: number, z: number, blockId: string | null) => {
    if (!activeTest || activeTestLocked) return;
    const currentFrame = editableFrame(activeTest, frameKind, intermediateIndex);
    const currentVoxel = currentFrame.voxels.find((voxel) => keyOf(voxel) === `${x},${y},${z}`);
    const genericId = blockId && genericBlockIds.has(blockId)
      ? selectedGenericIds[blockId] ?? 0
      : undefined;
    if (
      (!blockId && !currentVoxel) ||
      (currentVoxel?.blockId === blockId && currentVoxel.genericId === genericId)
    ) return;
    setGroupSelection(null);
    if (!paintGestureRef.current.active || !paintGestureRef.current.snapshotSaved) {
      pushHistory(undoStackRef, {
        testId: activeTest.id,
        frameKind,
        intermediateIndex,
        frame: currentFrame,
      });
      redoStackRef.current = [];
      syncHistoryState();
      paintGestureRef.current.snapshotSaved = true;
    }
    setTests((current) => current.map((test) => {
      if (test.id !== activeTest.id) return test;
      const frame = cloneFrame(editableFrame(test, frameKind, intermediateIndex));
      frame.voxels = frame.voxels.filter((voxel) => keyOf(voxel) !== `${x},${y},${z}`);
      if (blockId) {
        frame.voxels.push(genericId === undefined
          ? { x, y, z, blockId }
          : { x, y, z, blockId, genericId });
      }
      return replaceEditableFrame(test, frameKind, intermediateIndex, frame);
    }));
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
  };

  const updateActive = (patch: Partial<TestCase>) => {
    if (!activeTest || activeTestLocked) {
      setToast("This test is locked");
      return;
    }
    setTests((current) => current.map((test) => test.id === activeTest.id ? { ...test, ...patch } : test));
  };

  const addFolder = () => {
    const name = newFolderName.trim();
    if (!name) return;
    const baseId = name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "folder";
    const existingIds = new Set(folders.map((folder) => folder.id));
    let id = baseId;
    let suffix = 2;
    while (existingIds.has(id)) {
      id = `${baseId}-${suffix}`;
      suffix += 1;
    }
    setFolders((current) => [...current, { collapsed: false, id, locked: false, name }]);
    setNewFolderName("");
    setAddingFolder(false);
    setToast(`${name} test folder created`);
  };

  const toggleFolderCollapsed = (folderId: string) => {
    setFolders((current) => current.map((folder) =>
      folder.id === folderId ? { ...folder, collapsed: !folder.collapsed } : folder,
    ));
  };

  const toggleFolderLocked = (folderId: string) => {
    const folder = folders.find((item) => item.id === folderId);
    if (!folder) return;
    const locked = !folder.locked;
    setFolders((current) => current.map((item) =>
      item.id === folderId ? { ...item, locked } : item,
    ));
    endPaintGesture();
    setToast(`${folder.name} ${locked ? "locked" : "unlocked"}`);
  };

  const toggleTestLocked = (testId: string) => {
    const test = tests.find((item) => item.id === testId);
    if (!test) return;
    if (lockedFolderIds.has(test.folderId)) {
      setToast("Unlock the suite folder before changing this test lock");
      return;
    }
    const locked = !test.locked;
    setTests((current) => current.map((item) =>
      item.id === testId ? { ...item, locked } : item,
    ));
    endPaintGesture();
    setToast(`${test.name} ${locked ? "locked" : "unlocked"}`);
  };

  const addTest = (requestedFolderId?: string) => {
    const requestedFolder = folders.find((folder) => folder.id === requestedFolderId);
    const activeFolder = folders.find((folder) => folder.id === activeTest?.folderId);
    const targetFolder = requestedFolder && !requestedFolder.locked
      ? requestedFolder
      : activeFolder && !activeFolder.locked
        ? activeFolder
        : folders.find((folder) => !folder.locked);
    if (!targetFolder) {
      setToast("Unlock a suite folder before adding a test");
      return;
    }
    const existingIds = new Set(tests.map((test) => test.id));
    let suffix = tests.length + 1;
    let id = `test-${suffix}`;
    while (existingIds.has(id)) {
      suffix += 1;
      id = `test-${suffix}`;
    }
    const testWorld = activeTest ? { ...activeTest.world } : { ...DEFAULT_WORLD };
    const source = activeTest?.start ?? { voxels: floorFrame(testWorld.width, testWorld.height) };
    const folderId = targetFolder.id;
    const test: TestCase = {
      description: "",
      folderId,
      id,
      locked: false,
      name: `Untitled test ${tests.length + 1}`,
      input: "up",
      intermediate: [],
      start: cloneFrame(source),
      expected: cloneFrame(source),
      world: testWorld,
    };
    setTests((current) => [...current, test]);
    setActiveId(id);
    setFrameKind("start");
    setIntermediateIndex(null);
    setShowResult(false);
    setToast(`New test created in ${folders.find((folder) => folder.id === folderId)?.name ?? "test folder"}`);
  };

  const reorderTest = (testId: string, offset: -1 | 1) => {
    const test = tests.find((item) => item.id === testId);
    if (!test) return;
    const folder = folders.find((item) => item.id === test.folderId);
    const folderTests = tests.filter((item) => item.folderId === test.folderId);
    const sourceIndex = folderTests.findIndex((item) => item.id === testId);
    const target = folderTests[sourceIndex + offset];
    if (folder?.locked || test.locked || !target || target.locked) {
      setToast(test.locked || target?.locked ? "Locked tests cannot be reordered" : "This test cannot move farther in its folder");
      return;
    }
    setTests((current) => {
      const sourceGlobalIndex = current.findIndex((item) => item.id === test.id);
      const targetGlobalIndex = current.findIndex((item) => item.id === target.id);
      if (sourceGlobalIndex < 0 || targetGlobalIndex < 0) return current;
      const next = [...current];
      [next[sourceGlobalIndex], next[targetGlobalIndex]] = [next[targetGlobalIndex], next[sourceGlobalIndex]];
      return next;
    });
    setToast(`${test.name} moved ${offset < 0 ? "up" : "down"}`);
  };

  const duplicateTest = (testId: string) => {
    const source = tests.find((test) => test.id === testId);
    if (!source) return;
    const folder = folders.find((item) => item.id === source.folderId);
    if (folder?.locked) {
      setToast("Unlock the suite folder before duplicating a test");
      return;
    }
    const existingIds = new Set(tests.map((test) => test.id));
    const baseId = `${source.id}-copy`;
    let id = baseId;
    let suffix = 2;
    while (existingIds.has(id)) {
      id = `${baseId}-${suffix}`;
      suffix += 1;
    }
    const duplicate: TestCase = {
      ...source,
      expected: cloneFrame(source.expected),
      intermediate: source.intermediate.map(cloneFrame),
      id,
      locked: false,
      name: `${source.name} copy`,
      start: cloneFrame(source.start),
      world: { ...source.world },
    };
    setTests((current) => {
      const sourceIndex = current.findIndex((test) => test.id === source.id);
      const next = [...current];
      next.splice(sourceIndex + 1, 0, duplicate);
      return next;
    });
    setActiveId(id);
    setFrameKind("start");
    setIntermediateIndex(null);
    setShowResult(false);
    setToast(`${source.name} duplicated`);
  };

  const deleteTest = (testId: string) => {
    const source = tests.find((test) => test.id === testId);
    if (!source) return;
    const folder = folders.find((item) => item.id === source.folderId);
    if (source.locked || folder?.locked) {
      setToast("Unlock this test and its suite folder before deleting it");
      return;
    }
    if (tests.length <= 1) {
      setToast("The editor needs at least one test case");
      return;
    }
    if (!window.confirm(`Delete “${source.name}”? This cannot be undone.`)) return;

    const deletion = deleteTestCase(tests, testId, activeId);
    if (!deletion.removed) return;
    setTests(deletion.tests);
    setActiveId(deletion.activeId);
    setResults((current) => {
      const next = { ...current };
      delete next[testId];
      return next;
    });
    if (activeId === testId) {
      setFrameKind("start");
      setIntermediateIndex(null);
      setShowResult(false);
    }
    setGroupSelection((current) => current?.testId === testId ? null : current);
    undoStackRef.current = undoStackRef.current.filter((snapshot) => snapshot.testId !== testId);
    redoStackRef.current = redoStackRef.current.filter((snapshot) => snapshot.testId !== testId);
    syncHistoryState();
    endPaintGesture();
    setToast(`${source.name} deleted`);
  };

  const duplicateFrame = () => {
    if (!activeTest || activeTestLocked) {
      setToast("This test is locked");
      return;
    }
    pushHistory(undoStackRef, {
      testId: activeTest.id,
      frameKind: "expected",
      intermediateIndex: null,
      frame: activeTest.expected,
    });
    redoStackRef.current = [];
    syncHistoryState();
    setTests((current) => current.map((test) =>
      test.id === activeTest.id
        ? { ...test, expected: cropFrameToWorld(test.start, test.world) }
        : test,
    ));
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
    setFrameKind("expected");
    setIntermediateIndex(null);
    setShowResult(false);
    setToast("Start frame copied to expected");
  };

  const addIntermediateFrame = () => {
    if (!activeTest || activeTestLocked) {
      setToast("This test is locked");
      return;
    }
    const insertion = insertIntermediateFrame(activeTest, frameKind, intermediateIndex);
    setTests((current) => current.map((test) =>
      test.id === activeTest.id ? insertion.test : test));
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
    setGeneratedTimeline(null);
    setGroupSelection(null);
    setFrameKind("expected");
    setIntermediateIndex(insertion.insertionIndex);
    setShowResult(false);
    clearHistory();
    setToast(`Tick ${insertion.insertionIndex + 1} added as a copy of the previous frame`);
  };

  const copyPreviousFrame = () => {
    if (!activeTest || activeTestLocked) {
      setToast("This test is locked");
      return;
    }
    const source = previousExpectedFrame(activeTest, frameKind, intermediateIndex);
    if (!source) {
      setToast("The Start frame has no previous frame");
      return;
    }
    pushHistory(undoStackRef, {
      testId: activeTest.id,
      frameKind,
      intermediateIndex,
      frame: editableFrame(activeTest, frameKind, intermediateIndex),
    });
    redoStackRef.current = [];
    syncHistoryState();
    setTests((current) => current.map((test) =>
      test.id === activeTest.id
        ? replaceEditableFrame(test, frameKind, intermediateIndex, cloneFrame(source))
        : test));
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
    setGeneratedTimeline(null);
    setGroupSelection(null);
    setShowResult(false);
    setToast(`${intermediateIndex === null ? "Expected" : `Tick ${intermediateIndex + 1}`} copied from its previous frame`);
  };

  const deleteIntermediateFrame = () => {
    if (!activeTest || intermediateIndex === null || activeTestLocked) {
      setToast(activeTestLocked ? "This test is locked" : "Select an intermediate tick first");
      return;
    }
    const removedTick = intermediateIndex + 1;
    const remainingCount = activeTest.intermediate.length - 1;
    setTests((current) => current.map((test) => {
      if (test.id !== activeTest.id) return test;
      return {
        ...test,
        intermediate: test.intermediate.filter((_, index) => index !== intermediateIndex),
      };
    }));
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
    setGeneratedTimeline(null);
    setGroupSelection(null);
    setShowResult(false);
    clearHistory();
    if (remainingCount === 0) {
      setFrameKind("expected");
      setIntermediateIndex(null);
    } else {
      setIntermediateIndex(Math.min(intermediateIndex, remainingCount - 1));
    }
    setToast(`Tick ${removedTick} removed from the expected timeline`);
  };

  const resetRoom = () => {
    if (!activeTest || activeTestLocked) {
      setToast("This test is locked");
      return;
    }
    pushHistory(undoStackRef, {
      testId: activeTest.id,
      frameKind,
      intermediateIndex,
      frame: editableFrame(activeTest, frameKind, intermediateIndex),
    });
    redoStackRef.current = [];
    syncHistoryState();
    setTests((current) => current.map((test) =>
      test.id === activeTest.id
        ? replaceEditableFrame(
          test,
          frameKind,
          intermediateIndex,
          defaultRoomFloor(test.world.width, test.world.height),
        )
        : test,
    ));
    setResults((current) => {
      const next = { ...current };
      delete next[activeTest.id];
      return next;
    });
    setShowResult(false);
    setToast(`${intermediateIndex === null
      ? frameKind === "start" ? "Start" : "Expected"
      : `Tick ${intermediateIndex + 1}`} room reset to floor tiles`);
  };

  const addBlock = () => {
    const name = newBlock.name.trim();
    if (!name) return;
    const baseId = name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "block";
    const existingIds = new Set(blocks.map((block) => block.id));
    let id = baseId;
    let suffix = 2;
    while (existingIds.has(id)) {
      id = `${baseId}-${suffix}`;
      suffix += 1;
    }
    const isGeneric = roles.find((role) => role.id === newBlock.roleId)?.generic === true;
    setBlocks((current) => [...current, { id, ...newBlock, name }]);
    if (isGeneric) {
      setGenericPrompt({ blockId: id, value: "0" });
      setGenericPromptError("");
    } else {
      setSelectedBlock(id);
    }
    setNewBlock({ name: "", color: "#D96B5F", roleId: roles[0]?.id ?? "solid" });
    setAddingBlock(false);
    setToast(isGeneric ? `${name} created · choose its generic object ID` : `${name} block created`);
  };

  const addRole = () => {
    const name = newRole.name.trim();
    if (!name) return;
    const baseId = name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "role";
    const existingIds = new Set(roles.map((role) => role.id));
    let id = baseId;
    let suffix = 2;
    while (existingIds.has(id)) {
      id = `${baseId}-${suffix}`;
      suffix += 1;
    }
    const role: PhysicsRoleDefinition = {
      id,
      name,
      description: newRole.description.trim(),
      generic: newRole.generic,
    };
    setRoles((current) => [...current, role]);
    setSelectedRoleId(id);
    setNewRole({ name: "", description: "", generic: false });
    setAddingRole(false);
    setToast(`${name} role created · ready for C++ behavior`);
  };

  const updateSelectedRole = (patch: Partial<Pick<PhysicsRoleDefinition, "name" | "description" | "generic">>) => {
    setRoles((current) => current.map((role) =>
      role.id === selectedRoleId ? { ...role, ...patch } : role,
    ));
  };

  const exportProject = () => {
    const boundedTests = cropTestsToWorld(tests);
    const payload = JSON.stringify({
      schemaVersion: 8,
      coordinateSystem: { horizontalAxes: ["x", "y"], verticalAxis: "z", floorLayer: 0 },
      roles,
      blocks,
      folders,
      tests: boundedTests.map((test) => ({
        ...test,
        start: { voxels: sortVoxels(test.start.voxels) },
        intermediate: test.intermediate.map((frame) => ({
          voxels: sortVoxels(frame.voxels),
        })),
        expected: { voxels: sortVoxels(test.expected.voxels) },
      })),
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
        const parsed = JSON.parse(text) as {
          blocks: StoredBlockDefinition[];
          folders?: StoredTestFolder[];
          roles?: PhysicsRoleDefinition[];
          tests: StoredTestCase[];
          world?: WorldSettings;
        };
        if (!parsed.blocks?.length || !parsed.tests?.length) throw new Error("Invalid project");
        const legacyWorld = normalizeWorld(parsed.world ?? DEFAULT_WORLD);
        const importedRoles = normalizeRoles(parsed.roles);
        const importedFolders = normalizeFolders(parsed.folders);
        const importedBlocks = normalizeBlocks(parsed.blocks, importedRoles);
        const importedTests = normalizeTests(
          parsed.tests,
          importedFolders,
          importedBlocks,
          importedRoles,
          parsed.blocks,
          legacyWorld,
        );
        setRoles(importedRoles);
        setFolders(importedFolders);
        setBlocks(importedBlocks);
        setNewBlock((current) => ({
          ...current,
          roleId: importedRoles.some((role) => role.id === current.roleId)
            ? current.roleId
            : importedRoles[0].id,
        }));
        setTests(importedTests);
        setActiveId(importedTests[0].id);
        setFrameKind("start");
        setIntermediateIndex(null);
        setGeneratedTimeline(null);
        setResults({});
        clearHistory();
        setSelectedRoleId(importedRoles[0].id);
        setToast(`Imported ${importedTests.length} tests in ${importedFolders.length} folders`);
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
          {toast && <p className="author-status" role="status"><span />{toast}</p>}
        </div>
      </header>

      <section className="author-layout">
        <section className="author-workspace">
          <section className="author-stage" aria-label="Voxel frame editor">
            <div className="stage-chrome stage-chrome--left">
              <div className="frame-switch" role="group" aria-label="Frame to edit">
                <button className={frameKind === "start" && intermediateIndex === null ? "active" : ""} onClick={() => { setFrameKind("start"); setIntermediateIndex(null); setGroupSelection(null); }}><span>01</span> Start</button>
                {activeTest.intermediate.map((_, index) => (
                  <button key={index} className={intermediateIndex === index ? "active" : ""} onClick={() => { setFrameKind("expected"); setIntermediateIndex(index); setGroupSelection(null); }}><span>{String(index + 2).padStart(2, "0")}</span> Tick {index + 1}</button>
                ))}
                <button className={frameKind === "expected" && intermediateIndex === null ? "active" : ""} onClick={() => { setFrameKind("expected"); setIntermediateIndex(null); setGroupSelection(null); }}><span>{String(activeTest.intermediate.length + 2).padStart(2, "0")}</span> Expected</button>
              </div>
              <div className="stage-history" role="group" aria-label="Paint history">
                <button type="button" aria-label="Undo paint" title="Undo paint · ⌘Z" disabled={activeTestLocked || !historyState.canUndo} onClick={undoPaint}>↶</button>
                <button type="button" aria-label="Redo paint" title="Redo paint · ⇧⌘Z" disabled={activeTestLocked || !historyState.canRedo} onClick={redoPaint}>↷</button>
              </div>
              {frameKind === "expected" && intermediateIndex === null && <button type="button" className="copy-start-button" title="Replace expected room with a copy of the start room" disabled={activeTestLocked} onClick={duplicateFrame}>Copy from start room</button>}
              <button type="button" className="add-tick-button" title="Insert an expected tick after the selected frame" disabled={activeTestLocked} onClick={addIntermediateFrame}>Add tick</button>
              {frameKind === "expected" && <button type="button" className="copy-previous-button" title="Replace this frame with a copy of the frame immediately before it" disabled={activeTestLocked} onClick={copyPreviousFrame}>Copy previous</button>}
              {intermediateIndex !== null && <button type="button" className="delete-tick-button" title={`Remove expected tick ${intermediateIndex + 1}`} disabled={activeTestLocked} onClick={deleteIntermediateFrame}>Delete tick</button>}
              <button type="button" className="generate-timeline-button" disabled={activeTestLocked} onClick={generateTimeline}>Auto-generate frames</button>
              <button type="button" className="reset-room-button" disabled={activeTestLocked} onClick={resetRoom}>Reset room</button>
            </div>
            <div className="stage-chrome stage-chrome--right">
              {activeTestLocked && <span className="lock-pill"><span className="lock-glyph" aria-hidden="true" /> Read only</span>}
              <span className="coordinate-pill">{activeWorld.width} × {activeWorld.height} × ∞</span>
            </div>
            <MazeBenchCanvas frame={activeFrame} blocks={blocks} genericBlockIds={genericBlockIds} world={activeWorld} layer={layer} selectedVoxelKeys={selectedVoxelKeys} selectionMode={groupSelectionMode} selectedBlock={selectedBlock} eraseMode={selectedBlock === DELETE_TOOL_ID} interactive paintable={!activeTestLocked} onCameraQuarterTurnChange={setCameraQuarterTurns} onSelectVoxel={selectVoxelGroup} onSelectVoxels={selectVoxelGroups} onPaint={paint} onPaintGestureStart={beginPaintGesture} onPaintGestureEnd={endPaintGesture} />
            {generatedTimeline?.testId === activeTest.id && (
              <section className="timeline-review" aria-label="Generated C++ timeline review">
                <div className="timeline-review__heading"><div><span>C++ GENERATED · NOT SAVED</span><strong>{generatedTimeline.frames.length} tick {generatedTimeline.frames.length === 1 ? "frame" : "frames"}</strong></div><div><button className="tool-button" onClick={() => setGeneratedTimeline(null)}>Discard</button><button className="tool-button tool-button--primary" disabled={activeTestLocked} onClick={acceptGeneratedTimeline}>Accept frames</button></div></div>
                <TimelineSnapshotStrip
                  key={generatedTimeline.generationId}
                  blocks={blocks}
                  final={generatedTimeline.final}
                  frames={generatedTimeline.frames}
                  genericBlockIds={genericBlockIds}
                  layer={layer}
                  start={activeTest.start}
                  world={activeWorld}
                />
              </section>
            )}
            <div className="author-hotbar" aria-label="Block palette · Left and right arrows choose tools">
              <span className="author-hotbar__toolname is-visible">{selectedToolName}</span>
              <div className="author-hotbar__slots">
                <button type="button" className={`author-hotbar__slot author-hotbar__eraser ${!groupSelectionMode && !activeGroupSelection && selectedBlock === DELETE_TOOL_ID ? "is-active" : ""}`} aria-label="Erase tool" title="Erase cube · E · ←/→ chooses tools" onClick={() => { setGenericPrompt(null); setGroupToolPinned(false); setGroupSelection(null); setSelectedBlock(DELETE_TOOL_ID); setToast("Erase tool selected"); }}>
                  <span className="author-hotbar__key">E</span>
                  <svg className="author-tool-icon author-tool-icon--eraser" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" focusable="false"><path d="m7 21-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 1 1 2.5 0 3.4L13 21" /><path d="M22 21H7" /><path d="m5 11 9 9" /></svg>
                </button>
                <button type="button" className={`author-hotbar__slot author-hotbar__group ${groupSelectionMode || activeGroupSelection ? "is-active" : ""}`} aria-label="Select group tool" aria-pressed={groupToolPinned} title="Camera-relative arrows · Shift+↑/↓ changes layer · Delete erases" onClick={activateGroupTool}>
                  <span className="author-hotbar__key">G</span>
                  <span className="group-tool-icon" aria-hidden="true"><i /><i /><i /></span>
                </button>
                {blocks.map((block, index) => {
                  const generic = genericBlockIds.has(block.id);
                  const genericLabel = generic
                    ? genericToolbarLabel(
                      block.id,
                      selectedBlock,
                      selectedGenericIds[block.id] ?? 0,
                      groupToolPinned || Boolean(genericPrompt),
                    )
                    : undefined;
                  return (
                    <button key={block.id} className={`author-hotbar__slot ${!groupSelectionMode && !activeGroupSelection && selectedBlock === block.id ? "is-active" : ""}`} title={`${block.name} — ${generic ? `generic object ${selectedGenericIds[block.id] ?? 0} · ←/→ changes ID` : `${roles.find((role) => role.id === block.roleId)?.name ?? block.roleId} · ←/→ chooses tools`}`} onClick={() => requestBlockSelection(block.id)}>
                      <span className="author-hotbar__key">{index + 1}</span>
                      <span className={`swatch-cube ${generic ? "generic" : ""} ${genericLabel && genericLabel.length > 5 ? "generic-label-long" : genericLabel && genericLabel.length > 2 ? "generic-label-medium" : ""}`} data-generic-label={genericLabel} style={{ "--block-color": block.color } as React.CSSProperties} />
                    </button>
                  );
                })}
                <span className="author-hotbar__divider" />
                <button className="author-hotbar__slot author-hotbar__add" aria-label="Define a new block" onClick={() => setAddingBlock(true)}>＋</button>
              </div>
            </div>
            {genericPrompt && (
              <div className="generic-id-backdrop" role="presentation" onPointerDown={(event) => { if (event.target === event.currentTarget) setGenericPrompt(null); }}>
                <section className="generic-id-modal" role="dialog" aria-modal="true" aria-labelledby="generic-id-title" aria-describedby="generic-id-message">
                  <span className="generic-id-modal__eyebrow">Generic numbered family</span>
                  <h2 id="generic-id-title">Choose {blocks.find((block) => block.id === genericPrompt.blockId)?.name ?? "object"} ID</h2>
                  <p id="generic-id-message">Cubes painted with the same number belong to the same generic object.</p>
                  <label className="field"><span>ID (0 or more)</span><input ref={genericIdInputRef} inputMode="numeric" value={genericPrompt.value} aria-invalid={Boolean(genericPromptError)} onChange={(event) => { setGenericPrompt((current) => current ? { ...current, value: event.target.value } : current); setGenericPromptError(""); }} onKeyDown={(event) => { if (event.key === "Enter") { event.preventDefault(); confirmGenericSelection(); } else if (event.key === "Escape") { event.preventDefault(); setGenericPrompt(null); } }} /></label>
                  {genericPromptError && <small className="generic-id-modal__error" role="alert">{genericPromptError}</small>}
                  <div className="generic-id-modal__actions"><button className="tool-button" type="button" onClick={() => setGenericPrompt(null)}>Cancel</button><button className="tool-button tool-button--primary" type="button" onClick={confirmGenericSelection}>Select</button></div>
                </section>
              </div>
            )}
            {showResult && activeResult && (
              <div className={`result-console ${activeResult.pass ? "passed" : "failed"}`}>
                <div className="result-heading">
                  <div className="result-mark">{activeResult.pass ? "✓" : "!"}</div>
                  <div><span>C++ ENGINE RESULT · {activeResult.pass ? "4/4 ROTATIONS" : `${activeResult.rotation}° · ${activeResult.input.toUpperCase()}${activeResult.tick === undefined ? "" : ` · TICK ${activeResult.tick}`}`}</span><h3>{activeResult.pass ? "All 4 rotations match" : `${activeResult.missing.length + activeResult.unexpected.length} voxel differences${activeResult.tick === undefined ? "" : ` on tick ${activeResult.tick}`}`}</h3></div>
                  <button aria-label="Close comparison" onClick={() => setShowResult(false)}>×</button>
                </div>
                <div className="compare-grid">
                  <div className="compare-card"><div><strong>EXPECTED{activeResult.tick === undefined ? "" : ` TICK ${activeResult.tick}`} · {activeResult.rotation}°</strong><span>{activeResult.missing.length ? `${activeResult.missing.length} missing` : "reference"}</span></div><MazeBenchCanvas frame={activeResult.expected} blocks={blocks} genericBlockIds={genericBlockIds} world={activeResult.world} layer={layer} compact /></div>
                  <div className="compare-card"><div><strong>ENGINE OUTPUT{activeResult.tick === undefined ? "" : ` TICK ${activeResult.tick}`} · <DirectionIcon direction={activeResult.input} /></strong><span>{activeResult.unexpected.length ? `${activeResult.unexpected.length} unexpected` : "exact"}</span></div><MazeBenchCanvas frame={activeResult.actual} blocks={blocks} genericBlockIds={genericBlockIds} world={activeResult.world} layer={layer} compact /></div>
                </div>
              </div>
            )}
          </section>
        </section>

        <aside className="author-sidebar">
          <details className="author-panel" open>
            <summary><span className="chevron">▸</span><span>Test Case</span><em>canonical up <DirectionIcon direction="up" /></em></summary>
            <div className="author-panel__body">
              <label className="field"><span>Name</span><input disabled={activeTestLocked} value={activeTest.name} onChange={(event) => updateActive({ name: event.target.value })} /></label>
              <label className="field"><span>Description</span><textarea rows={3} disabled={activeTestLocked} value={activeTest.description} placeholder="Describe the intended transition and invariants for debugging agents." onChange={(event) => updateActive({ description: event.target.value })} /></label>
              <label className="field"><span>Suite folder</span><select disabled={activeTestLocked} value={activeTest.folderId} onChange={(event) => updateActive({ folderId: event.target.value })}>{folders.map((folder) => <option key={folder.id} value={folder.id} disabled={folder.locked}>{folder.name}{folder.locked ? " · locked" : ""}</option>)}</select></label>
              <div className="field"><span>Movement input</span><div className="canonical-input"><strong><DirectionIcon direction="up" /> Up</strong><small>Authored once; automatically checked as ↑ → ↓ ← by rotating the entire level.</small></div></div>
              <button className="tool-button tool-button--primary full" onClick={() => runTest(activeTest)}>Run test</button>
            </div>
          </details>

          <details className="author-panel suite-panel" open>
            <summary><span className="chevron">▸</span><span>Test Suite</span><em className="suite-count">{Object.keys(results).length ? `${counts.passed}/${tests.length}` : tests.length}</em></summary>
            <div className="author-panel__body">
              <div className="summary-track"><i style={{ width: Object.keys(results).length ? `${(counts.passed / tests.length) * 100}%` : "0%" }} /></div>
              <div className="test-folders">
                {folders.map((folder) => {
                  const folderTests = tests.filter((test) => test.folderId === folder.id);
                  const completedResults = folderTests.map((test) => results[test.id]).filter(Boolean);
                  const passedTests = completedResults.filter((result) => result.pass).length;
                  return (
                    <section className={`test-folder ${folder.collapsed ? "collapsed" : ""} ${folder.locked ? "locked" : ""}`} key={folder.id} aria-label={`${folder.name} test folder`}>
                      <div className="test-folder__header">
                        <button className="folder-collapse-button" type="button" aria-label={`${folder.collapsed ? "Expand" : "Collapse"} ${folder.name}`} aria-expanded={!folder.collapsed} onClick={() => toggleFolderCollapsed(folder.id)}><span aria-hidden="true">▾</span></button>
                        <input aria-label={`Rename ${folder.name} folder`} disabled={folder.locked} value={folder.name} onChange={(event) => setFolders((current) => current.map((item) => item.id === folder.id ? { ...item, name: event.target.value } : item))} />
                        <button className={`folder-lock-button ${folder.locked ? "active" : ""}`} type="button" aria-label={`${folder.locked ? "Unlock" : "Lock"} ${folder.name} folder`} title={`${folder.locked ? "Unlock" : "Lock"} folder`} onClick={() => toggleFolderLocked(folder.id)}><span className="lock-glyph" aria-hidden="true" /></button>
                        <em>{completedResults.length ? `${passedTests}/${folderTests.length}` : folderTests.length}</em>
                        <button className="folder-add-button" type="button" disabled={folder.locked} aria-label={`Add test to ${folder.name}`} title={folder.locked ? "Unlock folder to add tests" : `Add test to ${folder.name}`} onClick={() => addTest(folder.id)}>＋</button>
                      </div>
                      {!folder.collapsed && <div className="test-list">
                        {folderTests.length ? folderTests.map((test) => {
                          const result = results[test.id];
                          const passedRotations = result?.checks.filter((check) => check.pass).length;
                          const folderIndex = folderTests.findIndex((item) => item.id === test.id);
                          const locked = test.locked || folder.locked;
                          const previousLocked = folderTests[folderIndex - 1]?.locked === true;
                          const nextLocked = folderTests[folderIndex + 1]?.locked === true;
                          return (
                            <div className={`test-card-row ${locked ? "locked" : ""}`} key={test.id}>
                              <button className={`test-card ${test.id === activeId ? "active" : ""}`} onClick={() => { setActiveId(test.id); setFrameKind("start"); setIntermediateIndex(null); setGeneratedTimeline(null); setGroupSelection(null); setShowResult(Boolean(results[test.id])); }}>
                                <span className={`test-status ${!result ? "idle" : result.pass ? "pass" : "fail"}`}>{!result ? folderIndex + 1 : result.pass ? "✓" : "!"}</span>
                                <span className="test-copy"><strong>{test.name}</strong><span className="test-copy__description">{test.description || "No description yet"}</span><small><DirectionIcon direction="up" /> up · {result ? `${passedRotations}/4 rotations` : "4 rotations"} · {test.world.width}×{test.world.height} · {cropFrameToWorld(test.start, test.world).voxels.length} voxels</small></span>
                              </button>
                              <div className="test-card-actions">
                                <button className="test-card-action" type="button" disabled={folder.locked || test.locked || folderIndex === 0 || previousLocked} aria-label={`Move ${test.name} up`} title="Move up" onClick={() => reorderTest(test.id, -1)}>↑</button>
                                <button className="test-card-action" type="button" disabled={folder.locked} aria-label={`Duplicate ${test.name}`} title="Duplicate test" onClick={() => duplicateTest(test.id)}>⧉</button>
                                <button className="test-card-action" type="button" disabled={folder.locked || test.locked || folderIndex === folderTests.length - 1 || nextLocked} aria-label={`Move ${test.name} down`} title="Move down" onClick={() => reorderTest(test.id, 1)}>↓</button>
                                <button className={`test-card-action test-lock-button ${locked ? "active" : ""}`} type="button" disabled={folder.locked} aria-label={`${test.locked ? "Unlock" : "Lock"} ${test.name}`} title={folder.locked ? "Locked by suite folder" : `${test.locked ? "Unlock" : "Lock"} test`} onClick={() => toggleTestLocked(test.id)}><span className="lock-glyph" aria-hidden="true" /></button>
                                <button className="test-card-action test-delete-button" type="button" disabled={locked || tests.length <= 1} aria-label={`Delete ${test.name}`} title={locked ? "Unlock this test before deleting it" : tests.length <= 1 ? "At least one test is required" : "Delete test"} onClick={() => deleteTest(test.id)}>×</button>
                              </div>
                            </div>
                          );
                        }) : <p className="empty-folder">No tests yet</p>}
                      </div>}
                    </section>
                  );
                })}
              </div>
              {addingFolder && <div className="folder-form"><input aria-label="New test folder name" value={newFolderName} placeholder="e.g. Gravity" onChange={(event) => setNewFolderName(event.target.value)} onKeyDown={(event) => { if (event.key === "Enter") addFolder(); if (event.key === "Escape") setAddingFolder(false); }} /><button className="tool-button tool-button--primary" onClick={addFolder}>Create</button></div>}
              <div className="suite-actions"><button className="tool-button" onClick={() => addTest()}>＋ New test</button><button className="tool-button" onClick={() => setAddingFolder((value) => !value)}>＋ New folder</button></div>
            </div>
          </details>

          <details className="author-panel" open>
            <summary><span className="chevron">▸</span><span>Block Definition</span><button type="button" className="panel-add" aria-label="Add block" onClick={(event) => { event.preventDefault(); setAddingBlock((value) => !value); }}>＋</button></summary>
            <div className="author-panel__body">
              {addingBlock ? (
                <div className="definition-form new-definition">
                  <label className="field"><span>New block name</span><input value={newBlock.name} placeholder="e.g. Ice" onChange={(event) => setNewBlock((value) => ({ ...value, name: event.target.value }))} /></label>
                  <div className="definition-row"><label className="field color-field"><span>Color</span><input type="color" value={newBlock.color} onChange={(event) => setNewBlock((value) => ({ ...value, color: event.target.value }))} /></label><label className="field"><span>Physics role</span><select value={newBlock.roleId} onChange={(event) => setNewBlock((value) => ({ ...value, roleId: event.target.value }))}>{roles.map((role) => <option key={role.id} value={role.id}>{role.name}</option>)}</select></label></div>
                  <button className="tool-button tool-button--primary full" onClick={addBlock}>Create block</button>
                </div>
              ) : groupSelectionMode || activeGroupSelection ? (
                <div className="group-tool-description"><span className="group-tool-icon large" aria-hidden="true"><i /><i /><i /></span><div><strong>Select group</strong><small>Press G once, then click a cube; G again clears the selection. Arrow keys move relative to the camera. Shift+↑ raises and Shift+↓ lowers the selected groups one layer. Delete or Backspace erases them.</small></div></div>
              ) : selectedBlock === DELETE_TOOL_ID ? (
                <div className="eraser-description"><svg className="author-tool-icon author-tool-icon--eraser" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d="m7 21-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 1 1 2.5 0 3.4L13 21" /><path d="M22 21H7" /><path d="m5 11 9 9" /></svg><div><strong>Erase tool</strong><small>Click a visible cube to remove it. Press E to select.</small></div></div>
              ) : selectedDefinition ? (
                <div className="definition-form">
                  <div className="selected-block-title"><span className={`swatch-cube large ${genericBlockIds.has(selectedDefinition.id) ? "generic" : ""}`} data-generic-label={genericBlockIds.has(selectedDefinition.id) ? "N" : undefined} style={{ "--block-color": selectedDefinition.color } as React.CSSProperties} /><div><strong>{selectedDefinition.name}</strong><small>{selectedDefinition.id}</small></div></div>
                  <label className="field"><span>Name</span><input value={selectedDefinition.name} onChange={(event) => { setBlocks((current) => current.map((block) => block.id === selectedBlock ? { ...block, name: event.target.value } : block)); setResults({}); }} /></label>
                  <div className="definition-row"><label className="field color-field"><span>Color</span><input type="color" value={selectedDefinition.color} onChange={(event) => setBlocks((current) => current.map((block) => block.id === selectedBlock ? { ...block, color: event.target.value } : block))} /></label><label className="field"><span>Physics role</span><select value={selectedDefinition.roleId} onChange={(event) => { const roleId = event.target.value; const generic = roles.find((role) => role.id === roleId)?.generic === true; const affected = new Set([selectedDefinition.id]); setBlocks((current) => current.map((block) => block.id === selectedDefinition.id ? { ...block, roleId } : block)); setTests((current) => current.map((test) => isTestLocked(test) ? test : { ...test, start: setGenericModeForBlocks(test.start, affected, generic), intermediate: test.intermediate.map((frame) => setGenericModeForBlocks(frame, affected, generic)), expected: setGenericModeForBlocks(test.expected, affected, generic) })); setSelectedRoleId(roleId); setResults({}); }}>{roles.map((role) => <option key={role.id} value={role.id}>{role.name}</option>)}</select></label></div>
                </div>
              ) : null}
            </div>
          </details>

          <details className="author-panel" open>
            <summary><span className="chevron">▸</span><span>Physics Roles</span><button type="button" className="panel-add" aria-label="Add physics role" onClick={(event) => { event.preventDefault(); setAddingRole((value) => !value); }}>＋</button></summary>
            <div className="author-panel__body">
              {addingRole ? (
                <div className="definition-form new-definition">
                  <label className="field"><span>Role name</span><input value={newRole.name} placeholder="e.g. Ice" onChange={(event) => setNewRole((value) => ({ ...value, name: event.target.value }))} /></label>
                  <label className="field"><span>Description</span><textarea rows={3} value={newRole.description} placeholder="Describe what this role should do." onChange={(event) => setNewRole((value) => ({ ...value, description: event.target.value }))} /></label>
                  <div className="generic-toggle"><input aria-label="Make new role a generic numbered family" id="new-role-generic" type="checkbox" checked={newRole.generic} onChange={(event) => setNewRole((value) => ({ ...value, generic: event.target.checked }))} /><span><b>Generic numbered family</b><small>Blocks using this role receive object IDs 0, 1, 2, and beyond.</small></span></div>
                  <button className="tool-button tool-button--primary full" onClick={addRole}>Create physics role</button>
                </div>
              ) : selectedRole ? (
                <div className="definition-form role-definition">
                  <label className="field"><span>Role</span><select value={selectedRole.id} onChange={(event) => setSelectedRoleId(event.target.value)}>{roles.map((role) => <option key={role.id} value={role.id}>{role.name}</option>)}</select></label>
                  <label className="field"><span>Name</span><input value={selectedRole.name} onChange={(event) => updateSelectedRole({ name: event.target.value })} /></label>
                  <label className="field"><span>Description</span><textarea rows={3} value={selectedRole.description} onChange={(event) => updateSelectedRole({ description: event.target.value })} /></label>
                  <div className="generic-toggle"><input aria-label={`Make ${selectedRole.name} a generic numbered family`} id="selected-role-generic" type="checkbox" checked={selectedRole.generic} onChange={(event) => { const generic = event.target.checked; const affected = new Set(blocks.filter((block) => block.roleId === selectedRole.id).map((block) => block.id)); updateSelectedRole({ generic }); setTests((current) => current.map((test) => isTestLocked(test) ? test : { ...test, start: setGenericModeForBlocks(test.start, affected, generic), intermediate: test.intermediate.map((frame) => setGenericModeForBlocks(frame, affected, generic)), expected: setGenericModeForBlocks(test.expected, affected, generic) })); setResults({}); }} /><span><b>Generic numbered family</b><small>Select its N tool below, type an object ID, and press Enter before painting.</small></span></div>
                  <p className="engine-role-note"><b>Stable engine key preserved.</b> New roles occupy space but remain behaviorally inert until their rule is implemented in the C++ engine.</p>
                </div>
              ) : null}
            </div>
          </details>

          <details className="author-panel" open>
            <summary><span className="chevron">▸</span><span>Test World</span><em>{activeWorld.width} × {activeWorld.height} × ∞</em></summary>
            <div className="author-panel__body">
              <TestWorldEditor key={`${activeTest.id}:${activeWorld.width}:${activeWorld.height}`} locked={activeTestLocked} test={activeTest} onSave={saveWorldDimensions} />
              <p className="axis-note"><b>This size belongs only to {activeTest.name}.</b> Draft values do nothing until Save dimensions is pressed; Cancel restores the saved values. Start and Expected then resize together while other tests keep their own dimensions. Shrinking crops this test&apos;s out-of-bounds voxels. <b>Z is unbounded</b> around floor layer 0.</p>
            </div>
          </details>
        </aside>
      </section>
    </main>
  );
}
