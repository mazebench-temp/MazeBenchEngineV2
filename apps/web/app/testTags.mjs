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
