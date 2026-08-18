/**
 * A test is visible in a tag when one of its direct tags is that tag or one
 * of its descendants. The visited set also makes malformed legacy cycles safe.
 *
 * @param {readonly string[]} directTagIds
 * @param {string} tagId
 * @param {ReadonlyMap<string, { id: string, parentId?: string }>} tagsById
 */
export function directTagsIncludeTag(directTagIds, tagId, tagsById) {
  return directTagIds.some((directTagId) => {
    const visited = new Set();
    let cursor = tagsById.get(directTagId);
    while (cursor && !visited.has(cursor.id)) {
      if (cursor.id === tagId) return true;
      visited.add(cursor.id);
      cursor = cursor.parentId ? tagsById.get(cursor.parentId) : undefined;
    }
    return false;
  });
}

/** @param {readonly string[]} directTagIds @param {ReadonlySet<string>} lockedTagIds */
export function directTagsAreLocked(directTagIds, lockedTagIds) {
  return directTagIds.some((tagId) => lockedTagIds.has(tagId));
}

/**
 * @param {string} tagId
 * @param {ReadonlyMap<string, { id: string, parentId?: string }>} tagsById
 */
export function rootTagId(tagId, tagsById) {
  const visited = new Set();
  let cursor = tagsById.get(tagId);
  let rootId;
  while (cursor && !visited.has(cursor.id)) {
    rootId = cursor.id;
    visited.add(cursor.id);
    cursor = cursor.parentId ? tagsById.get(cursor.parentId) : undefined;
  }
  return rootId;
}

/**
 * @param {string} groupTagId
 * @param {readonly { default?: boolean, id: string, name: string, parentId?: string }[]} tags
 */
export function defaultSubtagId(groupTagId, tags) {
  return tags.find((tag) => tag.parentId === groupTagId &&
    (tag.default || tag.name.trim().toLowerCase() === "default"))?.id;
}

/**
 * Add one stable, reserved Default child to every top-level tag group.
 * Existing default children are normalized instead of duplicated.
 *
 * @template {{ collapsed?: boolean, default?: boolean, id: string, locked?: boolean, name: string, parentId?: string }} T
 * @param {readonly T[]} tags
 * @returns {Array<T | (T & { default: true, parentId: string })>}
 */
export function ensureDefaultSubtags(tags) {
  const usedIds = new Set(tags.map((tag) => tag.id));
  const roots = tags.filter((tag) => !tag.parentId);
  const existingDefaults = new Map(roots.flatMap((root) => {
    const existing = tags.find((tag) => tag.parentId === root.id &&
      (tag.default || tag.name.trim().toLowerCase() === "default"));
    return existing ? [[root.id, existing.id]] : [];
  }));
  const generatedDefaults = new Map();
  for (const root of roots) {
    if (existingDefaults.has(root.id)) continue;
    const baseId = `${root.id}-default`;
    let id = baseId;
    let suffix = 2;
    while (usedIds.has(id)) id = `${baseId}-${suffix++}`;
    usedIds.add(id);
    generatedDefaults.set(root.id, {
      collapsed: false,
      default: true,
      id,
      locked: false,
      name: "Default",
      parentId: root.id,
    });
  }
  return tags.flatMap((tag) => {
    const normalized = tag.parentId && existingDefaults.get(tag.parentId) === tag.id
      ? { ...tag, default: true, name: "Default" }
      : tag;
    return tag.parentId
      ? [normalized]
      : [normalized, ...(generatedDefaults.has(tag.id) ? [generatedDefaults.get(tag.id)] : [])];
  });
}

/**
 * Enforce one root group and one-or-more direct subtags. A Default subtag is
 * used only when the case has no authored subtag in its selected group.
 *
 * @param {{ directTagIds?: readonly string[], preferredTagId?: string }} value
 * @param {readonly { default?: boolean, id: string, name: string, parentId?: string }[]} tags
 */
export function normalizeTestTagPlacement(value, tags) {
  const tagsById = new Map(tags.map((tag) => [tag.id, tag]));
  const rootTags = tags.filter((tag) => !tag.parentId);
  const validDirect = [...new Set((value.directTagIds ?? []).filter((tagId) => tagsById.has(tagId)))];
  const preferredRoot = value.preferredTagId && tagsById.has(value.preferredTagId)
    ? rootTagId(value.preferredTagId, tagsById)
    : undefined;
  const groupTagId = preferredRoot ??
    (validDirect.length ? rootTagId(validDirect[0], tagsById) : undefined) ??
    rootTags[0]?.id;
  if (!groupTagId) return { groupTagId: "", tagIds: [] };

  const defaultId = defaultSubtagId(groupTagId, tags);
  let tagIds = validDirect.filter((tagId) =>
    tagId !== groupTagId && rootTagId(tagId, tagsById) === groupTagId);
  if (tagIds.some((tagId) => tagId !== defaultId)) {
    tagIds = tagIds.filter((tagId) => tagId !== defaultId);
  }
  if (!tagIds.length && defaultId) tagIds = [defaultId];
  return { groupTagId, tagIds };
}
