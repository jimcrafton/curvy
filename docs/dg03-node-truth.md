# DG-03: the nodes are the only truth for an object

Goal: a `SketchObject` stops being a second copy of what the graph holds. Every edit of an object's outline, placement or paint frame is
an edit of its nodes; the object (path, matrix, bounds) is a cache that the document refreshes from them. Undo of such an edit restores
the graph, not the object. Decided 2026-10-25 (option 3): do it now, before more code depends on the object being authoritative.

## Where things stand (2026-10-25)

- An object holds `path_` = the **placed** outline (the shape node's curve carried by the transform node's matrix; `placedOutline()` computes the
  same thing from the nodes), `paintMatrix_` (the accumulated matrix, mirrored in the transform node as translate/rotate/scale/shear/pivot),
  `frameAngle_`, `paintWarp_`, and for text/symbol/image the words, font, matrix, style, warp, definition (already mirrored as string
  parameters of the shape node).
- Edits change the object first (`setGeometry`, `transform`, `warp`, the text setters). `geometryRevision_` counts them; `writePathNodes()` /
  `syncPathNodes()` copy a stale object into its nodes at the next `changed()`; `pullPathNodes()` copies a node newer than its object back.
- Undo of a geometry edit is a `GeometryState` per object (path, angle, text matrix, paint matrix, warp) written back into the objects, then
  pushed to the nodes. Graph edits (the graph dock) undo through `GraphSnapshot`.
- Readers (`geometry()`, `bounds()`, painting, export, hit tests) read the object. Selection bounds/frame and picking already read the nodes
  (stage 3 of this task, done).

## Measured (spike, 2026-10-25, 2000 objects / 8000 nodes)

- Node-first move: `storeTransform` on every placement node, 2.5 ms per mouse move; cooking all 2000 placement nodes afterwards 6.6 ms
  (paid only for what is drawn). A move does not rewrite curves; only vertex edits do.
- Frame + bounds: 0.5 ms (0.15 ms mid-drag). Timeline step: 1.5 ms.

## Design

1. **The node holds everything that is the object's.** `curvyTransform` gets `paintWarp` (text, 9 numbers); the matrix is already there.
   `frameAngle` does **not** go in the node. A probe (2026-10-25) shows it equals the node's `rotation` (modulo 180 degrees, which is the same
   box) until the matrix gains shear, e.g. a world-axis resize of a rotated object; then the decomposed rotation drifts (3.3 degrees in the
   probe) from the frame the user is working in, and the matrix cannot give it back. It is the orientation of the selection handles, state of
   the same kind as `groupAngle_` and `quadFrame_`, so it stays outside the graph and travels in the undo step next to the graph snapshot
   (`FrameState` grows a per-object angle list).
2. **Readers keep reading the object.** `geometry()` etc. stay as they are, so painting, export and the 300+ tests do not change. What
   changes is who writes: the object's path is only ever set by the document, from the nodes (`refreshObject`).
3. **Edits are document operations on nodes.**
   - `placeObjects(baseline, matrix)`: for a drag, each object's placement = baseline placement x matrix, written with `storeTransform`
     (one `setFloat` per changed number), the object refreshed from the node. Move, resize, rotate, shear, flip, align, nudge, the panels and
     transform-again all become this.
   - `setObjectCurve(id, drawnPath)`: vertex edits (direct select, pen, shape tools, path operations, cutting, warp) write the shape node's
     `curve`, keeping point attributes (`geometryFromPath(..., keepAttributes)`), then refresh.
   - Text / symbol / image: the setters write the node's string parameters; the node is the thing that rebuilds the outline.
4. **Undo restores the nodes, not the whole graph.** Measured in S2 (2000 objects, 8000 nodes): `snapshotGraph()` 135 ms, the snapshot after
   the edit 138 ms, restore 248 ms; far too slow for every drag. So a placement step keeps a small per-object state (the numbers the
   transform node holds, plus the text strings) and undo/redo write those back through the same funnel as the edit (`commitPlacement`:
   4 ms to commit, 5 ms to undo at 2000 objects). S3 gives curve edits the same kind of step (the shape node's `curve` handle per touched
   node, shared not copied). Group and frame state (`groupAngle_`, `quadFrame_`, frame angles) that is not in the graph stays in the step.
   The graph dock keeps `GraphSnapshot`: its edits are single nodes and its graphs are not touched by every mouse move.
5. **Refresh.** `graphChanged()` already pulls; it becomes the only way an object's path changes. Remove `geometryRevision_`, `nodeRevision_`,
   `writePathNodes`, the object-wins branch of `pullPathNodes`, and `GeometryState` as an undo payload. `SketchObject::setGeometry`,
   `transform`, `warp` and the text setters become private to the document (or to a `refresh` entry point); objects outside any document
   (clones, clipboard, import before `add`) are built from a prototype and the document makes the nodes on `add`.

## Stages (each one builds, passes the tests and the checks before the next)

- **S1 Node data.** Add `paintWarp` to the transform node; write it with the matrix; pull it back. Round-trip through the graph text and
  undo of a graph edit. Behaviour unchanged. (Tests: node round-trip, loading an older graph without it.)
- **S2 Placement edits node-first. Done.** `SketchDocument::placeObjects` is the one place a placement is written: the transform node (and a
  text object's strings) first, then the object from the same numbers, with the outline cache seeded so the frame does not re-cook 2000
  nodes per mouse move (that was 10 ms; now 0.25 ms; the node write itself is 3.8 ms for 2000 objects). Used by the canvas drag
  (`applyToSelection`), the transform tools, `transformObjects`, the Transform panel and hold-scrub (`previewTransform`), align,
  distribute, rotate 90, flip, rotate by, Transform Again. `commitPlacement` is their undo step (see Design 4). Warp is still a curve
  edit (S3). Test: `Placement.AMoveWritesTheTransformNodeAndLeavesTheObjectInStep`; the large-graph test budgets the drag step.
- **S3 Curve edits node-first. Done for the outline edits (2026-10-25).** `SketchDocument::setObjectOutline(object, placedPath)` sets the
  outline and rewrites the shape node's curve at once (point ids and tags kept); `syncObject(object)` does the same after a warp.
  Used by direct select (drag and delete anchor), the live shape handles, reverse, simplify, join, redefine symbol and text warp; the undo/redo of
  `commitGeometry` calls `syncObject` per object instead of the bulk `writePathNodes()`. Objects with no placement node yet or driven by
  a chain are still written by the next `changed()`. Left for S4: the pen/shape/text creation paths and the text, symbol and image setters
  still rely on `syncPathNodes()` at `changed()`; they are found by making the mutators private. Test:
  `Placement.ACurveEditWritesTheShapeNodeAndKeepsTheTags`.
- **S4 Remove the old direction. Part a done (2026-10-25).** The mutators that change an object's outline, placement or text
  (`setGeometry`, `transform`, `warp`, `setPaintMatrix/Warp`, `setTextMatrix/Text/FontName/FontSize/TextState/TextStyle/TextWarp/TextAll`,
  `setSymbol`, `setImage`) are private to `SketchObject`; `SketchDocument` is the friend. An object not in a document yet (import, clone) is built
  with `SketchObject::Prototype`, which the document also uses to refresh an object from its nodes. New document calls replace the
  direct edits: `warpObjects`, `setObjectText`/`textStateOf`, and `changeSelectionText` now edits a `TextState` value instead of the
  object. Tests that moved or retexted a live object go through `placeObjects` / `setObjectOutline` / `setObjectText`. `writePathNodes`
  is gone.
  **Part b moves to S5:** `syncPathNodes()` is not only a stale-writer: it creates a node for each new object, removes the nodes of deleted
  ones, wires parent links and binds a loaded file's nodes to its objects (`nodeRevision() == 0` means "just loaded"). The revisions and
  `bindLoadedNode` exist for that load path, so they go when a file stores the graph and objects are built from nodes (S5). Until then
  `GeometryState` stays as the small per-object undo payload (Design 4).
- **S5 Files. Done for `.skb` (2026-10-25).** Version 4 adds a `GRPH` chunk: the graph text and each object's shape node id. An object whose
  outline is only its node's curve under its placement (`SketchDocument::pathHeldByNode`: not text, not chain-driven, in step with its node)
  is written without an outline; on load the graph is read first (into a scratch network, so a bad graph refuses the file before the
  document is touched) and the outline is built from the node's curve under the object's saved placement. Text keeps its saved outline (the
  fonts of the machine that wrote it), and objects the graph does not hold keep theirs. Versions 1 to 3 still load, and the nodes are made
  from the objects as before. What it costs: a node's curve holds cubics only, so a quadratic comes back as the equal cubic, and outlines come
  back to within a float step; the file grows by the graph (about 450 bytes per object before gzip).
  **Not removed in S5:** the revision counters, `syncPathNodes` and `bindLoadedNode` (S6 below).
- **S6 The two revision counters become one state. Done (2026-10-25).** `nodeRevision` is gone: an object is either bound to its nodes
  (`boundToNodes()`: the nodes hold what it is) or not, and every edit of its outline clears that itself, so an edit that forgets to write
  the nodes is still found and written by the next `changed()`, as before. `everBound()` tells a new or loaded object (whose nodes, if it has
  any, are the file's) from one edited since. `geometryRevision` stays only as the version of the outline cache. Behaviour is unchanged.
  **Kept on purpose:** `syncPathNodes` and `bindLoadedNode`. Making `add` build each node chain on its own would be quadratic inside
  a batch (an import of 2000 objects) or need the binder rewritten per object, and it would give up the self-healing above; and SVG files
  (and `.skb` before version 4) still have to match their outlines to the nodes they embed, because the outline is what other viewers show.
  `GeometryState` stays as the per-object undo payload (Design 4).

## Risks

- Anything that edits an object directly today and was never routed through the document: found by making the mutators private (S4).
- Driven objects (move points, expressions, keys on a placement): the object shows what the chain cooks; their outline is never written back,
  as now.
- Undo granularity and snapshot size on big graphs (measured in S2).
- Text: its matrix and warp are already node strings; the outline rebuild must happen from the node (S3).
