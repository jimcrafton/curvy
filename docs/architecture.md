# Curvy: architecture, code flow and hot paths

State of 2026-11-03. This describes how the code is organised and how one edit travels from a tool to the screen, where time is spent, and where
the next improvements are. `design.md` has the research notes and the phased plan, `sync-performance-review.md` the DG-12 measurements,
`dg04-layers-subnets.md` the layer/group design.

## 1. The big picture

Curvy is a node-based 2D vector editor with two views of one artwork that are kept in step:

- **Objects** (`SketchObject`, in `SketchDocument::objects_`): what the canvas draws and the tools edit. A path with its look, placement, name, flags.
- **Nodes** (a `jitnodes::Network`, the "graph"): the storage model. Every object has a shape node (`curvyPath`, or `curvyShape` for a parametric
  shape), a transform (placement) node after it, and a chain of look nodes (`curvyFill`, `curvyStroke`, `curvyEffect`). Groups and layers are
  `curvySubnet` nodes holding their members' chains in a child network.

The graph is the document (it is what is saved and what undo restores); the objects are the working copy the UI is fast against. Most of the
code is the reconciliation between the two, run after every change (section 4).

A **scope** is one network: scope 0 is the root, every subnet (group, layer) has its own child network and scope number. Node ids are only unique
per network, so a node is always addressed as `NodeRef(scope, id)`.

## 2. Areas of the code

| Area | Files | What it does |
| --- | --- | --- |
| Entry, windows | `main.cpp`, `sketchwindow.*`, `workspacecontroller.*`, `dockcontroller.*`, `dockcontext.*` | Starts newui, builds the window from `Resources/*.newui`, binds views to the model, docks and workspace rings. |
| Model | `sketchdocument.*`, `sketchobject.*`, `document*.cpp` (layers, paths, selection, transform, clip, symbols, trace, ...) | The artwork: objects, selection, artboards, layers/groups, undo. `SketchDocument` derives `GraphDocument`. |
| Graph document | `graphdocument.*` | Owns the `jitnodes::Network`; graph undo (`commitGraphEdit`), snapshots, scopes (`NodeRef`). |
| Object <-> node sync | `documentlive.cpp`, `pathnode.*`, `shapenode.*`, `appearancenode.*`, `liveshape.*` | The node types and the code that mirrors objects into nodes and pulls graph edits back. The hot area. |
| Canvas and tools | `canvasview.*`, `canvastools.cpp`, `shapetools.cpp`, `transformtools.cpp`, `cuttools.cpp`, `texttool.cpp`, `artboardtool.cpp`, `guides.cpp` | Input handling and overlays; each tool turns mouse/pen events into document calls. |
| Panels | `stylepanel.*`, `shapepanel.*`, `textpanel.*`, `layerspanel.*`, `graphdock.*`, `timelinebar.*`, ... | Property docks and the node graph editor; read the model and call document methods. |
| Files and export | `sketchbin.cpp` (.skb), `io/svg/sketchsvg.cpp`, `io/pdf/` (PDF / AI import and export, fonts, encryption), `io/eps/epsexport.cpp`, `sketchexport.cpp` | Serialisation and output. |
| Geometry | `pathops.cpp`, `pathflatten.cpp`, `textoutline.cpp`, `imagetrace.cpp` | Boolean ops, flattening, text to outlines, tracing. |
| Checks | `sketchchecks.cpp`, `workspacechecks.cpp` | `--check-tools` and `--check-workspace` headless self-tests. |
| External | `3rdparty/newui` (UI toolkit, Blend2D drawing), `3rdparty/jitnodes` (node engine) | newui draws everything with Blend2D; jitnodes cooks nodes (asmjit kernels for point-wise work). |

Pattern: model (`SketchDocument`) / views (`CanvasView`, panels) / controllers (`SketchWindowController`, `SketchCommandController`,
`SketchDocumentController`), using newui's `Model::updateAllViews()` and delegates (`onChanged`, `onSelectionChanged`).

## 3. Flow: drawing a shape to the screen

Example: the user drags out a star with the shape tool.

1. **Input.** newui delivers mouse events to `CanvasView`. The active tool (`shapetools.cpp`) tracks the drag and shows a preview in
   `paintShapePreview` (an overlay; the document is untouched until the mouse is released).
2. **Create.** On release `CanvasView::addDrawn` (`canvastools.cpp`) gives the new `SketchObject` the document's default look and calls
   `SketchDocument::addParametric` (a star/polygon/rounded rectangle/arc is parametric) or `addUndoable`.
3. **Add as one undo step.** `addUndoable` (`sketchdocument.cpp`) assigns an id, pushes an undo entry, and `attach`es the object into `objects_`.
   Undo of the add takes a graph snapshot first, so redo can put back nodes the sync pruned.
4. **`changed()`** (`sketchdocument.cpp`) is the one funnel every mutation ends in. In order: `pullLayerNodes`, `pullPaintOrder`, `syncLayers`,
   `syncGroupScopes`, **`syncPathNodes`**, `pruneGroupScopes`, `putBaseLayer`, `syncPaintOrder`, `markModified`, **`updateAllViews`**,
   `onChanged`. A `Batch` defers it so a multi-object step syncs once.
5. **Object -> node sync** (`syncPathNodes` -> `syncPathScope` -> `syncAppearanceNodes`, `documentlive.cpp`). For each scope:
   - bind: an object without a node gets a `curvyPath` or `curvyShape` (`addNode`, ids reserved per owner so undo entries stay valid);
   - place: the shape gets a transform node (`addOwnedNode`), look nodes are chained after it;
   - write: stale objects (`!boundToNodes()`) write their outline, matrix and paint warp into the nodes (`writeNode`, `storeTransform`).
6. **Views update.** `updateAllViews` makes `CanvasView` repaint (the view registered itself with `addView`); the status/size fields and docks
   refresh through `onChanged` and `onSelectionChanged` handlers in `SketchWindowController`.
7. **Paint.** newui calls `CanvasView::paint(BLContext&)` (`canvasview.cpp`): pasteboard, artboards, grid, then `SketchDocument::paint` ->
   `paintRange` (walks `objects_` in stacking order, opening group layers for opacity/blend/clip) -> `SketchObject::paint` -> `paintItems`
   (each fill/stroke of the object's appearance, bottom to top; objects with pixel effects go through an offscreen layer), then
   `paintOverlay` (selection frame, handles, tool overlays) and rulers. **The canvas draws the objects, not the graph**; the graph is
   not cooked to render.

### A graph edit (the reverse direction)

A node edited in the graph dock or by a drag (`setGraphParam`, `setGraphFloatLive`, `commitGraphEdit`) changes the network and ends in
`graphChanged()`:

`pullPathNodes` (adopt the node-side shape into the objects when the node changed) -> `pullAppearanceNodes` -> `refreshLiveShapes` ->
`changed()`. The rule between the two sides: an object edited since its last sync (`!boundToNodes()`) wins; otherwise the node wins. A hand
edit of a parametric shape inserts a frozen `curvyPath` after the `curvyShape` (`freezeParametric`), so the shape stays editable as a node.

### Undo

Two kinds of step: object steps (closures over a `Slot`, restoring objects) and graph steps (`commitGraphEdit` stores before/after
`GraphSnapshot`s, restored with `restoreSnapshot`, then `graphChanged`). Add and delete also store a graph snapshot because the sync
prunes the node chains of objects that leave.

## 4. Change tracking (what makes sync cheap)

Added for DG-12 (jitnodes plus curvy):

- `Network::topologyRevision()`: a process-unique number bumped when a node is added, removed, replaced, or an input is wired or unwired.
- Touch log: every `Node::touch()` (any saved change) and every wiring change is recorded as `(revision, id)` in a bounded log per network.
  `touchedSince(watermark)` returns the ids touched after a revision, or false when the log no longer reaches back (the caller then looks at
  everything). `dependentsOf(ids)` follows wiring and expression references downstream; `consumersOf(id)` is the direct reverse wiring.
- Each pass keeps its own watermark per scope and only visits what was touched:
  `pullPathNodes` (`PullScope`/`PullStamp`), `pullAppearanceNodes` (`appearanceScopes_`, per-shape invalidation of `appearanceChains_`),
  `drivenPlace` (incremental `drivenPlaces_`), `syncPathScope` (`PathSync`: settled objects skip binding, pruning and placement).
- `snapshotGraph()` reuses the previous snapshot's serialised text for nodes whose `revision()` is unchanged.
- `ScopeTables` (`scopeTables_`): per scope the node id set, shape -> placement map and appearance successors, rebuilt only when
  `topologyRevision()` changes.

Invariants to keep when adding code: any code that changes wiring or node data must go through `Network` (so it is logged); a table
derived from the graph must either be keyed by `topologyRevision()` or fed from the touch log; never cache by node id alone across undo, since
ids come back (reserved ids) with different content (use `Node::revision()`).

## 5. Hot paths

Measured with the 1000-shape stress test (`ShapeNodeObject.AThousandMixedShapesEditedThenUndoneAndRedoneComeBackExactly`, 165 s before the
DG-12 work, about 9 s after). Costs per step at 1000 objects, roughly:

| Path | Cost now | Why |
| --- | --- | --- |
| `snapshotGraph()` | about 0.9 ms | Builds a `NodeSnapshot` (strings, vectors, map node) for every node and then copies the whole snapshot into `lastSnapshot_`; O(nodes) even when nothing changed. Called by every add/delete undo and redo and by each graph edit. |
| `syncPathScope` per add/remove | about 0.2 ms | Settled objects are skipped, but each step still builds `objectsNow`, `prevNodeOf`, `touchedNow`, the `Bound` list and the saved `PathSync` (all O(objects) allocations). |
| `pullPathNodes` | about 0.6 ms | Per-object stamp (6 matrix doubles, hash lookups) plus `ScopeViews` rebuild (`views.rebuild`, about 0.3 ms) on every topology change: `nodeIds()`, an `unordered_set`, `placementNodes` (string compare per node). |
| `syncAppearanceNodes` | about 0.2 ms | Per object a `std::map` lookup and a deep compare of the `AppearanceItem` vector against the cached chain. |
| `syncPaintOrder` | about 0.15 ms | Rewrites the paint-merge order every call. |
| `detach`/`attach` | about 0.5 ms per call, mostly the `changed()` they end in | `objects_` is a vector of `unique_ptr`, so erase/insert is O(n) (not measured separately; small next to the sync). |
| `placeObjects` (a drag of many objects) | about 4 to 5 ms for 2000 | Writes the transform node of every moved object per mouse move (`writeObjectNodes`), each touching the log. Budget test: `Selection.TheFrameOfALargeGraphIsCheapToMeasureWhileScrubbing` (limit 6 ms; it is the closest test to its limit). |
| Painting | not measured here | `paintRange` visits every object per repaint; objects with pixel effects, opacity or blend allocate an offscreen layer per object per frame. |

What is **not** hot: `restoreSnapshot` (0.5 ms), `sameGraph` (pointer-equal text), the canvas overlay code, the graph editor.

## 6. Where to improve next

In order of expected gain for a large document:

1. **Make add/remove incremental end to end.** Each add or remove still costs O(objects) in several passes (table above). A change journal
   that says "object X attached at index i" would let `syncPathScope`, `syncAppearanceNodes`, `syncPaintOrder` and `pullPathNodes` handle one
   object instead of re-deriving state for all of them. The touch log already carries the node side; the object side (attach, detach, reorder,
   look edit) needs an equivalent: a per-object revision (`geometryRevision()` exists; add `appearanceRevision()` and use it instead of
   comparing item vectors).
2. **Snapshots without a full pass.** Make `GraphSnapshot` share per-node entries (`shared_ptr<const NodeSnapshot>`) so an unchanged node
   costs a pointer copy, and have `snapshotNetwork` take only the touched nodes (from the touch log) plus the previous snapshot. That
   removes the second copy into `lastSnapshot_` and most of the 0.9 ms.
3. **Maintain `ScopeTables` incrementally** from the touch log instead of rebuilding them on every topology change, and give `Node` an integer
   type id so "is this a transform node" is not a string compare (`typeName() == kTransformNodeType` appears in most loops).
4. **Object storage.** If profiling shows it matters: a stable-index container (or a lazily compacted vector with tombstones) for `objects_`,
   so undo of an add/delete does not shift thousands of `unique_ptr`s.
5. **Paint.** Cull by `paintBounds()` against the visible rectangle before `object.paint` (the viewport clip is set, but each object still
   walks its appearance); cache a rasterised tile for objects with effects/opacity while nothing moves; skip invisible and fully clipped
   groups early. Measure first: add a frame-time benchmark for a 1000-object document at several zooms.
6. **Drag path.** Moving many objects writes each transform node on every mouse move. During a drag, defer node writes and write once on
   release (the object already carries the matrix); the live `placeObjects` would then touch objects only.
7. **Contention and threads.** The cook already runs on jitnodes' thread pool; sync and paint are single-threaded. Splitting paint (static
   background layer cached while only the overlay changes) is the cheapest parallelism win. Note `snapshotGraph` and the touch log are not
   thread-safe (UI thread only).
8. **Tooling.** Use the `x64-profile` preset (Release flags `/O2 /Ob2` plus `/Zi /bigobj`, PDBs next to the exes):
   `cmake --preset x64-profile && cmake --build --preset x64-profile` from a VS developer prompt, then in Visual Studio open
   `out/build/x64-profile/curvyTests.exe` (File > Open > Project/Solution, or Debug > Performance Profiler > CPU Usage with that exe and
   arguments such as `--gtest_filter=ShapeNodeObject.AThousandMixed*`). DG-12 was done with temporary scoped timers because the release
   build has no PDBs; with this preset a call tree replaces them.

jitnodes and asmjit: the JIT only accelerates numeric kernels over points (cook of point-wise nodes). The remaining time in the editor
is bookkeeping (hash lookups, string compares, allocation), so more JIT would not help; the gains above are algorithmic.

## 7. Testing the performance work

- `ShapeNodeObject.AThousandMixedShapes...` (about 9 s): correctness of add, edit, freeze, undo, redo at scale. No timing assertion.
- `Selection.TheFrameOfALargeGraphIsCheapToMeasureWhileScrubbing`, `SketchPerf.*`, `AThousandsObjectDocumentStaysResponsive`: time budgets.
- `curvy --check-workspace` and `--check-tools`: headless UI drives. GUI suite (`tests/gui`): real input; run on request.
- Not written yet (DG-12 acceptance): a scaling test comparing one parametric edit at 100 and 1000 objects (at most about 3 times).
