type Direction = "up" | "down" | "left" | "right";
type PhysicsRole = { id: string; generic: boolean };
type BlockDefinition = { id: string; roleId: string };
type Voxel = { x: number; y: number; z: number; blockId: string; genericId?: number };
type Frame = { voxels: Voxel[] };
type WorldSettings = { width: number; height: number };

type PhysicsExports = WebAssembly.Exports & {
  memory: WebAssembly.Memory;
  physics_abi_version: () => number;
  role_buffer: () => number;
  role_buffer_capacity: () => number;
  role_code: (length: number) => number;
  simulate_turn: (count: number, width: number, height: number, direction: number) => number;
  voxel_buffer: () => number;
  voxel_capacity: () => number;
  voxel_stride: () => number;
};

let physicsPromise: Promise<PhysicsExports> | null = null;

async function loadPhysics() {
  if (!physicsPromise) {
    physicsPromise = fetch("/physics/voxel_physics.wasm")
      .then((response) => {
        if (!response.ok) throw new Error(`C++ physics engine failed to load (${response.status})`);
        return response.arrayBuffer();
      })
      .then((bytes) => WebAssembly.instantiate(bytes, {}))
      .then(({ instance }) => instance.exports as PhysicsExports);
  }
  return physicsPromise;
}

function directionCode(direction: Direction) {
  return { up: 0, right: 1, down: 2, left: 3 }[direction];
}

async function roleCodesById(physics: PhysicsExports, roles: PhysicsRole[]) {
  const encoder = new TextEncoder();
  const roleBufferPointer = physics.role_buffer();
  const roleBufferCapacity = physics.role_buffer_capacity();
  const codes = new Map<string, number>();

  for (const role of roles) {
    const bytes = encoder.encode(role.id);
    if (bytes.length > roleBufferCapacity) {
      throw new Error(`Physics role key is too long: ${role.id}`);
    }
    new Uint8Array(physics.memory.buffer, roleBufferPointer, bytes.length).set(bytes);
    codes.set(role.id, physics.role_code(bytes.length));
  }
  return codes;
}

export async function simulateTurnWithCpp(
  frame: Frame,
  direction: Direction,
  blocks: BlockDefinition[],
  roles: PhysicsRole[],
  world: WorldSettings,
): Promise<Frame> {
  const physics = await loadPhysics();
  if (physics.physics_abi_version() !== 2 || physics.voxel_stride() !== 5) {
    throw new Error("The web app and C++ physics engine use different ABI versions");
  }
  if (frame.voxels.length > physics.voxel_capacity()) {
    throw new Error(`The C++ engine supports up to ${physics.voxel_capacity()} voxels per frame`);
  }

  const rolesById = await roleCodesById(physics, roles);
  const blockRoles = new Map(blocks.map((block) => [block.id, rolesById.get(block.roleId) ?? 0]));
  const genericRoleIds = new Set(roles.filter((role) => role.generic).map((role) => role.id));
  const genericBlockIds = new Set(
    blocks.filter((block) => genericRoleIds.has(block.roleId)).map((block) => block.id),
  );
  const stride = physics.voxel_stride();
  const voxelBuffer = new Int32Array(
    physics.memory.buffer,
    physics.voxel_buffer(),
    frame.voxels.length * stride,
  );
  frame.voxels.forEach((voxel, index) => {
    const offset = index * stride;
    voxelBuffer[offset] = voxel.x;
    voxelBuffer[offset + 1] = voxel.y;
    voxelBuffer[offset + 2] = voxel.z;
    voxelBuffer[offset + 3] = blockRoles.get(voxel.blockId) ?? 0;
    voxelBuffer[offset + 4] = genericBlockIds.has(voxel.blockId)
      ? Math.max(0, Math.floor(Number(voxel.genericId) || 0))
      : -1;
  });

  const status = physics.simulate_turn(
    frame.voxels.length,
    world.width,
    world.height,
    directionCode(direction),
  );
  if (status === -1) throw new Error("The C++ engine rejected invalid turn data");

  return {
    voxels: frame.voxels.map((voxel, index) => {
      const offset = index * stride;
      return {
        ...voxel,
        x: voxelBuffer[offset],
        y: voxelBuffer[offset + 1],
        z: voxelBuffer[offset + 2],
      };
    }),
  };
}
