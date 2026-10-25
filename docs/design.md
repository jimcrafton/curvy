# curvy design notes

## Status
- `src/` and `include/curvy/` are the sketch demo (newui-demos/demos/sketch) ported as-is: namespace `curvy`, flat file names kept
  (`sketchdocument.h`, ...), `Resources/curvy.newui` is the old sketch layout.
- `3rdparty/newui` and `3rdparty/jitnodes` are git submodules.

## Build notes
- The project is C++17 throughout (newui's `delegate.h` does not compile as C++20). jitnodes upstream is C++20 but only used
  `std::bit_cast`; it builds as C++17 once that is replaced (`jitnodes/core/bitCast.h`).
- jitnodes' CMakeLists is not used: Blend2D embeds asmjit under a private ABI namespace, so jitnodes gets its own asmjit static build.
- reflectgen needs `3rdparty/newui/tools/reflectgen/.venv` (`python -m venv .venv`, `pip install -r requirements.txt`).

## Phases
1. Port the sketch demo and build it (done).
2. Use a jitnodes `Network` directly from the document code; cook artwork objects from it.
3. Map the document model (paths, shapes, strokes) onto graph nodes; hide the graph in the default UI, expose it in advanced mode.
4. Skeletal strokes (see `Skel Strokes.docx`).
5. Replace the sketch UI with new mockups converted through the `html-mockup-to-newui` skill.

## Phase 2: GraphDocument
`GraphDocument : newui::Document` owns a `jitnodes::Network`; `SketchDocument` derives from `GraphDocument`. The graph layer knows
nothing of sketch types (paths, layers, artboards), so it can be unit tested alone.

- **One undo stack.** `UndoStack` (newui) is moved into `GraphDocument` and shared. jitnodes' own `History` is not used: it is a
  second linear stack and could not interleave with sketch edits. Graph edits instead use
  `snapshotNetwork(net, &previous)` before and after the edit and push `{label, restoreSnapshot(after), restoreSnapshot(before)}`.
  Snapshots share the text of unchanged nodes and restore touches only the nodes that differ, so cooked caches survive undo.
- **Edit API (all one undo step each):** `addGraphNode`, `removeGraphNode`, `connectGraphNodes`, `setGraphParam`, `renameGraphNode`.
  A drag edits live and commits once, like `commitGeometry`.
- **Cooking.** `GraphDocument::cook(NodeId)` wraps `Network::cook`; `changed()` marks affected nodes dirty and repaints.
- **File format.** The document stays SVG. `saveNetwork()` text is embedded in a dedicated element (namespaced, ignored by other
  SVG readers); `GraphDocument` exposes `graphToText()` / `graphFromText()` and the SVG reader/writer call them. A load goes
  through `Batch`, and a failed `loadNetwork` leaves the old graph intact.
- **Phase 2 slice.** A graph-driven "live shape": a `SketchObject` holds a node id; its outline is the cooked polyline of that node
  (circle / star with parameters), drawn on the canvas, undoable. Geometry from jitnodes is polyline-only, so a `bezierPath` node
  with in/out handle attributes is a later addition.
- **Status (2026-10-18).** Implemented: `GraphDocument` (`graphdocument.h/.cpp`), `SketchDocument` derives from it and the undo stack lives there;
  live shapes in `documentlive.cpp` (`addLiveShape`, `setLiveShapeParam`; `SketchObject::graphNode()`). The graph is saved as
  `<metadata><jitnodes:graph data="..."/></metadata>` (the reader skips `metadata`, and the XML parser keeps only attributes, so the text is an
  attribute), and each live object carries `data-graph-node`. Each document's `Network` has one thread (`Network(1)`), since scratch documents are
  created freely. A live shape's outline is rebuilt from the node on every graph change, so moving or transforming one directly is overwritten;
  placement belongs in a transform node (next slice). Duplicating a live shape yields a plain polygon.
- **Direction change (2026-10-18, not yet implemented).** The graph is the document: every path becomes a node (a `bezierPath`-style node holding
  its points), and the object list, layers, groups and artboards become views over the graph (layers and groups as subnets). `SketchObject` then
  becomes a cached view of a node rather than the primary store, and the slice above (live shapes beside plain paths) is the stepping stone. The UI
  consequences are in `docs/ui-design.md` ("The graph is the document", "Document structure views"). **Decided: path nodes need a `bezierPath` node type, or an
  extension of the geometry model, and one of them will be added** (jitnodes geometry is polyline-only today). Still needs a plan for the migration
  and for selection and hit testing by node.
- **Path nodes (2026-10-24, first step of the direction change).** Every Line/Rect/RoundRect/Ellipse/Polygon/Polyline/Path object has a `curvyPath` node
  (`pathnode.h/.cpp`, registered by `GraphDocument`) whose string parameter `d` holds its curves (M/L/Q/C/Z, six significant digits - what the SVG and
  .skb files keep); its cook is the flattened polyline, so other nodes can read it. For now the object is still edited directly and the node is a
  **mirror**: `SketchDocument::syncPathNodes()` (from `changed()`) creates missing nodes, rewrites those whose object's `geometryRevision()` moved,
  and removes orphans, so every edit, undo, delete, duplicate and load is covered without touching the `setGeometry` call sites. A restored object
  gets its old node id back. Live shapes keep their own node and are skipped. Files without a graph get nodes on load.
  **Authority (2026-10-24):** `commitGeometry` writes the node in the same step as the object (`writePathNodes`), and a graph edit, undo or redo
  that changes a node's text is adopted by the object (`pullPathNodes`, via `GraphDocument::setGraphString`) unless the object was edited since its
  node was written - so a freshly loaded file (object newer) keeps the SVG's curves. Live drag previews still touch the object first; the node
  catches up at commit. Text, symbol and image objects have `curvyText`, `curvySymbol` and `curvyImage` nodes: `d` is the outline, plus the words,
  font, size, placement, style and warp (text), the definition id (symbol) or the picture name (image). A text node's words are adopted back
  into the object; which symbol or picture an object shows is mirrored only, since definitions and pixels live in the document. Still to do: layers
  and groups as subnets, selection and hit-testing by node, storing the graph in .skb.
- **Appearance as nodes (2026-10-24).** Each entry of an object's appearance stack is a node chained after its shape node, bottom to top: `curvyFill`
  (paint, opacity, blend, visible), `curvyStroke` (the same plus weight, cap, join, miter, align, arrows, brush, dashes and width profile) and `curvyEffect`
  (kind, amount, tint, softness, offsets, radius). They pass the shape through and carry the look as parameters: every number is a float parameter, a
  colour is its colour space plus four components plus alpha, and the gradient stops, point-gradient points, dashes and width points are small geometry tables (one point per row).
  `AppearanceParams` (include/curvy/appearancenode.h) is the plain-value form of one node; two looks are the same exactly when their params are equal.
  Authority works as for curves: `SketchDocument` keeps what each node held when it and its object last agreed (`appearanceSynced_`), so an object edited
  since wins and writes the node, and a node edited since (graph edit, undo, redo) wins and `itemFrom` changes only the fields that differ, leaving the
  rest at full double precision. A number that went through the graph is the node's float. Which items an object has is read from the graph (the chain
  of appearance nodes after its shape node), so loading an SVG needs no extra attributes; `appearanceChains_` makes an unchanged object cost nothing
  per edit. Not yet: patterns and brushes are held by id (looked up in the document's libraries), colour swatches are not nodes, a Fill node cannot be
  shared by several objects, and the Appearance panel still edits the object (which writes the node). An embedded graph is about 760 bytes per object
  now (jitnodes' compact save leaves out parameters that still have their default).
- **Bezier curves in jitnodes (2026-10-24, both steps done).** `jitnodes::Primitive` has a `kind` (`polyline`, `cubic`). A cubic primitive holds
  SVG-order control points (anchor, out handle, in handle, anchor ...; open 3n+1 points, closed 3n ending on the first anchor), so handles are ordinary
  points: transform, wrangle and noise nodes move them with their anchors and a point attribute on an anchor (pressure, tilt, width) is just a point
  attribute. `hasCurves`/`flattenCurves(tolerance)` in `geo/topology.h`; resample, subdivide and blast flatten curves first; a `flatten` node;
  `geometryToSvg` writes cubics as paths; copyToPoints/append/concat keep the kind. **Native storage:** a node can hold a geometry parameter
  (`Node::declareGeometry`, `setGeometryParam`, `geometryParam`; `sameGeometry`), saved with the node (`"geometries"`: points, primitives, attribute
  arrays as `{name, values}` entries because the JSON5 reader rejects keys like "n.x"), restored by undo snapshots, clips and asset updates like any
  other parameter. `curvyPath`, `curvyText`, `curvySymbol` and `curvyImage` hold their outline in the geometry parameter `curve` (a figure of straight
  lines is a polyline, any other a cubic; quads are raised exactly) and cook it as it is; the `d` text is gone, `pathToText`/`pathFromText` remain for
  Copy/Paste path data and the text warp. Coordinates are float32 (the cooked precision). `geometryFromPath`/`pathFromGeometry` convert; objects are
  only rebuilt from a node whose curve differs from theirs by more than 1e-5 relative, and attributes on anchors stay while a move or undo keeps the layout
  (`geometryFromPath(path, keepAttributes)`). `GraphDocument::setGraphGeometry` is the undoable edit. After loading a file the object and its node
  may differ in the last digit a file holds; they are treated as in agreement within 1e-4. **Costs and gaps:** an embedded graph is much bigger than
  the path text it replaced (SVG metadata now holds every node's arrays, written compact); `.skb` still stores no graph (the nodes are made from the
  objects on load, so attributes on anchors are not kept there) because the duplicate would swell the format - fixed when objects stop holding a
  second copy of their curves. Files saved with the earlier `d` text nodes fail to load their graph and get nodes made from their objects. The
  jitnodes edit is in the 3rdparty/jitnodes working tree: **commit and push it, then bump the submodule.** Next: layers and groups as subnets,
  selection and hit-testing by node, the node view (the graph tree mockup), objects as pure views of their nodes.
- **Transform node (2026-10-24).** Each shape node is followed by a `curvyTransform` node (six float parameters a b c d e f, an SVG matrix), wired between the shape and the
  object's appearance nodes; its cook is the shape's geometry moved by the matrix. The shape node now holds the outline **as drawn**, and the matrix is the sum of every
  `SketchObject::transform()` since (move, rotate, scale, shear, flip; a warp is not a matrix and is not in it). Paints are laid out on the outline before the matrix
  (`paintFrameBounds()`) and carried by it (`paintToPage()`), so gradients, point gradients and patterns follow all of those; the SVG writer adds the matrix to
  `gradientTransform` and PDF to the shading pattern's matrix. Authority is as for the other nodes: an object edit writes curve and matrix, a graph edit or undo that
  changes either rebuilds the outline as matrix x curve (`pullPathNodes`). Files: `data-paint-matrix` in SVG, version 3 of `.skb` (a version 2 file, or one with only a
  frame angle, gets a rotation). A graph saved before this has no transform nodes; they are added on load and the shape nodes are rewritten as drawn. Not yet: moving the matrix
  out of the object so selection and handles read it from the node.
- **Perspective warp and paints (2026-10-25).** A perspective warp bakes into the outline (control points mapped) and into `SketchObject::paintWarp()` too: a projective map
  `PaintWarp` of the plane the paints are laid out on, so a paint sits at `paintToPage() . paintWarp()`; a warp of page-to-page map H stores `A^-1 H A W`, and later moves, rotates
  and scales only grow `paintToPage()`. The canvas draws a warped gradient, point gradient or pattern flat on its own box and resamples it through the inverse map
  (`setWarpedPaintStyle`, at most 2048 px a side; strokes too). Persisted: `data-paint-warp` in SVG, a bit of the placement byte in `.skb` v3; undo restores it through
  `GeometryState`. Not yet: the SVG and PDF *pictures* of a warped gradient or pattern are still the unwarped paint (needs an image fill; the data round-trips), the paint-editing
  overlays and handles read the affine part only, and the warp is not in the transform node (it is in the shape node's curve).
- **Transform node parameters and parents (2026-10-25).** The node's parameters are translateX/Y, rotation (degrees), scaleX/Y, shear (degrees) and pivotX/Y, composed as
  translate x pivot x rotate x shear x scale x pivot^-1 (any 2D affine matrix, a flip is a negative scale; the order is fixed because it adds no reach in 2D). A new node pivots
  about the middle of its shape. The graph box follows the numbers while a shape is dragged. **Parents:** `SketchObject::parent()` and `SketchDocument::setParent` (undoable, loops
  refused). A child's transform node takes its parent's transform node as an optional input 1; each transform node's output geometry carries its page matrix as details
  `w0`..`w5`, so a child cooks as parent x own placement, and the node holds the placement *relative to the parent* (`worldTransformOf` follows the links). Transforming a parent
  (`transformObjects`, and the drag, panel, rotate 90, flip and transform-again paths through `snapshotSelection(true)`) carries its descendants; a loaded graph's links give
  objects their parents. UI: Object > Parent > "Parent to Last Selected" (the last object selected becomes the parent of the others) and "Release Parent", one undo step each
  (`parentSelection`, `releaseParent`). Not yet: align/distribute and the nudge paths do not carry children, and `.skb` stores no parent.
- **Selection and picking by node (2026-10-25).** `selectedNodes()` / `setSelectedNodes()` (placement nodes), `objectOfNode()` (shape, placement or appearance node to its object) and
  `hitTestNode()`. `placedOutline(object)` is the outline the graph makes (shape node's curve carried by `worldTransformOf` its transform node); `hitTest` and the marquee/lasso
  test that, with the object's cached bounds only ruling out far objects first. **DG-03 stage 3 (2026-10-25):** `selectionBounds` and `selectionFrame` (so the handles, the
  Transform panel and the drag origin) are measured on `placedOutline()` too (`outlineBounds`). `placedOutline` falls back to the object's own path for a driven object (its
  chain's cook lives only in the object) and for one edited since its node was written (a drag in progress). The outline is cached per object (by its geometry revision; cleared
  when the graph changes or time moves a node) and `findById` is indexed, because measuring a 2000-object selection took 10 ms; it is now 0.5 ms (0.15 ms mid-drag), and a timeline
  step on that graph went from 23 to 1.5 ms with jitnodes changes (`Network::dependsOnTime` skips plain-number parameters; `setTime` keeps a list of the nodes that have an
  expression, keys or a subnet, rebuilt after any edit that can change it, and returns whether any node was affected; `GraphDocument::timeChanged(bool graphAffected)`). Not yet: `frameAngle` and the accumulated matrix are still the object's,
  the object still holds its own path copy (the staging for removing it: make `geometry()` a cache refreshed from the node, then drop it from `.skb`), and
  `selection_` is still a list of object ids underneath (the node ids are mapped to it).
- **Point ids and tags (2026-10-25).** Every point of a shape node's curve has an int attribute `id` (`kPointIdAttrib`) that travels with it through the nodes (and into saved
  files). `geometryFromPath` numbers a new shape's points 0..n-1; when the object is edited and its points are added or removed, the points still where they were (matched in order,
  64-point lookahead) keep their `id` and every other attribute, and new points get ids above all the old ones. Research behind it: Houdini renumbers points on delete and the
  recommended practice is a stable `id` attribute; Blender loses groups wherever a node rebuilds geometry without carrying the attribute; CAD calls it the topological naming
  problem. **Tag node** `curvyTag` (Houdini's Group Create, points only): passes its input through and adds the point group `group.<name>` (jitnodes' int attribute). Selectors,
  any of which picks a point: `ids` ("3,7,10-14"), `every`/`offset` (every nth anchor), `box` ("x0 y0 x1 y1" in the shape's own space); `kind` narrows to any, anchors, handles,
  corners or smooth (a cubic's handles follow their anchor); `operation` joins with a group of that name already on the input (replace, union, subtract, intersect). A tag may sit between
  a shape and its transform (`shapeBehind` looks through it). **UI:** in the direct-select tool Ctrl-click an anchor to tag or untag it (the document's active tag, "tag1"), tagged anchors
  get an orange ring; Object > Points has Tag Picked Point, Untag Picked Point, Clear Tag and Move Tagged Points. `SketchDocument::tagPoints/clearTag/taggedIds/taggedPositions/pointIdNear`
  keep the tag node's `ids` (one undo step each). **Move Points** (`curvyMovePoints`, `addMovePoints`) is the "Group parameter" of the transform: translate, rotate, scale, shear and
  pivot (in the shape's own space) applied to the points of one group only (all when empty). It is a node of its own, not a parameter of the placement transform, because the placement
  is one rigid matrix that the object, its paints and its children all share. An object with a move-points node in its chain is **driven** (`drivenBy`, `adoptChain`): its outline is
  what the chain cooks, moving or rotating it only changes the placement matrix, and the shape node is never written from it, so drawing on a driven object snaps back. Not yet: a
  way to name or switch tags (the canvas always uses "tag1"), multi-point tagging (marquee), a Group parameter on other nodes, ids kept through a move-and-insert in one edit, and tags
  that follow ids through join, reverse and simplify.
- **Expressions (`fx`), time and the timeline (2026-10-25).** jitnodes already evaluates float parameters from expressions (`sin(time) * 50`, `frame`, `pi`, `node.param`
  references) and from keyframes at `Network::time()`; curvy now exposes it. `GraphDocument::setGraphExpression` (one undo step, returns the parser's message for a bad one) and
  `setTime` (not an undo step; `SketchDocument::timeChanged` refreshes the objects that follow it). The placement and move-points cooks read the *resolved* values
  (`resolvedTransformOf`), so an expression or key drives them; an object whose chain has an expression, keys or a move-points node (or whose parent's does) is **driven**
  (`drivenPlace`, worked out once per graph change): it shows what the chain cooks at the current time and an edit of its outline snaps back to the expression. `ExpressionField`
  (`include/curvy/expressionfield.h`) is the text field of the parameter box: a number is stored, any other text (or text after "=") becomes the expression, a lone "=" clears it, and a
  driven field shows "=expression" in the accent colour with a bar down its left edge. **Done 2026-10-25: the PropertyEditor pattern.** The Visual Designer in
  `code/cpp_codetools/extension/NativeEditControls` (PropertiesGrid) draws the value of each property row itself and only creates an editor for the row the user clicks. The graph
  dock's parameter box now works that way: `ParamGrid` (`include/curvy/paramgrid.h`) paints every row (caption, value, key diamond, colour/gradient chip, slider) and moves one
  `ExpressionField` ("graphDetailEditor") over the clicked value; the dock keeps only what a row does (`DetailRow`: apply, scrub, node, param) and fills the grid's `Row`s. Caption drag
  scrubs through `NumberScrubber` (an instance with `onBegin`/`onChange`/`onEnd`/`onUnit` delegates, shared with `NumberScrubber::attach`; the grid exposes the same as delegates too, no
  listener classes). A `Choice` row (click steps to the next option, Shift+click the previous) serves the Tag and Move Points boxes and the Paint/Spread rows. Keys: the diamond at the right of a number is clicked, or Ctrl+K in the editor, to key
  the number at the current time (hollow between keys, filled on one). The grid is not reflected, so GUI tests read a value by opening its editor (`param_value` in tests/gui).
  **Timeline bar** (`TimelineBar`, mockup `docs/mockups/timelineBar.html` -> `Resources/timelineBar.newui`): First, Previous, Play/Stop, Next, Last, a frame field, a scrubber over
  0..48 and the frame rate, along the bottom of the canvas; the View ring's Timeline slot (M) shows it (no menu item yet). The document's time is frame / fps (`Network::fps()`, 24);
  Play runs a 16 ms timer by the wall clock and loops; every move calls `SketchDocument::setTime`. Callbacks into it are guarded by a liveness flag, because the views and the model
  outlive the object while the window closes (an unguarded handler crashed `--check-workspace` at exit; the same `+=` lambdas capturing `this` in `GraphDock` and `DockController`
  have the same exposure). Not yet: `key` (keyframes in the fields), easing curves, the frame range and fps as document settings (saved), the ruler, keyed rows and dopesheet from
  `animation.html`, a key for Play, per-frame caching.
- **Risks.** Node ids must stay stable across undo (restoreSnapshot preserves them); the Network must not be edited while cooking.
