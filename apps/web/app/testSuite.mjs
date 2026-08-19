/**
 * Remove one test and choose the nearest surviving test when the active test
 * is deleted. The last test is deliberately retained so the editor always has
 * an editable room.
 *
 * @template {{ folderId: string, id: string }} T
 * @param {T[]} tests
 * @param {string} testId
 * @param {string} activeId
 * @returns {{ activeId: string, removed: T | null, tests: T[] }}
 */
export function deleteTestCase(tests, testId, activeId) {
  const removedIndex = tests.findIndex((test) => test.id === testId);
  if (removedIndex < 0 || tests.length <= 1) {
    return { activeId, removed: null, tests };
  }

  const removed = tests[removedIndex];
  const remaining = tests.filter((test) => test.id !== testId);
  if (activeId !== testId) {
    return { activeId, removed, tests: remaining };
  }

  const originalFolderTests = tests.filter((test) => test.folderId === removed.folderId);
  const folderIndex = originalFolderTests.findIndex((test) => test.id === testId);
  const remainingFolderTests = remaining.filter((test) => test.folderId === removed.folderId);
  const replacement =
    remainingFolderTests[folderIndex] ??
    remainingFolderTests[folderIndex - 1] ??
    remaining[removedIndex] ??
    remaining[removedIndex - 1] ??
    remaining[0];

  return { activeId: replacement.id, removed, tests: remaining };
}

/**
 * Hidden cases remain authored project data, but are intentionally outside
 * every physics-engine gate until the author makes them visible again.
 *
 * @template {{ hidden?: boolean }} T
 * @param {T[]} tests
 * @returns {T[]}
 */
export function runnableTestCases(tests) {
  return tests.filter((test) => test.hidden !== true);
}
