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

/**
 * Aggregate the full group, not the currently searched/filtered card list.
 * A missing result is pending, not a failure or an implicit pass. Hidden cases
 * never turn a group red or prevent its active cases from turning green.
 *
 * @param {Array<{ id: string, hidden?: boolean }>} tests
 * @param {Record<string, { pass: boolean } | undefined>} results
 */
export function summarizeTestResults(tests, results) {
  let passed = 0, failed = 0, pending = 0, hidden = 0;
  for (const test of tests) {
    if (test.hidden === true) { hidden++; continue; }
    const result = results[test.id];
    if (!result) pending++;
    else if (result.pass) passed++;
    else failed++;
  }
  const active = passed + failed + pending;
  const state = failed ? "fail" : pending ? "pending" : active ? "pass" : "empty";
  return { state, active, passed, failed, pending, hidden };
}
