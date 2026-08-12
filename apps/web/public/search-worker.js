const DIRECTION_NAMES = ["up", "right", "down", "left"];

let stopped = false;
let physicsPromise = null;

function mulberry32(seed) {
  let value = seed >>> 0;
  return () => {
    value = (value + 0x6D2B79F5) >>> 0;
    let result = value;
    result = Math.imul(result ^ (result >>> 15), result | 1);
    result ^= result + Math.imul(result ^ (result >>> 7), result | 61);
    return ((result ^ (result >>> 14)) >>> 0) / 4294967296;
  };
}

function integer(random, minimum, maximum) {
  return minimum + Math.floor(random() * (maximum - minimum + 1));
}

function choose(random, values) {
  return values[Math.floor(random() * values.length)];
}

function keyOf(voxel) {
  return `${voxel.x},${voxel.y},${voxel.z}`;
}

function cloneCandidate(candidate) {
  return {
    ...candidate,
    voxels: candidate.voxels.map((voxel) => ({ ...voxel })),
    solution: [...(candidate.solution ?? [])],
  };
}

function roleBlocks(configuration) {
  const enabled = new Set(configuration.enabledBlockIds);
  const byRole = new Map();
  for (const block of configuration.blocks) {
    if (!enabled.has(block.id)) continue;
    const values = byRole.get(block.roleId) ?? [];
    values.push(block);
    byRole.set(block.roleId, values);
  }
  return byRole;
}

function firstRoleBlock(byRole, roleId) {
  return byRole.get(roleId)?.[0] ?? null;
}

function put(voxels, voxel) {
  const key = keyOf(voxel);
  const index = voxels.findIndex((item) => keyOf(item) === key);
  if (index >= 0) voxels[index] = voxel;
  else voxels.push(voxel);
}

function removeAt(voxels, x, y, z) {
  const key = `${x},${y},${z}`;
  const index = voxels.findIndex((voxel) => keyOf(voxel) === key);
  if (index >= 0) voxels.splice(index, 1);
}

function isCollectible(voxel, blockRoles) {
  return blockRoles.get(voxel.blockId) === "goal";
}

function rigidVoxelAt(voxels, x, y, z, blockRoles, ignored = null) {
  return voxels.find((voxel) => voxel !== ignored &&
    (!ignored || !(ignored instanceof Set) || !ignored.has(voxel)) &&
    !isCollectible(voxel, blockRoles) &&
    voxel.x === x && voxel.y === y && voxel.z === z) ?? null;
}

function highestSupport(voxels, x, y, layers, blockRoles, ignored = null) {
  let highest = -1;
  for (const voxel of voxels) {
    if (voxel === ignored || (ignored instanceof Set && ignored.has(voxel)) ||
        isCollectible(voxel, blockRoles)) continue;
    if (voxel.x === x && voxel.y === y && voxel.z <= layers) {
      highest = Math.max(highest, voxel.z);
    }
  }
  return highest;
}

function randomInterior(random, width, depth) {
  return {
    x: integer(random, 1, Math.max(1, width - 2)),
    y: integer(random, 1, Math.max(1, depth - 2)),
  };
}

function validateRequiredBlocks(byRole) {
  const missing = ["floor", "player", "goal"]
    .filter((role) => !firstRoleBlock(byRole, role));
  if (missing.length) {
    throw new Error(`Enable at least one ${missing.join(", ")} block before searching.`);
  }
}

const ADJACENT_3D = [
  [1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1],
];

function insideVolume(voxel, configuration) {
  return voxel.x >= 1 && voxel.x < configuration.width - 1 &&
    voxel.y >= 1 && voxel.y < configuration.depth - 1 &&
    voxel.z >= 1 && voxel.z <= configuration.layers + 1;
}

function insideStaticVolume(voxel, configuration) {
  return voxel.x >= 1 && voxel.x < configuration.width - 1 &&
    voxel.y >= 1 && voxel.y < configuration.depth - 1 &&
    voxel.z >= 0 && voxel.z <= configuration.layers;
}

function exactBlockClusters(voxels, blockIds) {
  const eligible = voxels.filter((voxel) => blockIds.has(voxel.blockId));
  const byKey = new Map(eligible.map((voxel) => [keyOf(voxel), voxel]));
  const visited = new Set();
  const clusters = [];
  for (const seed of eligible) {
    if (visited.has(seed)) continue;
    const members = [];
    const queue = [seed];
    visited.add(seed);
    while (queue.length) {
      const member = queue.pop();
      members.push(member);
      for (const [dx, dy, dz] of ADJACENT_3D) {
        const neighbor = byKey.get(`${member.x + dx},${member.y + dy},${member.z + dz}`);
        if (neighbor && neighbor.blockId === seed.blockId && !visited.has(neighbor)) {
          visited.add(neighbor);
          queue.push(neighbor);
        }
      }
    }
    clusters.push(members);
  }
  return clusters;
}

function remainsConnectedWithout(members, removed) {
  const remaining = members.filter((member) => member !== removed);
  if (remaining.length <= 1) return true;
  const byKey = new Map(remaining.map((member) => [keyOf(member), member]));
  const visited = new Set([remaining[0]]);
  const stack = [remaining[0]];
  while (stack.length) {
    const member = stack.pop();
    for (const [dx, dy, dz] of ADJACENT_3D) {
      const neighbor = byKey.get(`${member.x + dx},${member.y + dy},${member.z + dz}`);
      if (neighbor && !visited.has(neighbor)) {
        visited.add(neighbor);
        stack.push(neighbor);
      }
    }
  }
  return visited.size === remaining.length;
}

function growStaticCluster(
  voxels,
  members,
  configuration,
  random,
  blockRoles,
  floorBlock,
) {
  if (!members.length) return false;
  const minimumZ = blockRoles.get(members[0].blockId) === "ice" ? 0 : 1;
  const candidates = [];
  const seen = new Set();
  for (const origin of members) {
    for (const [dx, dy, dz] of ADJACENT_3D) {
      const voxel = {
        x: origin.x + dx,
        y: origin.y + dy,
        z: origin.z + dz,
        blockId: origin.blockId,
      };
      const key = keyOf(voxel);
      if (seen.has(key) || !insideStaticVolume(voxel, configuration) ||
          voxel.z < minimumZ) continue;
      const occupant = rigidVoxelAt(voxels, voxel.x, voxel.y, voxel.z, blockRoles);
      if (occupant && !(voxel.z === 0 && occupant.blockId === floorBlock.id)) continue;
      seen.add(key);
      candidates.push({ voxel, occupant });
    }
  }
  if (!candidates.length) return false;
  const { voxel, occupant } = choose(random, candidates);
  if (occupant) voxels.splice(voxels.indexOf(occupant), 1);
  voxels.push(voxel);
  members.push(voxel);
  return true;
}

function shrinkStaticCluster(voxels, members, random, floorBlock) {
  if (members.length <= 1) return false;
  const removable = members.filter((member) => remainsConnectedWithout(members, member));
  if (!removable.length) return false;
  const member = choose(random, removable);
  voxels.splice(voxels.indexOf(member), 1);
  if (member.z === 0) {
    voxels.push({ x: member.x, y: member.y, z: 0, blockId: floorBlock.id });
  }
  return true;
}

function seedStaticCluster(
  voxels,
  block,
  configuration,
  random,
  blockRoles,
  floorBlock,
  seedZ,
  targetSize,
) {
  for (let attempt = 0; attempt < 40; attempt += 1) {
    const cell = randomInterior(random, configuration.width, configuration.depth);
    const seed = { ...cell, z: seedZ, blockId: block.id };
    const occupant = rigidVoxelAt(voxels, seed.x, seed.y, seed.z, blockRoles);
    if (occupant && !(seed.z === 0 && occupant.blockId === floorBlock.id)) continue;
    if (ADJACENT_3D.some(([dx, dy, dz]) => voxels.some((voxel) =>
      voxel.blockId === block.id && voxel.x === seed.x + dx &&
      voxel.y === seed.y + dy && voxel.z === seed.z + dz))) continue;
    if (seedZ === 1 && !rigidVoxelAt(
      voxels, seed.x, seed.y, 0, blockRoles,
    )) {
      voxels.push({ x: seed.x, y: seed.y, z: 0, blockId: floorBlock.id });
    }
    if (occupant) voxels.splice(voxels.indexOf(occupant), 1);
    voxels.push(seed);
    const members = [seed];
    for (let growth = members.length; growth < targetSize; growth += 1) {
      if (!growStaticCluster(
        voxels, members, configuration, random, blockRoles, floorBlock,
      )) break;
    }
    return true;
  }
  return false;
}

function boxEntities(voxels, pushableBlocks) {
  const roles = new Map(pushableBlocks.map((block) => [block.id, block.roleId]));
  const entities = new Map();
  voxels.forEach((voxel, index) => {
    const role = roles.get(voxel.blockId);
    if (!role) return;
    const key = role === "weightless-pushable"
      ? `generic:${voxel.genericId ?? 0}`
      : `single:${index}`;
    const members = entities.get(key) ?? [];
    members.push(voxel);
    entities.set(key, members);
  });
  return [...entities.values()];
}

function growGenericBox(voxels, members, configuration, random, blockRoles) {
  if (!members.length || blockRoles.get(members[0].blockId) !== "weightless-pushable") {
    return false;
  }
  const candidates = [];
  const seen = new Set();
  for (const origin of members) {
    for (const [dx, dy, dz] of ADJACENT_3D) {
      const voxel = {
        x: origin.x + dx,
        y: origin.y + dy,
        z: origin.z + dz,
        blockId: origin.blockId,
        genericId: origin.genericId ?? 0,
      };
      const key = keyOf(voxel);
      if (seen.has(key) || !insideVolume(voxel, configuration) || rigidVoxelAt(
        voxels, voxel.x, voxel.y, voxel.z, blockRoles,
      )) continue;
      seen.add(key);
      candidates.push(voxel);
    }
  }
  if (!candidates.length) return false;
  const voxel = choose(random, candidates);
  voxels.push(voxel);
  members.push(voxel);
  return true;
}

function shrinkGenericBox(voxels, members, random) {
  if (members.length <= 1) return false;
  const removable = members.filter((member) => remainsConnectedWithout(members, member));
  if (!removable.length) return false;
  const member = choose(random, removable);
  voxels.splice(voxels.indexOf(member), 1);
  return true;
}

function translateBox(voxels, members, configuration, random, blockRoles) {
  const memberSet = new Set(members);
  for (let attempt = 0; attempt < 12; attempt += 1) {
    const [dx, dy, dz] = choose(random, ADJACENT_3D);
    const translated = members.map((member) => ({
      ...member,
      x: member.x + dx,
      y: member.y + dy,
      z: member.z + dz,
    }));
    if (translated.some((voxel) => !insideVolume(voxel, configuration) ||
        rigidVoxelAt(voxels, voxel.x, voxel.y, voxel.z, blockRoles, memberSet))) {
      continue;
    }
    const hasDirectSupport = translated.some((voxel) => rigidVoxelAt(
      voxels, voxel.x, voxel.y, voxel.z - 1, blockRoles, memberSet,
    ));
    if (!hasDirectSupport) continue;
    members.forEach((member, index) => Object.assign(member, translated[index]));
    return true;
  }
  return false;
}

function dynamicsAreSettled(voxels, configuration, blockRoles, pushableBlocks) {
  const rigidCoordinates = new Set();
  for (const voxel of voxels) {
    if (isCollectible(voxel, blockRoles)) continue;
    const key = keyOf(voxel);
    if (rigidCoordinates.has(key)) return false;
    rigidCoordinates.add(key);
  }
  const player = voxels.find((voxel) => blockRoles.get(voxel.blockId) === "player");
  if (!player || !insideVolume(player, configuration) || !rigidVoxelAt(
    voxels, player.x, player.y, player.z - 1, blockRoles, player,
  )) return false;
  return boxEntities(voxels, pushableBlocks).every((members) => {
    const memberSet = new Set(members);
    return members.length > 0 && members.every((member) => insideVolume(member, configuration)) &&
      members.some((member) => rigidVoxelAt(
        voxels, member.x, member.y, member.z - 1, blockRoles, memberSet,
      ));
  });
}

function placeSupportedBox(
  voxels,
  block,
  genericId,
  targetSize,
  configuration,
  random,
  blockRoles,
) {
  for (let attempt = 0; attempt < 60; attempt += 1) {
    const cell = randomInterior(random, configuration.width, configuration.depth);
    const z = highestSupport(
      voxels, cell.x, cell.y, configuration.layers, blockRoles,
    ) + 1;
    if (z <= 0 || z > configuration.layers + 1 || rigidVoxelAt(
      voxels, cell.x, cell.y, z, blockRoles,
    )) continue;
    const voxel = {
      ...cell,
      z,
      blockId: block.id,
      ...(block.roleId === "weightless-pushable" ? { genericId } : {}),
    };
    voxels.push(voxel);
    const members = [voxel];
    for (let extra = 1; extra < targetSize; extra += 1) {
      if (!growGenericBox(
        voxels, members, configuration, random, blockRoles,
      )) break;
    }
    return members;
  }
  const anchors = [];
  for (let y = 1; y < configuration.depth - 1; y += 1) {
    for (let x = 1; x < configuration.width - 1; x += 1) {
      const z = highestSupport(voxels, x, y, configuration.layers, blockRoles) + 1;
      if (z > 0 && z <= configuration.layers + 1 &&
          !rigidVoxelAt(voxels, x, y, z, blockRoles)) {
        anchors.push({ x, y, z });
      }
    }
  }
  if (anchors.length) {
    const anchor = choose(random, anchors);
    const voxel = {
      ...anchor,
      blockId: block.id,
      ...(block.roleId === "weightless-pushable" ? { genericId } : {}),
    };
    voxels.push(voxel);
    const members = [voxel];
    for (let extra = 1; extra < targetSize; extra += 1) {
      if (!growGenericBox(voxels, members, configuration, random, blockRoles)) break;
    }
    return members;
  }
  return [];
}

function initialStaticClusterSize(random, configuration, clusterCount) {
  const interiorArea = Math.max(1, (configuration.width - 2) * (configuration.depth - 2));
  const terrainBudget = interiorArea * (0.12 + random() * 0.28);
  return integer(random, 1, Math.max(2, Math.floor(terrainBudget / clusterCount)));
}

function carveInitialHoles(voxels, configuration, random, blockRoles, floorBlock, iceBlocks) {
  const area = Math.max(1, (configuration.width - 2) * (configuration.depth - 2));
  const clusterCount = integer(random, 1, Math.max(1, Math.floor(area / 48) + 1));
  const rowZeroTerrain = new Set([floorBlock.id, ...iceBlocks.map((block) => block.id)]);
  let carved = 0;
  for (let cluster = 0; cluster < clusterCount; cluster += 1) {
    let cell = randomInterior(random, configuration.width, configuration.depth);
    const length = integer(random, 2, Math.max(3, Math.floor(area * 0.1)));
    for (let step = 0; step < length; step += 1) {
      const occupant = voxels.find((voxel) =>
        voxel.x === cell.x && voxel.y === cell.y && voxel.z === 0);
      const hasStructureAbove = voxels.some((voxel) =>
        voxel.x === cell.x && voxel.y === cell.y && voxel.z > 0 &&
        !isCollectible(voxel, blockRoles));
      if (occupant && rowZeroTerrain.has(occupant.blockId) && !hasStructureAbove) {
        removeAt(voxels, cell.x, cell.y, 0);
        carved += 1;
      }
      const [dx, dy] = choose(random, ADJACENT_3D.slice(0, 4));
      cell = {
        x: Math.max(1, Math.min(configuration.width - 2, cell.x + dx)),
        y: Math.max(1, Math.min(configuration.depth - 2, cell.y + dy)),
      };
    }
  }
  if (!carved) {
    const candidates = voxels.filter((voxel) => voxel.z === 0 &&
      rowZeroTerrain.has(voxel.blockId) && !voxels.some((other) =>
        other.x === voxel.x && other.y === voxel.y && other.z > 0 &&
        !isCollectible(other, blockRoles)));
    if (candidates.length) {
      const fallback = choose(random, candidates);
      removeAt(voxels, fallback.x, fallback.y, 0);
    }
  }
}

function makeCandidate(configuration, random) {
  const { width, depth, layers } = configuration;
  const byRole = roleBlocks(configuration);
  validateRequiredBlocks(byRole);
  const floor = firstRoleBlock(byRole, "floor");
  const player = firstRoleBlock(byRole, "player");
  const goals = byRole.get("goal") ?? [];
  const iceBlocks = byRole.get("ice") ?? [];
  const normalPushables = byRole.get("pushable") ?? [];
  const weightlessPushables = byRole.get("weightless-pushable") ?? [];
  const structural = configuration.blocks.filter((block) =>
    configuration.enabledBlockIds.includes(block.id) &&
    !["floor", "player", "goal", "ice", "pushable", "weightless-pushable"]
      .includes(block.roleId));
  const voxels = [];
  const blockRoles = new Map(configuration.blocks.map((block) => [block.id, block.roleId]));

  for (let y = 0; y < depth; y += 1) {
    for (let x = 0; x < width; x += 1) {
      voxels.push({ x, y, z: 0, blockId: floor.id });
    }
  }

  const staticClusterMaximum = Math.max(
    2, Math.min(8, Math.floor((width * depth) / 12)),
  );
  for (const ice of iceBlocks) {
    const clusterCount = integer(random, 2, staticClusterMaximum);
    for (let cluster = 0; cluster < clusterCount; cluster += 1) {
      seedStaticCluster(
        voxels,
        ice,
        configuration,
        random,
        blockRoles,
        floor,
        0,
        initialStaticClusterSize(random, configuration, clusterCount),
      );
    }
  }

  for (const block of structural) {
    const clusterCount = integer(random, 2, staticClusterMaximum);
    for (let cluster = 0; cluster < clusterCount; cluster += 1) {
      seedStaticCluster(
        voxels,
        block,
        configuration,
        random,
        blockRoles,
        floor,
        1,
        initialStaticClusterSize(random, configuration, clusterCount),
      );
    }
  }

  if (configuration.evolveHoles) {
    carveInitialHoles(voxels, configuration, random, blockRoles, floor, iceBlocks);
  }

  const goalCells = [];
  const goalCount = Math.min(
    Math.max(1, configuration.collectibles ?? 1),
    Math.max(1, (width - 2) * (depth - 2)),
  );
  for (let goalIndex = 0; goalIndex < goalCount; goalIndex += 1) {
    for (let attempt = 0; attempt < 40; attempt += 1) {
      const cell = randomInterior(random, width, depth);
      if (goalCells.some((goalCell) => goalCell.x === cell.x && goalCell.y === cell.y)) continue;
      const goalZ = highestSupport(voxels, cell.x, cell.y, layers, blockRoles) + 1;
      if (goalZ <= 0 || goalZ > layers + 1) continue;
      voxels.push({ ...cell, z: goalZ, blockId: choose(random, goals).id });
      goalCells.push(cell);
      break;
    }
  }

  if (weightlessPushables.length) {
    const minimumBoxes = Math.max(0, configuration.minWeightlessBoxes ?? 1);
    const maximumBoxes = Math.max(minimumBoxes, configuration.maxWeightlessBoxes ?? 4);
    const boxCount = integer(random, minimumBoxes, maximumBoxes);
    // This is an initial random target, not a polycube size limit. Subsequent
    // mutations can keep growing every object until the selected volume fills.
    const initialSizeTarget = Math.max(
      1,
      Math.floor(((width - 2) * (depth - 2)) / Math.max(4, boxCount)),
    );
    for (let genericId = 0; genericId < boxCount; genericId += 1) {
      const placed = placeSupportedBox(
        voxels,
        choose(random, weightlessPushables),
        genericId,
        integer(random, 1, initialSizeTarget),
        configuration,
        random,
        blockRoles,
      );
      if (!placed.length) {
        throw new Error(
          `The selected volume cannot fit ${boxCount} distinct weightless polycubes.`,
        );
      }
    }
  }

  if (normalPushables.length) {
    const boxCount = integer(
      random, 0, Math.min(4, Math.max(1, Math.floor(width * depth / 20))),
    );
    for (let index = 0; index < boxCount; index += 1) {
      placeSupportedBox(
        voxels,
        choose(random, normalPushables),
        -1,
        1,
        configuration,
        random,
        blockRoles,
      );
    }
  }

  for (let attempt = 0; attempt < 100; attempt += 1) {
    const cell = randomInterior(random, width, depth);
    const z = highestSupport(voxels, cell.x, cell.y, layers, blockRoles) + 1;
    if (z <= 0 || z > layers + 1 ||
        goalCells.some((goalCell) => cell.x === goalCell.x && cell.y === goalCell.y) ||
        rigidVoxelAt(voxels, cell.x, cell.y, z, blockRoles)) {
      continue;
    }
    voxels.push({ ...cell, z, blockId: player.id });
    break;
  }

  return {
    id: `candidate-${Date.now()}-${integer(random, 0, 0xFFFFFF).toString(16)}`,
    name: "Evolution candidate",
    createdAt: new Date().toISOString(),
    world: { width, height: depth, floorLayer: 0 },
    layers,
    voxels,
    solution: [],
    moves: 0,
    expanded: 0,
    generated: 0,
    nodesPerSecond: 0,
    optimal: false,
    seed: configuration.seed,
  };
}

function mutateCandidate(candidate, configuration, random) {
  const next = cloneCandidate(candidate);
  next.solution = [];
  next.optimal = false;
  const byRole = roleBlocks(configuration);
  const playerBlock = firstRoleBlock(byRole, "player");
  const floorBlock = firstRoleBlock(byRole, "floor");
  const iceBlocks = byRole.get("ice") ?? [];
  const weightlessPushableBlocks = byRole.get("weightless-pushable") ?? [];
  const pushableBlocks = [
    ...(byRole.get("pushable") ?? []),
    ...(byRole.get("weightless-pushable") ?? []),
  ];
  const structural = configuration.blocks.filter((block) =>
    configuration.enabledBlockIds.includes(block.id) &&
    !["floor", "player", "goal", "ice", "pushable", "weightless-pushable"]
      .includes(block.roleId));
  const blockRoles = new Map(configuration.blocks.map((block) => [block.id, block.roleId]));
  const iceClusters = exactBlockClusters(
    next.voxels, new Set(iceBlocks.map((block) => block.id)),
  );
  const structureClusters = exactBlockClusters(
    next.voxels, new Set(structural.map((block) => block.id)),
  );
  const weightlessBoxes = boxEntities(next.voxels, weightlessPushableBlocks);
  const minimumWeightlessBoxes = Math.max(0, configuration.minWeightlessBoxes ?? 1);
  const maximumWeightlessBoxes = Math.max(
    minimumWeightlessBoxes, configuration.maxWeightlessBoxes ?? 4,
  );
  // Entity-first selection gives walls/structures, Ice, holes, the player,
  // the goal, and every individual box equal mutation opportunity. Invalid
  // operations retry within that entity instead of biasing another category.
  const entities = [
    ...structureClusters.map((members) => ({ kind: "static", members })),
    ...iceClusters.map((members) => ({ kind: "static", members })),
    ...(weightlessPushableBlocks.length &&
        (weightlessBoxes.length !== minimumWeightlessBoxes ||
         weightlessBoxes.length !== maximumWeightlessBoxes)
      ? [{ kind: "weightless-population" }]
      : []),
    ...(configuration.evolveHoles ? [{ kind: "holes" }] : []),
    ...(playerBlock ? [{ kind: "player" }] : []),
    ...next.voxels.filter((voxel) => isCollectible(voxel, blockRoles))
      .map((member) => ({ kind: "goal", member })),
    ...boxEntities(next.voxels, pushableBlocks).map((members) => ({ kind: "box", members })),
  ];
  const entity = choose(random, entities);

  for (let attempt = 0; attempt < 18; attempt += 1) {
    if (entity.kind === "static") {
      const changed = random() < 0.5
        ? growStaticCluster(
          next.voxels,
          entity.members,
          configuration,
          random,
          blockRoles,
          floorBlock,
        )
        : shrinkStaticCluster(next.voxels, entity.members, random, floorBlock);
      if (changed) break;
    } else if (entity.kind === "holes") {
      const cell = randomInterior(random, configuration.width, configuration.depth);
      const existing = next.voxels.find((voxel) =>
        voxel.x === cell.x && voxel.y === cell.y && voxel.z === 0);
      if (existing && (existing.blockId === floorBlock.id ||
          iceBlocks.some((block) => block.id === existing.blockId))) {
        removeAt(next.voxels, cell.x, cell.y, 0);
      } else if (!existing) {
        put(next.voxels, { ...cell, z: 0, blockId: floorBlock.id });
      } else {
        continue;
      }
      break;
    } else if (entity.kind === "weightless-population") {
      const shouldAdd = weightlessBoxes.length < minimumWeightlessBoxes ||
        (weightlessBoxes.length < maximumWeightlessBoxes &&
         (weightlessBoxes.length <= minimumWeightlessBoxes || random() < 0.5));
      if (shouldAdd) {
        const usedIds = new Set(weightlessBoxes.map((members) => members[0].genericId ?? 0));
        let genericId = 0;
        while (usedIds.has(genericId)) genericId += 1;
        if (placeSupportedBox(
          next.voxels,
          choose(random, weightlessPushableBlocks),
          genericId,
          1,
          configuration,
          random,
          blockRoles,
        ).length) break;
      } else if (weightlessBoxes.length > minimumWeightlessBoxes) {
        const removed = choose(random, weightlessBoxes);
        for (const member of removed) next.voxels.splice(next.voxels.indexOf(member), 1);
        break;
      }
    } else if (entity.kind === "box") {
      const operation = random();
      const changed = operation < 0.3
        ? growGenericBox(
          next.voxels,
          entity.members,
          configuration,
          random,
          blockRoles,
        )
        : operation < 0.6
          ? shrinkGenericBox(next.voxels, entity.members, random)
          : translateBox(next.voxels, entity.members, configuration, random, blockRoles);
      if (changed) break;
    } else if (entity.kind === "player") {
      const player = next.voxels.find((voxel) => voxel.blockId === playerBlock.id);
      const cell = randomInterior(random, configuration.width, configuration.depth);
      const z = highestSupport(
        next.voxels,
        cell.x,
        cell.y,
        configuration.layers,
        blockRoles,
        player,
      ) + 1;
      if (player && z > 0 && z <= configuration.layers + 1 &&
          !next.voxels.some((voxel) => isCollectible(voxel, blockRoles) &&
            voxel.x === cell.x && voxel.y === cell.y && voxel.z === z) &&
          !rigidVoxelAt(next.voxels, cell.x, cell.y, z, blockRoles, player)) {
        Object.assign(player, cell, { z });
        break;
      }
    } else if (entity.kind === "goal") {
      const currentGoals = next.voxels.filter((voxel) => isCollectible(voxel, blockRoles));
      const cell = randomInterior(random, configuration.width, configuration.depth);
      const goalZ = highestSupport(
        next.voxels, cell.x, cell.y, configuration.layers, blockRoles, entity.member,
      ) + 1;
      if (goalZ <= 0 || goalZ > configuration.layers + 1 ||
          currentGoals.some((voxel) => voxel !== entity.member &&
            voxel.x === cell.x && voxel.y === cell.y && voxel.z === goalZ)) continue;
      Object.assign(entity.member, cell, { z: goalZ });
      break;
    }
  }

  // Floor is a plane-only material. This final guard also protects imported
  // candidates and future mutation operators.
  next.voxels = next.voxels.filter((voxel) =>
    voxel.blockId !== floorBlock.id || voxel.z === 0);
  if (!dynamicsAreSettled(next.voxels, configuration, blockRoles, pushableBlocks)) {
    return cloneCandidate(candidate);
  }
  return next;
}

async function loadPhysics() {
  if (!physicsPromise) {
    physicsPromise = fetch("/physics/voxel_physics.wasm")
      .then((response) => {
        if (!response.ok) throw new Error(`C++ search engine failed to load (${response.status})`);
        return response.arrayBuffer();
      })
      .then((bytes) => WebAssembly.instantiate(bytes, {}))
      .then(({ instance }) => instance.exports);
  }
  return physicsPromise;
}

function roleCodes(physics, roles) {
  const encoder = new TextEncoder();
  const buffer = new Uint8Array(
    physics.memory.buffer,
    physics.role_buffer(),
    physics.role_buffer_capacity(),
  );
  const codes = new Map();
  for (const role of roles) {
    const bytes = encoder.encode(role.id);
    buffer.set(bytes.subarray(0, physics.role_buffer_capacity()));
    codes.set(role.id, physics.role_code(bytes.length));
  }
  return codes;
}

async function evaluate(candidate, configuration, physics, codes) {
  if (candidate.voxels.length > physics.search_voxel_capacity()) {
    throw new Error(
      `Candidate has ${candidate.voxels.length} voxels; exact search supports ${physics.search_voxel_capacity()}.`,
    );
  }
  const blockRoles = new Map(configuration.blocks.map((block) => [
    block.id,
    codes.get(block.roleId) ?? 0,
  ]));
  const genericRoles = new Set(configuration.roles.filter((role) => role.generic).map((role) => role.id));
  const genericBlocks = new Set(configuration.blocks
    .filter((block) => genericRoles.has(block.roleId)).map((block) => block.id));
  const stride = physics.voxel_stride();
  const buffer = new Int32Array(
    physics.memory.buffer,
    physics.voxel_buffer(),
    candidate.voxels.length * stride,
  );
  candidate.voxels.forEach((voxel, index) => {
    buffer.set([
      voxel.x,
      voxel.y,
      voxel.z,
      blockRoles.get(voxel.blockId) ?? 0,
      genericBlocks.has(voxel.blockId) ? Math.max(0, voxel.genericId ?? 0) : -1,
    ], index * stride);
  });
  const started = performance.now();
  const status = physics.search_solve(
    candidate.voxels.length,
    candidate.world.width,
    candidate.world.height,
    Math.min(configuration.maxNodes, physics.search_node_capacity()),
  );
  const elapsedMs = Math.max(0.001, performance.now() - started);
  const expanded = physics.search_expanded();
  const length = physics.search_solution_length();
  const solution = [];
  for (let index = 0; index < length; index += 1) {
    solution.push(DIRECTION_NAMES[physics.search_solution_step(index)]);
  }
  return {
    ...candidate,
    solution,
    moves: physics.search_moves(),
    expanded,
    generated: physics.search_generated(),
    transpositions: physics.search_transpositions(),
    nodesPerSecond: Math.round(expanded / (elapsedMs / 1000)),
    elapsedMs,
    optimal: status === 1,
    limitHit: status === 2,
  };
}

function better(left, right) {
  if (!right) return true;
  if (left.optimal !== right.optimal) return left.optimal;
  if (left.optimal && left.moves !== right.moves) return left.moves > right.moves;
  if (left.limitHit !== right.limitHit) return !left.limitHit;
  return left.expanded > right.expanded;
}

async function evolve(configuration) {
  const physics = await loadPhysics();
  const codes = roleCodes(physics, configuration.roles);
  const random = mulberry32(configuration.seed);
  const populationSize = Math.max(4, Math.min(128, configuration.population));
  let population = Array.from(
    { length: populationSize },
    () => makeCandidate(configuration, random),
  );
  let best = null;
  let evaluated = 0;
  let totalExpanded = 0;
  let totalElapsedMs = 0;

  for (let generation = 1; generation <= configuration.generations; generation += 1) {
    if (stopped) return;
    const scored = [];
    for (let index = 0; index < population.length; index += 1) {
      if (stopped) return;
      try {
        const result = await evaluate(population[index], configuration, physics, codes);
        scored.push(result);
        evaluated += 1;
        totalExpanded += result.expanded;
        totalElapsedMs += result.elapsedMs;
        if (better(result, best)) {
          best = cloneCandidate(result);
          self.postMessage({ type: "best", candidate: best, generation, evaluated });
        }
      } catch {
        // Invalid candidates have no fitness and are replaced next generation.
      }
      if (index % 4 === 3 || index === population.length - 1) {
        self.postMessage({
          type: "progress",
          generation,
          generations: configuration.generations,
          evaluated,
          bestMoves: best?.optimal ? best.moves : 0,
          nodesPerSecond: Math.round(totalExpanded / Math.max(0.001, totalElapsedMs / 1000)),
        });
        await new Promise((resolve) => setTimeout(resolve, 0));
      }
    }
    scored.sort((left, right) => better(left, right) ? -1 : better(right, left) ? 1 : 0);
    const eliteCount = Math.max(1, Math.ceil(populationSize * 0.18));
    const elites = scored.slice(0, eliteCount);
    const next = elites.map(cloneCandidate);
    while (next.length < populationSize) {
      if (!elites.length || random() < 0.12) {
        next.push(makeCandidate(configuration, random));
        continue;
      }
      let child = cloneCandidate(choose(random, elites));
      const mutations = integer(random, 1, generation % 20 === 0 ? 7 : 4);
      for (let mutation = 0; mutation < mutations; mutation += 1) {
        child = mutateCandidate(child, configuration, random);
      }
      next.push(child);
    }
    population = next;
  }

  self.postMessage({
    type: "done",
    candidate: best,
    evaluated,
    nodesPerSecond: Math.round(totalExpanded / Math.max(0.001, totalElapsedMs / 1000)),
  });
}

if (typeof self !== "undefined") {
  self.addEventListener("message", (event) => {
    if (event.data?.type === "stop") {
      stopped = true;
      return;
    }
    if (event.data?.type !== "start") return;
    stopped = false;
    evolve(event.data.configuration).catch((error) => {
      self.postMessage({ type: "error", message: error instanceof Error ? error.message : String(error) });
    });
  });
}

export {
  boxEntities,
  exactBlockClusters,
  makeCandidate,
  mulberry32,
  mutateCandidate,
  shrinkGenericBox,
  shrinkStaticCluster,
};
