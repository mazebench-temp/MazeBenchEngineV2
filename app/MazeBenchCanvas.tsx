"use client";

import {
  PointerEvent as ReactPointerEvent,
  useCallback,
  useEffect,
  useRef,
  useState,
} from "react";
import * as THREE from "three";

type BlockDefinition = {
  id: string;
  name: string;
  color: string;
  behavior: string;
};

type Voxel = { x: number; y: number; z: number; blockId: string };
type Frame = { voxels: Voxel[] };
type WorldSettings = { width: number; height: number; floorLayer: 0 };

type MazeBenchPick = {
  bottomY?: number;
  face?: string;
  kind?: string;
  paintLayer: number | null;
  paintX: number;
  paintY: number;
  sourceLayer: number;
  sourceX: number;
  sourceY: number;
  topY?: number;
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

type MazeBenchRenderer = {
  dispose: () => void;
  getRenderStats: () => Record<string, number>;
  getDebugCameraTilt: () => number;
  getDebugCameraYaw: () => number;
  getDebugCameraZoom: () => number;
  invalidateSceneCache: () => void;
  isReady: () => boolean;
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
  elevation: number;
  label: string;
  raised: true;
  type: "wall";
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

type MazeBenchPlayData = {
  actors: never[];
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
  world: WorldSettings;
  layer: number;
  selectedBlock?: string;
  eraseMode?: boolean;
  interactive?: boolean;
  compact?: boolean;
  onPaint?: (x: number, y: number, z: number, blockId: string | null) => void;
  onPaintGestureEnd?: () => void;
  onPaintGestureStart?: () => void;
};

declare global {
  interface Window {
    PlayModules?: MazeBenchModules;
    __MAZEBENCH_THREE__?: typeof THREE;
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

function loadMazeBenchRuntime() {
  // The in-app preview blocks dynamically imported public .js assets. Hand
  // the renderer the same 0.184.0 Three namespace through the app bundle;
  // all scene construction remains MazeBench's own play-render-three.js.
  window.__MAZEBENCH_THREE__ = THREE;
  if (window.PlayModules?.createPlayCore && window.PlayModules?.registerRenderFunctions) {
    return Promise.resolve(window.PlayModules);
  }
  if (!window.__MAZEBENCH_VOXEL_RUNTIME__) {
    window.__MAZEBENCH_VOXEL_RUNTIME__ = RUNTIME_SCRIPTS.reduce(
      (chain, source) => chain.then(() => loadScript(source)),
      Promise.resolve(),
    ).then(() => {
      if (!window.PlayModules?.createPlayCore || !window.PlayModules?.registerRenderFunctions) {
        throw new Error("MazeBench render modules did not initialize");
      }
      return window.PlayModules;
    });
  }
  return window.__MAZEBENCH_VOXEL_RUNTIME__;
}

function frameToPlayData(
  frame: Frame,
  blocks: BlockDefinition[],
  world: WorldSettings,
  compact: boolean,
) {
  const minLayer = frame.voxels.length
    ? Math.min(0, ...frame.voxels.map((voxel) => voxel.z))
    : 0;
  // MazeBench's runtime stores elevations as non-negative integers. Keeping a
  // movable origin below the lowest authored cube preserves negative logical Z.
  // Keep exactly one internal layer below the lowest authored cube. Painting
  // its bottom face creates the next negative logical layer; the next render
  // rebases again, so logical Z remains unbounded without lifting small scenes
  // out of MazeBench's normal camera envelope.
  const layerOffset = 1 - minLayer;
  const definitions = new Map(blocks.map((block) => [block.id, block]));
  const terrain = Array.from({ length: world.height }, (_, y) =>
    Array.from({ length: world.width }, (_, x): TerrainCell => {
      const layers = frame.voxels
        .filter((voxel) => voxel.x === x && voxel.y === y)
        .sort((left, right) => left.z - right.z)
        .map((voxel): TerrainLayer | null => {
          const definition = definitions.get(voxel.blockId);
          if (!definition) return null;
          return {
            elevation: voxel.z + layerOffset,
            label: definition.name,
            raised: true,
            type: "wall",
            voxelColor: definition.color,
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
    actors: [],
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
  world,
  selectedBlock,
  eraseMode = false,
  interactive = false,
  compact = false,
  onPaint,
  onPaintGestureEnd,
  onPaintGestureStart,
}: CanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const wrapperRef = useRef<HTMLDivElement>(null);
  const runtimeRef = useRef<RuntimeState | null>(null);
  const currentDataRef = useRef(frameToPlayData(frame, blocks, world, compact));
  const orbitRef = useRef<{ x: number; y: number; yaw: number; tilt: number } | null>(null);
  const cameraRef = useRef({ yaw: 0, tilt: 0.22, zoom: compact ? 0.9 : 1 });
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
  const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");

  const setCamera = useCallback((next?: Partial<typeof cameraRef.current>) => {
    if (next) cameraRef.current = { ...cameraRef.current, ...next };
    const renderer = runtimeRef.current?.app.threeRenderer;
    if (!renderer) return;
    renderer.setDebugCameraView({
      ...cameraRef.current,
      mode: "perspective",
      preserveSceneCache: true,
    });
  }, []);

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
  }, []);

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

        return Promise.resolve(app.threeRendererReady).then(() => {
          if (cancelled || runtimeRef.current?.app !== app) return;
          setCamera();
          app.threeRenderer?.invalidateSceneCache();
          app.render();
          publishRendererState(app, canvas);
          setStatus("ready");
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
  }, [setCamera]);

  useEffect(() => {
    const next = frameToPlayData(frame, blocks, world, compact);
    currentDataRef.current = next;
    const runtime = runtimeRef.current;
    if (!runtime) return;
    runtime.layerOffset = next.layerOffset;
    runtime.app.applyLevelState(next.playData, {
      deferRender: true,
      immediateCamera: true,
      resetHistory: true,
      resetLevelEntry: true,
    });
    runtime.app.threeRenderer?.invalidateSceneCache();
    runtime.app.render();
    if (canvasRef.current) publishRendererState(runtime.app, canvasRef.current);
  }, [blocks, compact, frame, world]);

  useEffect(() => {
    const wrapper = wrapperRef.current;
    if (!wrapper) return;
    const observer = new ResizeObserver(() => {
      const app = runtimeRef.current?.app;
      if (!app) return;
      app.setupCanvas();
      app.threeRenderer?.invalidateSceneCache();
      setCamera();
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
  }, [interactive, recomputeTiltDirection, rotateCamera]);

  const pick = (clientX: number, clientY: number) => {
    const canvas = canvasRef.current;
    const runtime = runtimeRef.current;
    if (!canvas || !runtime?.app.threeRenderer?.isReady()) return null;
    return runtime.app.threeRenderer.pickEditorFace(clientX, clientY, canvas);
  };

  const paintFromPointer = (event: PaintPointerInput, initialSample = false) => {
    const runtime = runtimeRef.current;
    if (!interactive || !onPaint || !runtime || orbitRef.current) return;
    const target = pick(event.clientX, event.clientY);
    if (!target || target.kind === "levelSwitch") return;
    const erase = eraseMode || event.button === 2 || (event.buttons & 2) === 2 || event.altKey;
    const replace = event.metaKey || event.ctrlKey;
    const x = erase || replace ? target.sourceX : target.paintX;
    const y = erase || replace ? target.sourceY : target.paintY;
    const runtimeLayer = erase || replace ? target.sourceLayer : target.paintLayer;
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
    onPaint(x, y, z, erase ? null : selectedBlock ?? null);
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

  return (
    <div ref={wrapperRef} className={`voxel-canvas-wrap mazebench-runtime-canvas ${compact ? "compact" : ""}`}>
      <canvas
        ref={canvasRef}
        aria-label={interactive ? "Interactive MazeBench perspective polycube editor" : "MazeBench perspective polycube preview"}
        onContextMenu={(event) => event.preventDefault()}
        onPointerDown={(event) => {
          if (!interactive) return;
          event.currentTarget.setPointerCapture(event.pointerId);
          if (event.shiftKey || event.button === 1) {
            orbitRef.current = {
              x: event.clientX,
              y: event.clientY,
              yaw: cameraRef.current.yaw,
              tilt: cameraRef.current.tilt,
            };
            return;
          }
          onPaintGestureStart?.();
          lastPaintRef.current = "";
          paintFromPointer(pointerSample(event), true);
        }}
        onPointerMove={(event) => {
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
          if ((event.buttons & 1) === 1) schedulePaintSample(event);
        }}
        onPointerLeave={() => runtimeRef.current?.app.threeRenderer?.setEditorHoverTarget(null)}
        onPointerUp={() => {
          flushPendingPaint();
          orbitRef.current = null;
          lastPaintRef.current = "";
          paintStrokeRef.current = null;
          onPaintGestureEnd?.();
        }}
        onPointerCancel={() => {
          flushPendingPaint();
          orbitRef.current = null;
          lastPaintRef.current = "";
          paintStrokeRef.current = null;
          onPaintGestureEnd?.();
        }}
        onWheel={(event) => {
          if (!interactive) return;
          event.preventDefault();
          setCamera({ zoom: Math.max(0.55, Math.min(10, cameraRef.current.zoom * (event.deltaY < 0 ? 1.1 : 0.9))) });
        }}
      />
      {status !== "ready" && (
        <div className={`runtime-status ${status}`} role="status">
          {status === "error" ? "MazeBench renderer failed to load" : "Loading MazeBench renderer…"}
        </div>
      )}
      {interactive && (
        <div className="control-pad camera-pad" aria-label="Camera controls">
          <button className="control-button dpad-button" type="button" data-camera="up" aria-label="Camera up" onPointerDown={() => { cameraMotionRef.current.pointerTiltDirection = -1; recomputeTiltDirection(); }} onPointerUp={() => { cameraMotionRef.current.pointerTiltDirection = 0; recomputeTiltDirection(); }} onPointerLeave={() => { cameraMotionRef.current.pointerTiltDirection = 0; recomputeTiltDirection(); }} />
          <button className="control-button dpad-button" type="button" data-camera="left" aria-label="Rotate camera left" onClick={() => rotateCamera(-1)} />
          <span className="dpad-center" aria-hidden="true">CAM</span>
          <button className="control-button dpad-button" type="button" data-camera="right" aria-label="Rotate camera right" onClick={() => rotateCamera(1)} />
          <button className="control-button dpad-button" type="button" data-camera="down" aria-label="Camera down" onPointerDown={() => { cameraMotionRef.current.pointerTiltDirection = 1; recomputeTiltDirection(); }} onPointerUp={() => { cameraMotionRef.current.pointerTiltDirection = 0; recomputeTiltDirection(); }} onPointerLeave={() => { cameraMotionRef.current.pointerTiltDirection = 0; recomputeTiltDirection(); }} />
        </div>
      )}
      {interactive && (
        <div className="canvas-hint">
          <span><b>Click</b> add to face</span>
          <span><b>E</b> erase tool</span>
          <span><b>WASD</b> smooth camera</span>
          <span><b>Shift + drag</b> orbit</span>
          <span><b>Scroll</b> zoom</span>
          <span><b>⌘Z</b> undo</span>
        </div>
      )}
    </div>
  );
}
