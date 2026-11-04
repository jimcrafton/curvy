# Document sync cost: review and plan

Status 2026-11-03: partly fixed, **165 s -> about 15 s** for the stress test (goal: under 10 s, then drop `DISABLED_`). Every undoable step used to
re-sync the whole document, so the cost of one edit grew with the number of objects and a session of n edits was quadratic.

Done 2026-11-03 (all uncommitted; the jitnodes parts are in `3rdparty/jitnodes` and must be carried to `D:\code\jitnodes`):
- jitnodes `Network::topologyRevision()` (process-unique), a bounded touch log (`noteTouched`, `touchedSince(watermark)`, `touchRevision()`),
  `dependentsOf(ids)`, `consumersOf(id)`. Wiring changes are logged as touches of the nodes at both ends; `clear()` raises the log floor.
- `ScopeViews` tables are kept in `SketchDocument::scopeTables_` and rebuilt only when a scope's topology revision changes.
- `pullPathNodes` asks each scope which nodes changed since the last pass (touch log + dependents) and skips objects whose nodes and own stamp
  (`PullStamp`: geometry revision, node, bound flag, matrix) are unchanged; a touched subnet node makes every scope count as changed.
- `pullAppearanceNodes` only looks at shapes whose look chain was touched, and invalidates just those `appearanceChains_` entries (the wholesale
  clear in `graphChanged` is gone). Walking up from a touched look node passes the transform node to find the shape.
- `GraphDocument::snapshotGraph()` reuses the previous snapshot's text (it serialized all nodes each time: about 29 s of the redo and undo phases).
- `syncPathScope`: new shape nodes get their transform directly, restored ones find it through `consumersOf`, removals prune through the cache, and
  an unchanged scope (same topology, same objects and nodes) skips binding, pruning and placement. Redo of an add syncs once (`Batch`).

Left (about 15 s): every add or remove still changes the topology, so `syncPathScope`, `syncAppearanceNodes` and `pullLayerNodes`-style passes do
O(objects) work per step (about 1 to 2 ms at 1000 objects, roughly 3000 such steps in the test). To get under 10 s: an incremental path for "one
object added/removed" in `syncPathScope`, the same for `syncPaintOrder`, and no second `graphChanged` sync in the delete undo.

## What was measured

`ShapeNodeObject.DISABLED_AThousandMixedShapesEditedThenUndoneAndRedoneComeBackExactly` (tests/test_sketch_model.cpp) draws 1000 parametric
shapes, edits about 460 of them, undoes and redoes the edits, undoes everything, redoes everything. It passes and takes **about 165 s**. The 48 shape
version of the same test takes 0.3 s. Instrumenting the run at 1000 objects (about 4000 nodes):

| Where | Time over the first ~2250 `graphChanged()` calls | Per call |
| --- | --- | --- |
| `pullPathNodes()` (called by `graphChanged()`) | 50 s | 22 ms |
| `changed()` (sync of layers, groups, path nodes, paint order) | 17 s | 7.5 ms |
| of which `syncPathNodes()` | about 90% of `changed()` | 6.5 ms |
| of which `syncPaintOrder()` | about 4% of `changed()` | 0.3 ms |
| `refreshLiveShapes()` | 1 s | 0.5 ms |
| `snapshotNetwork` in `commitGraphEdit` | 0.4 s in total | negligible |
| `sameGraph` in `commitGraphEdit` | about 12 ms in total | negligible |

`sameGraph` is not a problem: snapshots share their text by `shared_ptr`, so the comparison takes the pointer-equality fast path (`sameText`,
`sameChild`). A content hash would not buy anything there. Snapshotting is cheap for the same reason (`snapshotNetwork` reuses the previous text when
`Node::revision()` is unchanged).

## Why it is linear per step

`graphChanged()` (documentlive.cpp) does `placedCache_.clear(); outlineCache_.clear(); appearanceChains_.clear();` and then runs three passes that
each look at **every** object and **every** node, although one edit touched one object:

1. **`pullPathNodes()`**, the biggest cost. For each of the n objects:
   - `ScopeViews` is built once per call: a copy of `nodeIds()`, an `unordered_set` of all ids, and `placementNodes()` (a string compare of each
     node's type name plus a `shapeBehind` walk). That is O(nodes) with a large constant, rebuilt on every call.
   - per object: `nodeTypeFits` (string compares), `drivenPlace` (walks the wiring), `drawnGeometry` (`net.cook` for a shape node),
     `worldOf` (evaluates the transform chain), then `back = object->geometry(); back.transform(inverse)`, `geometryFromPath(back)` (allocates) and
     `sameCurveShape(drawn, back)` (compares every point). Roughly 20 µs to 50 µs per object, times 1000 objects, for every edit.
2. **`syncPathScope()`** (called from `changed()` through `syncPathNodes()`): `net.nodeIds()`, an `existing` set, a `used` set, a loop over every
   node with a type-name string compare to find orphan shape nodes, `upstreamShapes` for every object, `cached()` for every object, and a final loop
   that decides per object whether anything must be written. The graph topology did not change in nearly all of these calls.
3. **`syncAppearanceNodes()` and `pullAppearanceNodes()`**: `appearanceChains_.clear()` in `graphChanged()` defeats the
   `cached->second.items == items` early-out, so every object's chain is recomputed each time (`appearanceChain`, `appearanceSynced_` lookups).

Everything above is repeated for each of the thousands of steps in the test (each add, each edit, each undo, each redo).

## Findings from the review (beyond the measurements)

- `ScopeViews` is created separately in `pullPathNodes`, `pullAppearanceNodes`, `syncAppearanceNodes`, `refreshLiveShapes` and `syncPathScope`
  (via `placementNodes`), so the same O(nodes) tables are built four or five times per edit.
- Caches are invalidated wholesale (`placedCache_`, `outlineCache_`, `appearanceChains_`) instead of per object.
- There is no notion of "what changed": `SketchObject::boundToNodes()` marks objects edited since their last sync, but nothing marks nodes edited
  since the last pull, even though `jitnodes::Node::revision()` already provides exactly that (a global counter bumped by every `touch()`).
- The jitnodes `Network` has no topology revision, so a pass cannot tell cheaply that no node was added, removed or rewired.
- `Network::nodeIds()` returns a fresh vector each time; `slots_` is probably a map, so this is also O(nodes) with allocation.
- Not a cause, but related: node id reservation (`SketchDocument::reservedIds_`, this session) is O(log n) per created node and does not touch this.

## Plan

Goal: the cost of an edit depends on what the edit touched, not on the document size.

1. **Topology revision in jitnodes.** Add `Network::topologyRevision()` (a counter bumped in `addNodeWithId`, `removeNode`, `connect`,
   `disconnect`, `replaceNode`, `clear`, and snapshot restore). Cheap, local, and testable in jitnodes.
2. **One cached view per scope.** Replace the per-call `ScopeViews` by a document-owned cache keyed by scope and the scope network's
   `topologyRevision()`: `existing`, `placed`, `next` are rebuilt only when the revision changed. Then `placementNodes` and the id sets stop being
   per-edit costs.
3. **Per object "seen" stamps in `pullPathNodes`.** Keep for each object the revisions it was last reconciled at: its shape node, its placement
   node and the placement nodes of its parents (the input 1 chain), plus `geometryRevision()`. An object whose stamps are unchanged is skipped
   without cooking, copying or comparing anything. An edit then costs the objects whose nodes changed.
4. **Early-out in `syncPathScope`.** When the topology revision and an `objectsRevision_` (bumped by add, attach, detach, parent change) are
   unchanged, skip pruning, wiring and the `feeding` computation and run only the final write loop for the stale objects (`!boundToNodes()`).
5. **Targeted appearance invalidation.** In `graphChanged()` drop only the `appearanceChains_` entries of objects whose look nodes changed
   (same revision stamps) instead of clearing the map.
6. **Bulk operations.** `Batch` already defers `changed()`; use it in the undo and redo of multi-object steps and check that `graphChanged()` honours
   it (today it calls `pullPathNodes` directly before `changed()`).

Steps 1 and 2 are mechanical and low risk. Step 3 is the one that changes behaviour (what counts as "unchanged"), so it needs the most tests: the
parent chain, driven objects (move points and animated nodes), frozen and bypassed path nodes, text and symbol objects, and objects in group and
layer subnets (ids are per network; a stamp must include the scope).

## Acceptance

- `ShapeNodeObject.DISABLED_AThousandMixedShapes...` passes with `--gtest_also_run_disabled_tests` in **under 10 s**, then loses its `DISABLED_` prefix.
- A scaling test: time for one parametric edit at 1000 objects is at most about 3 times the time at 100 objects.
- The existing suites stay green: ctest, `curvy --check-workspace`, the three perf tests (`AThousandsObjectDocumentStaysResponsive` and the other
  two), the GUI suite.
- No `invalid node id` or wrong outline when undoing and redoing across adds, deletes, groups and layers (`ReservedNodeIds.*`,
  `ShapeNodeObject.*`).
