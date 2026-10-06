"use client";

import {
  PointerEvent as ReactPointerEvent,
  useCallback,
  useEffect,
  useRef,
  useState,
} from "react";
import { cameraYawQuarterTurns, stepCameraZoom } from "./cameraNavigation.mjs";
import {
  marqueeRectangle,
  marqueeSamplePoints,
} from "./marqueeSelection.mjs";
import { cellObjectSelectionKey } from "./cellObjects.mjs";
import { resolveEditorPaintTarget } from "./editorPaintTarget.mjs";
import { gateIsRaised, liftIsRaised, normalizeButtonOrientation, normalizeLiftOrientation, normalizeSlopeDirection, puncherIsSprung } from "./visualVariants.mjs";
import {
  orangeWallMechanismDepth,
} from "./orangeWalls.mjs";

type BlockDefinition = {
  id: string;
  name: string;
  color: string;
  roleId: string;
  occupancy: string;
  visual: {
    buttonForm?: "visible" | "hidden";
    kind: "button" | "cube" | "floating-floor" | "gate" | "gem" | "lift" | "orange-wall" | "puncher" | "slope";
    modelUrl?: string;
    orangeForm?: "visible" | "hidden";
  };
};

type Voxel = {
  x: number;
  y: number;
  z: number;
  blockId: string;
  genericId?: number;
  groupId?: number;
  instanceId?: string;
  mechanismDepth?: number;
  orientation?: string;
  stateId?: number;
  variantId?: number;
};
type Frame = { voxels: Voxel[] };
type WorldSettings = { width: number; height: number; floorLayer: 0 };

type MazeBenchPick = {
  bottomY?: number;
  dx?: number;
  dy?: number;
  face?: string;
  highlightShape?: "geometry" | "surface";
  kind?: string;
  paintLayer: number | null;
  paintX: number;
  paintY: number;
  sourceLayer: number;
  sourceX: number;
  sourceY: number;
  selectionKey?: string;
  topY?: number;
};

export type PaintSurface = {
  dx: number;
  dy: number;
  face?: string;
  selectionKey?: string;
};

type PaintPointerInput = {
  altKey: boolean;
  button: number;
  buttons: number;
  clientX: number;
  clientY: number;
  ctrlKey: boolean;
  metaKey: boolean;
  pointerId: number;
  type: string;
};

type PaintStroke = {
  lastPaintedVoxelKey: string;
  layer: number | null;
  pointerId: number;
};

type SelectedVoxelOrigin = { x: number; y: number; z: number; selectionKey?: string };

type MarqueeDrag = {
  currentX: number;
  currentY: number;
  pointerId: number;
  startX: number;
  startY: number;
};

type MazeBenchRenderer = {
  dispose: () => void;
  getRenderStats: () => Record<string, number>;
  getDebugCameraTilt: () => number;
  getDebugCameraYaw: () => number;
  getDebugCameraZoom: () => number;
  invalidateSceneCache: () => void;
  isReady: () => boolean;
  whenLevelStateModelsReady: (playData: MazeBenchPlayData) => Promise<unknown>;
  pickEditorFace: (
    clientX: number,
    clientY: number,
    target: HTMLCanvasElement,
  ) => MazeBenchPick | null;
  setDebugCameraView: (options: {
    animate?: boolean;
    mode: "perspective";
    preserveSceneCache?: boolean;
    skipRender?: boolean;
    tilt?: number;
    yaw?: number;
    zoom?: number;
  }) => void;
  setEditorHoverTarget: (target: MazeBenchPick | null) => void;
};

type MazeBenchApp = {
  applyLevelState: (playData: MazeBenchPlayData, options?: Record<string, unknown>) => void;
  editorCameraElevationOffset?: number;
  editorCameraMaximumLogicalLayer?: number;
  editorCameraSceneKey?: string;
  isEditorRenderApp: boolean;
  playSurroundingRadius: number;
  render: (now?: number) => void;
  setupCanvas: () => void;
  state: { effects: { fuzzyEnabled: boolean; noisePhase: number } };
  threeRenderer?: MazeBenchRenderer;
  threeRendererReady?: Promise<unknown>;
};

type MazeBenchModules = {
  createPlayCore: (options: Record<string, unknown>) => MazeBenchApp;
  registerRenderFunctions: (app: MazeBenchApp) => void;
};

type TerrainLayer = {
  direction?: string;
  editorOnly?: boolean;
  editorOpacity?: number;
  elevation: number;
  genericLabel?: string;
  label: string;
  raised: boolean;
  orangeForm?: "cube" | "face" | "hidden";
  selectionKey: string;
  type: "wall" | "ice_slope" | "orange_wall" | "player_gate" | "player_lift";
  voxelColor: string;
  voxelKey: string;
};

type TerrainCell = {
  label: string;
  layers: TerrainLayer[];
  raised: false;
  type: "empty" | "wall";
  underlay: null;
};

type RenderActor = {
  collectionId: string;
  direction?: string;
  editorOnly?: boolean;
  editorOpacity?: number;
  elevation: number;
  groupId?: string;
  label: string;
  modelUrl?: string;
  orientation?: string;
  removed: false;
  selected: boolean;
  selectionKey: string;
  shape?: "cube" | "slope";
  styleKey?: string;
  type: "clone" | "floating_floor" | "gem" | "orange_button" | "puncher" | "weightless_box";
  sprung?: boolean;
  voxelColor?: string;
  x: number;
  y: number;
};

type MazeBenchPlayData = {
  actors: RenderActor[];
  cameraView: { height: number; width: number };
  disableHorizontalNeighborFetches: true;
  editorRender: true;
  gameId: string;
  height: number;
  hostFullBleedView: false;
  hostRenderPixelScale: number;
  levelId: string;
  levelLabel: string;
  terrain: TerrainCell[][];
  width: number;
  worldColumns: number[];
  worldRows: number[];
};

type RuntimeState = {
  app: MazeBenchApp;
  layerOffset: number;
};

type CameraMotion = {
  frameId: number;
  heldTiltKeys: Set<"w" | "s">;
  lastMs: number;
  pointerTiltDirection: -1 | 0 | 1;
  tiltDirection: -1 | 0 | 1;
  tiltVelocity: number;
  yawAnimation: null | {
    startMs: number;
    startYaw: number;
    targetYaw: number;
  };
};

type CanvasProps = {
  frame: Frame;
  blocks: BlockDefinition[];
  genericBlockIds: ReadonlySet<string>;
  world: WorldSettings;
  layer: number;
  selectedVoxelKeys?: ReadonlySet<string>;
  selectionMode?: boolean;
  selectedBlock?: string;
  selectedBlockCanShare?: boolean;
  eraseMode?: boolean;
  interactive?: boolean;
  paintable?: boolean;
  compact?: boolean;
  cameraLayerRange?: { minimum: number; maximum: number };
  cameraSceneKey?: string;
  // Snapshot jobs remount with a new key when their shared camera changes.
  initialCamera?: { yaw: number; tilt: number };
  onSnapshot?: (dataUrl: string) => void;
  snapshotRequestId?: number | string;
  onPaint?: (
    x: number,
    y: number,
    z: number,
    blockId: string | null,
    surface?: PaintSurface,
  ) => void;
  onPaintGestureEnd?: () => void;
  onPaintGestureStart?: () => void;
  onCameraQuarterTurnChange?: (quarterTurns: number) => void;
  onSelectVoxel?: (x: number, y: number, z: number, additive: boolean, selectionKey?: string) => void;
  onSelectVoxels?: (voxels: SelectedVoxelOrigin[], additive: boolean) => void;
};

declare global {
  interface Window {
    PlayModules?: MazeBenchModules;
    __MAZEBENCH_THREE__?: Record<string, unknown>;
    __MAZEBENCH_VOXEL_RUNTIME__?: Promise<MazeBenchModules>;
  }
}

const RUNTIME_SCRIPTS = [
  "/mazebench-runtime/play-rules.js",
  "/mazebench-runtime/play-core.js",
  "/mazebench-runtime/play-render-effects.js",
  "/mazebench-runtime/play-render-terrain.js",
  "/mazebench-runtime/play-render-actors.js",
  "/mazebench-runtime/play-render-three.js",
  "/mazebench-runtime/play-render-compositor.js",
  "/mazebench-runtime/play-render.js",
];

// Values are copied from MazeBenchEngine/public/author.js so keyboard and CAM
// pad movement have the same feel as the MazeBench editor.
const CAMERA_TILT_MAX_SPEED = Math.PI * 0.72;
const CAMERA_TILT_ACCEL = Math.PI * 3.4;
const CAMERA_TILT_DECEL = Math.PI * 4.2;
const CAMERA_YAW_DURATION_MS = 400;
const EMPTY_VOXEL_KEYS: ReadonlySet<string> = new Set();
const MARQUEE_CLICK_THRESHOLD_PX = 5;

function easeInOutQuad(progress: number) {
  const value = Math.max(0, Math.min(1, progress));
  return value < 0.5
    ? 2 * value * value
    : 1 - Math.pow(-2 * value + 2, 2) / 2;
}

function easeToward(current: number, target: number, maxDelta: number) {
  if (current < target) return Math.min(target, current + maxDelta);
  if (current > target) return Math.max(target, current - maxDelta);
  return current;
}

function isTypingTarget(target: EventTarget | null) {
  if (!(target instanceof Element)) return false;
  const tagName = target.tagName.toLowerCase();
  return (
    tagName === "input" ||
    tagName === "textarea" ||
    tagName === "select" ||
    (target as HTMLElement).isContentEditable ||
    Boolean(target.closest("[contenteditable='true']"))
  );
}

function loadScript(source: string) {
  return new Promise<void>((resolve, reject) => {
    const existing = document.querySelector<HTMLScriptElement>(`script[data-mazebench-runtime="${source}"]`);
    if (existing?.dataset.loaded === "true") {
      resolve();
      return;
    }
    if (existing) {
      existing.addEventListener("load", () => resolve(), { once: true });
      existing.addEventListener("error", () => reject(new Error(`Could not load ${source}`)), { once: true });
      return;
    }

    const script = document.createElement("script");
    script.src = source;
    script.async = false;
    script.dataset.mazebenchRuntime = source;
    script.addEventListener("load", () => {
      script.dataset.loaded = "true";
      resolve();
    }, { once: true });
    script.addEventListener("error", () => reject(new Error(`Could not load ${source}`)), { once: true });
    document.head.appendChild(script);
  });
}

async function loadMazeBenchRuntime() {
  // MazeBench and GLTFLoader must share this exact module namespace. Loading
  // a second bundled Three instance breaks model type identity and emits a
  // runtime warning even when both copies have the same version.
  if (!window.__MAZEBENCH_THREE__) {
    const threeModuleUrl = new URL("vendor/three.module.js", document.baseURI).href;
    window.__MAZEBENCH_THREE__ = await import(/* @vite-ignore */ threeModuleUrl);
  }
  if (typeof window.PlayModules?.createPlayCore === "function" &&
      typeof window.PlayModules?.registerRenderFunctions === "function") {
    return Promise.resolve(window.PlayModules);
  }
  if (!window.__MAZEBENCH_VOXEL_RUNTIME__) {
    window.__MAZEBENCH_VOXEL_RUNTIME__ = RUNTIME_SCRIPTS.reduce(
      (chain, source) => chain.then(() => loadScript(source)),
      Promise.resolve(),
    ).then(() => {
      if (typeof window.PlayModules?.createPlayCore !== "function" ||
          typeof window.PlayModules?.registerRenderFunctions !== "function") {
        throw new Error("MazeBench render modules did not initialize");
      }
      return window.PlayModules;
    });
  }
  return window.__MAZEBENCH_VOXEL_RUNTIME__;
}

function lerpHexColor(from: string, to: string, amount: number) {
  const parse = (value: string) => {
    const hex = value.replace(/^#/, "");
    const expanded = hex.length === 3
      ? hex.split("").map((character) => character + character).join("")
      : hex.padStart(6, "0").slice(0, 6);
    return [0, 2, 4].map((offset) => Number.parseInt(expanded.slice(offset, offset + 2), 16));
  };
  const start = parse(from);
  const end = parse(to);
  const channel = (index: number) => Math.round(
    start[index] + (end[index] - start[index]) * amount,
  ).toString(16).padStart(2, "0");
  return `#${channel(0)}${channel(1)}${channel(2)}`;
}

function frameToPlayData(
  frame: Frame,
  blocks: BlockDefinition[],
  genericBlockIds: ReadonlySet<string>,
  world: WorldSettings,
  compact: boolean,
  selectedVoxelKeys: ReadonlySet<string>,
  cameraLayerRange?: { minimum: number; maximum: number },
) {
  const frameMinLayer = frame.voxels.length
    ? Math.min(0, ...frame.voxels.map((voxel) => voxel.z))
    : 0;
  const minLayer = Math.min(
    frameMinLayer,
    Number.isFinite(cameraLayerRange?.minimum) ? Number(cameraLayerRange?.minimum) : 0,
  );
  // MazeBench's runtime stores elevations as non-negative integers. Keeping a
  // movable origin below the lowest authored cube preserves negative logical Z.
  // Keep exactly one internal layer below the lowest authored cube. Painting
  // its bottom face creates the next negative logical layer; the next render
  // rebases again, so logical Z remains unbounded without lifting small scenes
  // out of MazeBench's normal camera envelope.
  const layerOffset = 1 - minLayer;
  const definitions = new Map(blocks.map((block) => [block.id, block]));
  const actors: RenderActor[] = frame.voxels.flatMap((voxel) => {
    const definition = definitions.get(voxel.blockId);
    if (!definition) return [];
    const rigidFamilyType = definition.roleId === "weightless-pushable"
      ? "weightless_box"
      : definition.roleId === "clone"
        ? "clone"
        : null;
    const isRigidFamilyMember = rigidFamilyType !== null &&
      (definition.visual.kind === "cube" || definition.visual.kind === "slope");
    const isFloatingFloor = definition.visual.kind === "floating-floor";
    if (!isRigidFamilyMember && !isFloatingFloor && definition.visual.kind !== "gem" && definition.visual.kind !== "button" && definition.visual.kind !== "puncher") return [];
    const genericId = Math.max(0, Math.floor(Number(voxel.groupId ?? voxel.genericId) || 0));
    const groupId = rigidFamilyType === "clone" ? `c${genericId}` : `M${genericId}`;
    const selected = selectedVoxelKeys.has(cellObjectSelectionKey(voxel));
    const hiddenButton = definition.visual.kind === "button" &&
      definition.visual.buttonForm === "hidden";
    return [{
      collectionId: `voxel-tests:${cellObjectSelectionKey(voxel)}`,
      ...(isRigidFamilyMember
        ? {
            direction: definition.visual.kind === "slope"
              ? normalizeSlopeDirection(voxel.orientation, voxel.variantId)
              : undefined,
            groupId,
            shape: definition.visual.kind === "slope" ? "slope" as const : "cube" as const,
            styleKey: groupId,
            voxelColor: selected
              ? lerpHexColor(definition.color, "#34e7f0", 0.48)
              : definition.color,
          }
        : {}),
      elevation: voxel.z + layerOffset,
      ...(hiddenButton
        ? { editorOnly: true, editorOpacity: 0.5 }
        : {}),
      label: definition.name,
      ...(isFloatingFloor
        ? {
            voxelColor: selected
              ? lerpHexColor(definition.color, "#34e7f0", 0.48)
              : definition.color,
          }
        : {}),
      ...(definition.visual.kind === "gem"
        ? { modelUrl: definition.visual.modelUrl }
        : definition.visual.kind === "puncher"
          ? {
              direction: normalizeSlopeDirection(voxel.orientation, voxel.variantId),
              sprung: puncherIsSprung(voxel.genericId),
              voxelColor: definition.color,
            }
        : {
            orientation: normalizeButtonOrientation(voxel.orientation, voxel.variantId),
          }),
      removed: false,
      selected,
      selectionKey: cellObjectSelectionKey(voxel),
      type: isRigidFamilyMember
        ? rigidFamilyType
        : isFloatingFloor
          ? "floating_floor"
        : definition.visual.kind === "gem"
          ? "gem"
          : definition.visual.kind === "puncher"
            ? "puncher"
            : "orange_button",
      x: voxel.x,
      y: voxel.y,
    }];
  });
  const terrain = Array.from({ length: world.height }, (_, y) =>
    Array.from({ length: world.width }, (_, x): TerrainCell => {
      const layers = frame.voxels
        .filter((voxel) => voxel.x === x && voxel.y === y)
        .sort((left, right) => left.z - right.z)
        .map((voxel): TerrainLayer | null => {
          const definition = definitions.get(voxel.blockId);
          if (!definition || !["cube", "gate", "lift", "orange-wall", "slope"].includes(definition.visual.kind)) return null;
          if (
            (definition.roleId === "weightless-pushable" || definition.roleId === "clone") &&
            (definition.visual.kind === "cube" || definition.visual.kind === "slope")
          ) return null;
          const selected = selectedVoxelKeys.has(cellObjectSelectionKey(voxel));
          const isLift = definition.visual.kind === "lift";
          const isGate = definition.visual.kind === "gate";
          const hiddenOrangeWall = definition.visual.kind === "orange-wall" &&
            definition.visual.orangeForm === "hidden";
          return {
            ...(definition.visual.kind === "slope"
              ? { direction: normalizeSlopeDirection(voxel.orientation, voxel.variantId) }
              : isLift
                ? { direction: normalizeLiftOrientation(voxel.orientation, voxel.variantId, voxel.genericId) }
              : {}),
            // Test-suite frames author the visible row directly. Remaining
            // rise is metadata, not an editor-space offset: two Orange Cubes
            // at consecutive rows must stay stacked even when their numbers
            // differ or are nonzero.
            elevation: voxel.z + layerOffset,
            ...(hiddenOrangeWall
              ? { editorOnly: true, editorOpacity: 0.5 }
              : {}),
            genericLabel: definition.visual.kind === "orange-wall"
              ? String(orangeWallMechanismDepth(voxel))
              : genericBlockIds.has(definition.id) && !isLift
                ? String(Math.max(0, Math.floor(Number(voxel.genericId) || 0)))
                : undefined,
            label: definition.name,
            raised: isLift
              ? liftIsRaised(voxel.genericId)
              : isGate
                ? gateIsRaised(voxel.genericId)
              : true,
            ...(definition.visual.kind === "orange-wall"
              ? { orangeForm: hiddenOrangeWall ? "hidden" as const : "cube" as const }
              : {}),
            selectionKey: cellObjectSelectionKey(voxel),
            type: definition.visual.kind === "slope"
              ? "ice_slope"
              : isLift
                ? "player_lift"
              : isGate
                ? "player_gate"
              : definition.visual.kind === "orange-wall"
                ? "orange_wall"
                : "wall",
            voxelColor: selected
              ? lerpHexColor(definition.color, "#34e7f0", 0.48)
              : definition.color,
            voxelKey: definition.id,
          };
        })
        .filter((layer): layer is TerrainLayer => Boolean(layer));

      return {
        label: layers.at(-1)?.label ?? "Empty",
        layers,
        raised: false,
        type: layers.length ? "wall" : "empty",
        underlay: null,
      };
    }),
  );

  const playData: MazeBenchPlayData = {
    actors,
    cameraView: { width: world.width, height: world.height },
    disableHorizontalNeighborFetches: true,
    editorRender: true,
    gameId: "voxel-tests",
    height: world.height,
    hostFullBleedView: false,
    hostRenderPixelScale: compact
      ? 1
      : Math.min(2, typeof window === "undefined" ? 1 : window.devicePixelRatio || 1),
    levelId: "active-frame",
    levelLabel: "Voxel test frame",
    terrain,
    width: world.width,
    worldColumns: [0],
    worldRows: [0],
  };

  return { layerOffset, playData };
}

function publishRendererState(app: MazeBenchApp, canvas: HTMLCanvasElement) {
  window.requestAnimationFrame(() => {
    canvas.dataset.rendererReady = String(app.threeRenderer?.isReady() === true);
    canvas.dataset.rendererStats = JSON.stringify(app.threeRenderer?.getRenderStats?.() ?? {});
  });
}

export default function MazeBenchCanvas({
  frame,
  blocks,
  genericBlockIds,
  world,
  selectedVoxelKeys = EMPTY_VOXEL_KEYS,
  selectionMode = false,
  selectedBlock,
  selectedBlockCanShare = false,
  eraseMode = false,
  interactive = false,
  paintable = true,
  compact = false,
  cameraLayerRange,
  cameraSceneKey,
  initialCamera,
  onSnapshot,
  snapshotRequestId,
  onPaint,
  onPaintGestureEnd,
  onPaintGestureStart,
  onCameraQuarterTurnChange,
  onSelectVoxel,
  onSelectVoxels,
}: CanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const wrapperRef = useRef<HTMLDivElement>(null);
  const runtimeRef = useRef<RuntimeState | null>(null);
  const onSnapshotRef = useRef(onSnapshot);
  const snapshotCapturedRef = useRef(false);
  const currentDataRef = useRef(frameToPlayData(
    frame,
    blocks,
    genericBlockIds,
    world,
    compact,
    selectedVoxelKeys,
    cameraLayerRange,
  ));
  const orbitRef = useRef<{ x: number; y: number; yaw: number; tilt: number } | null>(null);
  const cameraRef = useRef({ yaw: initialCamera?.yaw ?? 0, tilt: initialCamera?.tilt ?? 0.22, zoom: compact ? 0.9 : 1 });
  const cameraMotionRef = useRef<CameraMotion>({
    frameId: 0,
    heldTiltKeys: new Set(),
    lastMs: 0,
    pointerTiltDirection: 0,
    tiltDirection: 0,
    tiltVelocity: 0,
    yawAnimation: null,
  });
  const cameraFrameCallbackRef = useRef<(now: number) => void>(() => {});
  const lastPaintRef = useRef("");
  const paintStrokeRef = useRef<PaintStroke | null>(null);
  const pendingPaintSampleRef = useRef<PaintPointerInput | null>(null);
  const paintFrameIdRef = useRef(0);
  const publishedCameraQuarterTurnRef = useRef<number | null>(null);
  const marqueeDragRef = useRef<MarqueeDrag | null>(null);
  const [marqueeRect, setMarqueeRect] = useState<ReturnType<typeof marqueeRectangle> | null>(null);
  const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");

  useEffect(() => {
    onSnapshotRef.current = onSnapshot;
  }, [onSnapshot]);

  const captureSnapshot = useCallback((app: MazeBenchApp, canvas: HTMLCanvasElement) => {
    if (!onSnapshotRef.current || snapshotCapturedRef.current) return;
    window.requestAnimationFrame(() => {
      if (runtimeRef.current?.app !== app || snapshotCapturedRef.current) return;
      try {
        // Capture while the WebGL drawing buffer is current, then immediately
        // let the parent unmount this renderer before it creates the next one.
        // A small 2D copy keeps timeline memory bounded and avoids retaining a
        // WebGL context for every generated tick.
        app.render();
        const thumbnail = document.createElement("canvas");
        thumbnail.width = 256;
        thumbnail.height = 256;
        const context = thumbnail.getContext("2d");
        if (!context) throw new Error("Timeline snapshot canvas is unavailable");
        context.drawImage(canvas, 0, 0, thumbnail.width, thumbnail.height);
        snapshotCapturedRef.current = true;
        onSnapshotRef.current?.(thumbnail.toDataURL("image/png"));
      } catch (error) {
        console.error(error);
        snapshotCapturedRef.current = true;
        onSnapshotRef.current?.("");
      }
    });
  }, []);

  const publishCameraQuarterTurn = useCallback((yaw: number) => {
    const quarterTurns = cameraYawQuarterTurns(yaw);
    if (publishedCameraQuarterTurnRef.current === quarterTurns) return;
    publishedCameraQuarterTurnRef.current = quarterTurns;
    onCameraQuarterTurnChange?.(quarterTurns);
  }, [onCameraQuarterTurnChange]);

  const setCamera = useCallback((
    next?: Partial<typeof cameraRef.current>,
    render = true,
  ) => {
    if (next) cameraRef.current = { ...cameraRef.current, ...next };
    publishCameraQuarterTurn(cameraRef.current.yaw);
    const renderer = runtimeRef.current?.app.threeRenderer;
    if (!renderer) return;
    renderer.setDebugCameraView({
      ...cameraRef.current,
      mode: "perspective",
      preserveSceneCache: true,
      skipRender: !render,
    });
  }, [publishCameraQuarterTurn]);

  const zoomCamera = useCallback((direction: -1 | 1) => {
    const renderer = runtimeRef.current?.app.threeRenderer;
    if (renderer) cameraRef.current.zoom = renderer.getDebugCameraZoom();
    setCamera({ zoom: stepCameraZoom(cameraRef.current.zoom, direction) });
  }, [setCamera]);

  const runCameraFrame = useCallback((now: number) => {
    const motion = cameraMotionRef.current;
    motion.frameId = 0;
    const app = runtimeRef.current?.app;
    const renderer = app?.threeRenderer;
    if (!app || !renderer) {
      motion.yawAnimation = null;
      motion.tiltVelocity = 0;
      motion.lastMs = 0;
      return;
    }

    const deltaSeconds = motion.lastMs
      ? Math.min(0.05, Math.max(0.001, (now - motion.lastMs) / 1000))
      : 1 / 60;
    motion.lastMs = now;
    let continueLoop = false;

    if (motion.yawAnimation) {
      const progress = Math.min(1, (now - motion.yawAnimation.startMs) / CAMERA_YAW_DURATION_MS);
      cameraRef.current.yaw =
        motion.yawAnimation.startYaw +
        (motion.yawAnimation.targetYaw - motion.yawAnimation.startYaw) * easeInOutQuad(progress);
      if (progress >= 1) {
        cameraRef.current.yaw = motion.yawAnimation.targetYaw;
        motion.yawAnimation = null;
      } else {
        continueLoop = true;
      }
    }

    publishCameraQuarterTurn(cameraRef.current.yaw);

    if (motion.tiltDirection || motion.tiltVelocity) {
      const targetVelocity = motion.tiltDirection * CAMERA_TILT_MAX_SPEED;
      const rate = motion.tiltDirection ? CAMERA_TILT_ACCEL : CAMERA_TILT_DECEL;
      motion.tiltVelocity = easeToward(
        motion.tiltVelocity,
        targetVelocity,
        rate * deltaSeconds,
      );
      if (!motion.tiltDirection && Math.abs(motion.tiltVelocity) < 0.002) {
        motion.tiltVelocity = 0;
      }
      const previousTilt = cameraRef.current.tilt;
      cameraRef.current.tilt = Math.max(
        0,
        Math.min(Math.PI, previousTilt + motion.tiltVelocity * deltaSeconds),
      );
      if (
        cameraRef.current.tilt === previousTilt &&
        motion.tiltVelocity !== 0 &&
        !motion.tiltDirection
      ) {
        motion.tiltVelocity = 0;
      }
      if (motion.tiltDirection || motion.tiltVelocity) continueLoop = true;
    }

    renderer.setDebugCameraView({
      ...cameraRef.current,
      mode: "perspective",
      preserveSceneCache: true,
      skipRender: true,
    });
    app.render(now);

    if (continueLoop) {
      motion.frameId = window.requestAnimationFrame((frameNow) => {
        cameraFrameCallbackRef.current(frameNow);
      });
    } else {
      motion.lastMs = 0;
    }
  }, [publishCameraQuarterTurn]);

  useEffect(() => {
    cameraFrameCallbackRef.current = runCameraFrame;
  }, [runCameraFrame]);

  const scheduleCameraFrame = useCallback(() => {
    const motion = cameraMotionRef.current;
    if (!motion.frameId) motion.frameId = window.requestAnimationFrame(runCameraFrame);
  }, [runCameraFrame]);

  const rotateCamera = useCallback((direction: -1 | 1) => {
    const renderer = runtimeRef.current?.app.threeRenderer;
    if (!renderer) return;
    const motion = cameraMotionRef.current;
    if (!motion.yawAnimation && !motion.tiltVelocity && !motion.tiltDirection) {
      cameraRef.current.yaw = renderer.getDebugCameraYaw();
      cameraRef.current.tilt = renderer.getDebugCameraTilt();
    }
    const fromYaw = motion.yawAnimation?.targetYaw ?? cameraRef.current.yaw;
    motion.yawAnimation = {
      startMs: performance.now(),
      startYaw: cameraRef.current.yaw,
      targetYaw: fromYaw + direction * (Math.PI / 2),
    };
    scheduleCameraFrame();
  }, [scheduleCameraFrame]);

  const pointCameraNorth = useCallback(() => {
    const renderer = runtimeRef.current?.app.threeRenderer;
    if (!renderer) return;
    const motion = cameraMotionRef.current;
    const currentYaw = renderer.getDebugCameraYaw();
    cameraRef.current.yaw = currentYaw;
    cameraRef.current.tilt = renderer.getDebugCameraTilt();
    motion.yawAnimation = {
      startMs: performance.now(),
      startYaw: currentYaw,
      targetYaw: Math.round(currentYaw / (Math.PI * 2)) * Math.PI * 2,
    };
    scheduleCameraFrame();
  }, [scheduleCameraFrame]);

  const recomputeTiltDirection = useCallback(() => {
    const motion = cameraMotionRef.current;
    let direction = motion.pointerTiltDirection;
    if (!direction) {
      if (motion.heldTiltKeys.has("s")) direction = 1;
      if (motion.heldTiltKeys.has("w")) direction = -1;
    }
    if (
      direction &&
      !motion.yawAnimation &&
      !motion.tiltVelocity &&
      !motion.tiltDirection
    ) {
      const renderer = runtimeRef.current?.app.threeRenderer;
      if (renderer) {
        cameraRef.current.yaw = renderer.getDebugCameraYaw();
        cameraRef.current.tilt = renderer.getDebugCameraTilt();
      }
    }
    motion.tiltDirection = direction as -1 | 0 | 1;
    if (direction || motion.tiltVelocity) scheduleCameraFrame();
  }, [scheduleCameraFrame]);

  useEffect(() => {
    let cancelled = false;
    const canvas = canvasRef.current;
    const wrapper = wrapperRef.current;
    if (!canvas || !wrapper) return;

    loadMazeBenchRuntime()
      .then((modules) => {
        if (cancelled) return;
        const { layerOffset, playData } = currentDataRef.current;
        const app = modules.createPlayCore({
          playData,
          canvas,
          playShell: null,
          playHeader: null,
          playStage: wrapper,
          mazeFrame: wrapper,
          fuzzyToggle: null,
          edgeToggle: null,
          cameraModeToggle: null,
          resetProgressButton: null,
          enableCameraControls: false,
        });
        modules.registerRenderFunctions(app);
        app.isEditorRenderApp = true;
        app.editorCameraElevationOffset = layerOffset;
        app.editorCameraMaximumLogicalLayer = cameraLayerRange?.maximum;
        app.editorCameraSceneKey = cameraSceneKey;
        app.playSurroundingRadius = 0;
        app.state.effects.fuzzyEnabled = false;
        app.state.effects.noisePhase = 0;
        runtimeRef.current = { app, layerOffset };
        app.applyLevelState(playData, {
          deferRender: true,
          immediateCamera: true,
          resetHistory: true,
          resetLevelEntry: true,
        });
        app.render();

        return Promise.resolve(app.threeRendererReady).then(async () => {
          if (cancelled || runtimeRef.current?.app !== app) return;
          await app.threeRenderer?.whenLevelStateModelsReady(playData);
          if (cancelled || runtimeRef.current?.app !== app) return;
          setCamera(undefined, false);
          app.threeRenderer?.invalidateSceneCache();
          app.render();
          publishRendererState(app, canvas);
          setStatus("ready");
          captureSnapshot(app, canvas);
        });
      })
      .catch((error: unknown) => {
        console.error(error);
        if (!cancelled) setStatus("error");
      });

    return () => {
      cancelled = true;
      const runtime = runtimeRef.current;
      runtimeRef.current = null;
      runtime?.app.threeRenderer?.dispose();
    };
  }, [cameraLayerRange?.maximum, cameraSceneKey, captureSnapshot, setCamera]);

  useEffect(() => {
    const next = frameToPlayData(
      frame,
      blocks,
      genericBlockIds,
      world,
      compact,
      selectedVoxelKeys,
      cameraLayerRange,
    );
    currentDataRef.current = next;
    if (onSnapshotRef.current) snapshotCapturedRef.current = false;
    const runtime = runtimeRef.current;
    if (!runtime) return;
    runtime.layerOffset = next.layerOffset;
    runtime.app.editorCameraElevationOffset = next.layerOffset;
    runtime.app.editorCameraMaximumLogicalLayer = cameraLayerRange?.maximum;
    runtime.app.editorCameraSceneKey = cameraSceneKey;
    runtime.app.applyLevelState(next.playData, {
      deferRender: true,
      immediateCamera: true,
      resetHistory: true,
      resetLevelEntry: true,
    });
    const app = runtime.app;
    void Promise.resolve(app.threeRenderer?.whenLevelStateModelsReady(next.playData)).then(() => {
      if (runtimeRef.current?.app !== app) return;
      setCamera(undefined, false);
      app.threeRenderer?.invalidateSceneCache();
      app.render();
      if (canvasRef.current) publishRendererState(app, canvasRef.current);
      if (canvasRef.current) captureSnapshot(app, canvasRef.current);
    });
  }, [blocks, cameraLayerRange, cameraSceneKey, captureSnapshot, compact, frame, genericBlockIds, selectedVoxelKeys, setCamera, snapshotRequestId, world]);

  useEffect(() => {
    const wrapper = wrapperRef.current;
    if (!wrapper) return;
    const observer = new ResizeObserver(() => {
      const app = runtimeRef.current?.app;
      if (!app) return;
      app.setupCanvas();
      app.threeRenderer?.invalidateSceneCache();
      setCamera(undefined, false);
      app.render();
      if (canvasRef.current) publishRendererState(app, canvasRef.current);
    });
    observer.observe(wrapper);
    return () => observer.disconnect();
  }, [setCamera]);

  useEffect(() => {
    if (!interactive) return;
    const motion = cameraMotionRef.current;
    const onKeyDown = (event: KeyboardEvent) => {
      if (
        event.defaultPrevented ||
        event.altKey ||
        event.ctrlKey ||
        event.metaKey ||
        isTypingTarget(event.target)
      ) {
        return;
      }
      const key = event.key.toLowerCase();
      if (key === "w" || key === "s") {
        event.preventDefault();
        motion.heldTiltKeys.add(key);
        recomputeTiltDirection();
      } else if ((key === "a" || key === "d") && !event.repeat) {
        event.preventDefault();
        rotateCamera(key === "a" ? -1 : 1);
      } else if (key === "n" && !event.repeat) {
        event.preventDefault();
        pointCameraNorth();
      } else if ((key === "-" || key === "_" || key === "=" || key === "+") && !event.repeat) {
        event.preventDefault();
        zoomCamera(key === "=" || key === "+" ? 1 : -1);
      }
    };
    const onKeyUp = (event: KeyboardEvent) => {
      const key = event.key.toLowerCase();
      if (key !== "w" && key !== "s") return;
      motion.heldTiltKeys.delete(key);
      recomputeTiltDirection();
    };
    const onBlur = () => {
      motion.heldTiltKeys.clear();
      motion.pointerTiltDirection = 0;
      recomputeTiltDirection();
    };
    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);
    window.addEventListener("blur", onBlur);
    return () => {
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
      window.removeEventListener("blur", onBlur);
      if (motion.frameId) window.cancelAnimationFrame(motion.frameId);
      motion.frameId = 0;
      motion.heldTiltKeys.clear();
      motion.pointerTiltDirection = 0;
      motion.tiltDirection = 0;
      motion.tiltVelocity = 0;
      motion.yawAnimation = null;
      if (paintFrameIdRef.current) window.cancelAnimationFrame(paintFrameIdRef.current);
      paintFrameIdRef.current = 0;
      pendingPaintSampleRef.current = null;
      paintStrokeRef.current = null;
    };
  }, [interactive, pointCameraNorth, recomputeTiltDirection, rotateCamera, zoomCamera]);

  useEffect(() => {
    if (paintable) return;
    if (paintFrameIdRef.current) window.cancelAnimationFrame(paintFrameIdRef.current);
    paintFrameIdRef.current = 0;
    pendingPaintSampleRef.current = null;
    paintStrokeRef.current = null;
    onPaintGestureEnd?.();
  }, [onPaintGestureEnd, paintable]);

  const pick = (clientX: number, clientY: number) => {
    const canvas = canvasRef.current;
    const runtime = runtimeRef.current;
    if (!canvas || !runtime?.app.threeRenderer?.isReady()) return null;
    return runtime.app.threeRenderer.pickEditorFace(clientX, clientY, canvas);
  };

  const paintFromPointer = (event: PaintPointerInput, initialSample = false) => {
    const runtime = runtimeRef.current;
    if (!interactive || !paintable || !onPaint || !runtime || orbitRef.current) return;
    const target = pick(event.clientX, event.clientY);
    if (!target || target.kind === "levelSwitch") return;
    const erase = eraseMode || event.button === 2 || (event.buttons & 2) === 2 || event.altKey;
    const replace = event.metaKey || event.ctrlKey;
    const paintTarget = resolveEditorPaintTarget(target, {
      erase,
      replace,
      selectedCanShare: selectedBlockCanShare,
    });
    const { x, y, layer: runtimeLayer } = paintTarget;
    if (runtimeLayer === null || x < 0 || y < 0 || x >= world.width || y >= world.height) return;
    const isEmptyGroundPick =
      target.paintLayer === 0 &&
      target.sourceLayer === 0 &&
      target.topY === 0 &&
      Number(target.bottomY) < 0;
    const z = isEmptyGroundPick ? 0 : runtimeLayer - runtime.layerOffset;
    const sourceZ = isEmptyGroundPick ? 0 : target.sourceLayer - runtime.layerOffset;
    const sourceVoxelKey = `${target.sourceX},${target.sourceY},${sourceZ}`;
    let stroke = paintStrokeRef.current;

    if (initialSample || !stroke || stroke.pointerId !== event.pointerId) {
      stroke = { lastPaintedVoxelKey: "", layer: z, pointerId: event.pointerId };
      paintStrokeRef.current = stroke;
    } else {
      // MazeBench locks a drag stroke to the first logical layer. This avoids
      // the just-painted cube becoming a launch face for an accidental tower.
      if (stroke.layer !== z) return;
      if (stroke.lastPaintedVoxelKey) {
        if (sourceVoxelKey === stroke.lastPaintedVoxelKey) return;
        stroke = { ...stroke, lastPaintedVoxelKey: "" };
        paintStrokeRef.current = stroke;
      }
    }

    const signature = `${x},${y},${z},${erase ? "erase" : selectedBlock}`;
    if (signature === lastPaintRef.current && event.type === "pointermove") return;
    lastPaintRef.current = signature;
    onPaint(x, y, z, erase ? null : selectedBlock ?? null, {
      dx: Number(target.dx) || 0,
      dy: Number(target.dy) || 0,
      face: target.face,
      selectionKey: target.selectionKey,
    });
    if (!erase) {
      paintStrokeRef.current = {
        ...stroke,
        lastPaintedVoxelKey: `${x},${y},${z}`,
      };
    }
  };

  const pointerSample = (event: ReactPointerEvent<HTMLCanvasElement>): PaintPointerInput => ({
    altKey: event.altKey,
    button: event.button,
    buttons: event.buttons,
    clientX: event.clientX,
    clientY: event.clientY,
    ctrlKey: event.ctrlKey,
    metaKey: event.metaKey,
    pointerId: event.pointerId,
    type: event.type,
  });

  const flushPendingPaint = () => {
    if (paintFrameIdRef.current) {
      window.cancelAnimationFrame(paintFrameIdRef.current);
      paintFrameIdRef.current = 0;
    }
    const sample = pendingPaintSampleRef.current;
    pendingPaintSampleRef.current = null;
    if (sample) paintFromPointer(sample);
  };

  const schedulePaintSample = (event: ReactPointerEvent<HTMLCanvasElement>) => {
    pendingPaintSampleRef.current = pointerSample(event);
    if (paintFrameIdRef.current) return;
    paintFrameIdRef.current = window.requestAnimationFrame(() => {
      paintFrameIdRef.current = 0;
      const sample = pendingPaintSampleRef.current;
      pendingPaintSampleRef.current = null;
      if (sample) paintFromPointer(sample);
    });
  };

  const updateMarquee = (drag: MarqueeDrag) => {
    marqueeDragRef.current = drag;
    const wrapperBounds = wrapperRef.current?.getBoundingClientRect();
    if (!wrapperBounds) return;
    const rectangle = marqueeRectangle(drag.startX, drag.startY, drag.currentX, drag.currentY);
    setMarqueeRect({
      ...rectangle,
      bottom: rectangle.bottom - wrapperBounds.top,
      left: rectangle.left - wrapperBounds.left,
      right: rectangle.right - wrapperBounds.left,
      top: rectangle.top - wrapperBounds.top,
    });
  };

  const finishMarquee = (event: ReactPointerEvent<HTMLCanvasElement>) => {
    const drag = marqueeDragRef.current;
    if (!drag || drag.pointerId !== event.pointerId) return false;

    const rectangle = marqueeRectangle(drag.startX, drag.startY, event.clientX, event.clientY);
    marqueeDragRef.current = null;
    setMarqueeRect(null);
    runtimeRef.current?.app.threeRenderer?.setEditorHoverTarget(null);

    if (
      rectangle.width < MARQUEE_CLICK_THRESHOLD_PX &&
      rectangle.height < MARQUEE_CLICK_THRESHOLD_PX
    ) {
      const target = pick(event.clientX, event.clientY);
      const runtime = runtimeRef.current;
      if (target && target.kind !== "levelSwitch" && runtime) {
        onSelectVoxel?.(
          target.sourceX,
          target.sourceY,
          target.sourceLayer - runtime.layerOffset,
          true,
          target.selectionKey,
        );
      }
      return true;
    }

    const canvasBounds = event.currentTarget.getBoundingClientRect();
    const clipped = marqueeRectangle(
      Math.max(canvasBounds.left, rectangle.left),
      Math.max(canvasBounds.top, rectangle.top),
      Math.min(canvasBounds.right - 0.5, rectangle.right),
      Math.min(canvasBounds.bottom - 0.5, rectangle.bottom),
    );
    const runtime = runtimeRef.current;
    if (clipped.width <= 0 || clipped.height <= 0 || !runtime) return true;

    const origins = new Map<string, SelectedVoxelOrigin>();
    for (const point of marqueeSamplePoints(clipped)) {
      const target = pick(point.x, point.y);
      if (!target || target.kind === "levelSwitch") continue;
      const origin = {
        x: target.sourceX,
        y: target.sourceY,
        z: target.sourceLayer - runtime.layerOffset,
        selectionKey: target.selectionKey,
      };
      origins.set(origin.selectionKey ?? `${origin.x},${origin.y},${origin.z}`, origin);
    }
    onSelectVoxels?.([...origins.values()], true);
    return true;
  };

  return (
    <div ref={wrapperRef} className={`voxel-canvas-wrap mazebench-runtime-canvas ${compact ? "compact" : ""} ${interactive && !paintable ? "read-only" : ""} ${selectionMode ? "selection-mode" : ""}`}>
      <canvas
        ref={canvasRef}
        aria-label={interactive ? paintable ? "Interactive MazeBench perspective polycube editor" : "Read-only MazeBench perspective polycube editor" : "MazeBench perspective polycube preview"}
        onContextMenu={(event) => event.preventDefault()}
        onPointerDown={(event) => {
          if (!interactive) return;
          event.currentTarget.setPointerCapture(event.pointerId);
          if (selectionMode && event.button === 0) {
            if (event.shiftKey) {
              updateMarquee({
                currentX: event.clientX,
                currentY: event.clientY,
                pointerId: event.pointerId,
                startX: event.clientX,
                startY: event.clientY,
              });
              return;
            }
            const target = pick(event.clientX, event.clientY);
            const runtime = runtimeRef.current;
            if (!target || target.kind === "levelSwitch" || !runtime) return;
            onSelectVoxel?.(
              target.sourceX,
              target.sourceY,
              target.sourceLayer - runtime.layerOffset,
              false,
              target.selectionKey,
            );
            return;
          }
          if (event.shiftKey || event.button === 1) {
            orbitRef.current = {
              x: event.clientX,
              y: event.clientY,
              yaw: cameraRef.current.yaw,
              tilt: cameraRef.current.tilt,
            };
            return;
          }
          if (!paintable) return;
          onPaintGestureStart?.();
          lastPaintRef.current = "";
          paintFromPointer(pointerSample(event), true);
        }}
        onPointerMove={(event) => {
          const marquee = marqueeDragRef.current;
          if (marquee && marquee.pointerId === event.pointerId) {
            updateMarquee({ ...marquee, currentX: event.clientX, currentY: event.clientY });
            return;
          }
          const orbit = orbitRef.current;
          if (orbit) {
            setCamera({
              yaw: orbit.yaw - (event.clientX - orbit.x) * 0.009,
              tilt: Math.max(0, Math.min(Math.PI, orbit.tilt + (event.clientY - orbit.y) * 0.007)),
            });
            return;
          }
          const target = pick(event.clientX, event.clientY);
          runtimeRef.current?.app.threeRenderer?.setEditorHoverTarget(target);
          if (!selectionMode && (event.buttons & 1) === 1) schedulePaintSample(event);
        }}
        onPointerLeave={() => runtimeRef.current?.app.threeRenderer?.setEditorHoverTarget(null)}
        onPointerUp={(event) => {
          finishMarquee(event);
          flushPendingPaint();
          orbitRef.current = null;
          lastPaintRef.current = "";
          paintStrokeRef.current = null;
          onPaintGestureEnd?.();
        }}
        onPointerCancel={() => {
          marqueeDragRef.current = null;
          setMarqueeRect(null);
          flushPendingPaint();
          orbitRef.current = null;
          lastPaintRef.current = "";
          paintStrokeRef.current = null;
          onPaintGestureEnd?.();
        }}
      />
      {marqueeRect && (
        <div
          aria-hidden="true"
          className="group-selection-marquee"
          style={{
            height: marqueeRect.height,
            left: marqueeRect.left,
            top: marqueeRect.top,
            width: marqueeRect.width,
          }}
        />
      )}
      {status !== "ready" && (
        <div className={`runtime-status ${status}`} role="status">
          {status === "error" ? "MazeBench renderer failed to load" : "Loading MazeBench renderer…"}
        </div>
      )}
      {interactive && (
        <div className="control-pad camera-pad" aria-label="Camera controls" title="− zooms out · + zooms in · E selects the eraser">
          <button className="control-button dpad-button" type="button" data-camera="up" aria-label="Camera up" onPointerDown={() => { cameraMotionRef.current.pointerTiltDirection = -1; recomputeTiltDirection(); }} onPointerUp={() => { cameraMotionRef.current.pointerTiltDirection = 0; recomputeTiltDirection(); }} onPointerLeave={() => { cameraMotionRef.current.pointerTiltDirection = 0; recomputeTiltDirection(); }} />
          <button className="control-button dpad-button" type="button" data-camera="left" aria-label="Rotate camera left" onClick={() => rotateCamera(-1)} />
          <button className="dpad-center compass-reset" type="button" aria-label="Point camera north" title="Point north · N" onClick={pointCameraNorth}>N</button>
          <button className="control-button dpad-button" type="button" data-camera="right" aria-label="Rotate camera right" onClick={() => rotateCamera(1)} />
          <button className="control-button dpad-button" type="button" data-camera="down" aria-label="Camera down" onPointerDown={() => { cameraMotionRef.current.pointerTiltDirection = 1; recomputeTiltDirection(); }} onPointerUp={() => { cameraMotionRef.current.pointerTiltDirection = 0; recomputeTiltDirection(); }} onPointerLeave={() => { cameraMotionRef.current.pointerTiltDirection = 0; recomputeTiltDirection(); }} />
        </div>
      )}
    </div>
  );
}
