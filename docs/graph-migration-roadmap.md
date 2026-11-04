# Graph migration: what is left (2026-11-01)

Where the migration stands: the nodes are the truth for an object's outline, placement and paint (DG-03, `dg03-node-truth.md`), and groups and layers are real
subnets (DG-04, `dg04-layers-subnets.md`, stages S1 to S5 and S4a to S4c-3 done). This file lists what is **not** migrated, why it was left, what it would
take, and a suggested order. It is a roadmap, not a commitment; every item says what finishing it buys, because several buy nothing today.

Sources: the two plan docs, `design.md` and the project notes. Items marked *unverified* come from notes written in earlier sessions and were not re-checked in the
code on 2026-11-01.

## Left on purpose (decided, nothing to do unless the reason changes)

| Item | Why it stays | Revisit when |
|---|---|---|
| `layers_` member (S4c-4) | It is the pending table: it lives between a layer edit and its sync, and `pullLayerNodes` refills it from the nodes otherwise. The loaders already replace it only through `setLayers` / `writeLayerTable`. Removing the member means adding an equivalent buffer plus a cache behind about 25 read sites, with the risk of per-edit scans that cost 8 to 10 s in three perf tests. | Never needed for correctness. |
| `syncLayers` sort of `objects_` | It runs only when the list is out of layer order (a layer edit changed the ranks, or a new object landed below the top layer). `pullPaintOrder` already takes a reorder made in a `paint` merge node first. | With S4d below. |
| `frameAngle_`, `groupAngle_`, `quadFrame_` outside the graph | They are the orientation of the selection handles, not geometry; a probe showed the node's rotation drifts from the frame once the matrix gains shear (DG-03 design 1). They travel in the undo step beside the graph snapshot. | If handle orientation ever needs to be saved per object in the graph. |
| `syncPathNodes`, `bindLoadedNode`, `GeometryState` | A per-object node build inside `add` is quadratic in a batch; SVG files and `.skb` before version 4 still match outlines to nodes; `GeometryState` is the small undo payload (a full graph snapshot costs 135 ms at 2000 objects). DG-03 S6. | A file format that stores only the graph. |

## Open items, by area

### A. Layers and groups (DG-04 residue)

1. **S4d: the graph owns cross-layer stacking.** Put layer ranks into the `paint` merge ordering so a layer reorder rewrites the merge nodes and `objects_` follows; the
   `syncLayers` sort goes. Touches `layerRanks`, `syncPaintOrder` and the layer-reorder edits in `documentlayers.cpp`. Buys: reordering a layer by dragging its subnet in
   a `paint` merge in the node view. Already true without it: layer order is saved in the nodes (`layerOrder`). Risk: moderate; rerun the three perf tests
   (`AnEditCostsLittleWhenManyObjectsAreGrouped`, `AThousandsObjectDocumentStaysResponsive`, `TheFrameOfALargeGraphIsCheapToMeasureWhileScrubbing`) and the GUI suite.
2. **Dragging a node into another scope's merge is not read as a move.** The merge reorder within a scope is read (S4b part 6); across scopes it is not. Buys: regrouping
   or changing layer from the node view. Needs: `pullPaintOrder` to compare scope membership, then the same move the sync already does for group changes.
3. **Parent links across groups are dropped by the sync**, and **wiring from outside a group is cut when its member moves.** Buys: parent/child between objects in different
   groups or layers. Needs: a cross-scope link representation in jitnodes (a subnet input/output, or a `NodeRef` input), which is a jitnodes change, not only curvy.
4. **Live shapes (boolean and similar) stay in the root whatever their group or layer.** Buys: a boolean result that belongs to a layer. Needs: moving the whole input
   chain with it, which the sync does for plain shapes but not for chains with several inputs.
5. **`groups_` / `groupProps_` / `SketchObject::layer()` are still request fields**, kept for clones, loaders and unplaced objects, which have no document to ask.
   Buys: one source of truth. Needs: objects always created through a document (a `Prototype` route exists, DG-03 S4a) and the loaders placing straight into scopes.
6. **An empty layer has no subnet** (*unverified*; written at S4b part 5, later parts may have changed it). Check `pruneGroupScopes` before relying on it.
7. **Entered-group view polish:** the Flow view of an entered group draws no different background; the canvas scoping covers picking and selection, not painting.

### B. Shape parameters as nodes (new question, 2026-11-01)

The star, polygon, rounded rectangle and arc are plain outlines recognised on demand (`recogniseLiveShape`, `liveshape.h`); nothing about them is stored. The canvas
handles and the Shape box both rewrite the outline. They are therefore **not** parameter nodes the way a boolean is. Decision needed: keep recognition (works for any file,
loses the shape on a freehand edit, no animation of sides or radius) or add real `curvyStar` / `curvyPolygon` / `curvyRoundRect` / `curvyArc` nodes (animatable, driveable
by expressions, shown in the graph; costs a node per shape and a migration for existing files, and "convert to path" becomes an operation). Buys: keyed corner radius,
expressions on points. Not started; no plan covers it.

**Draft plan (2026-11-02, for your decision; nothing is built).** Direction: parametric shape nodes, with recognition kept as a bridge for old shapes. Reason: today the
layout of the points lives only in the tool code. `CanvasView` calls `starPoints` / `regularPolygonPoints` once and stores the result as a plain `curvyPath`; the node
never knows what it was, so a file or script can only write the raw points, and a one-point nudge ends the "star" (`recogniseLiveShape` stops matching).

*Design*

1. **One node type, `curvyShape`, with a `kind` parameter** (`polygon`, `star`, `roundRect`, `arc`) rather than four node types. The kinds share placement and the
   "cook the outline from numbers" step, and one type means one `isShapeNodeType` entry and one place for `shapeBehind` to look. Parameters (all keyable, all valid
   expression targets): `points` (sides or tips), `outer`, `innerRatio` (star), `radius` (roundRect corner), `width` / `height` (roundRect), `start` / `sweep` (arc),
   `angle`. **As built in S1 (2026-11-02):** the centre *is* a pair of parameters (`centerX`, `centerY`), not left to the `curvyTransform`, so a recognised path converts
   with its transform node untouched (existing objects keep their outline in document coordinates and an identity placement). Angles are in degrees; `angle` is the
   first corner of a polygon or star and the turn of a rounded rectangle about its own middle; `outer` is also an arc's radius; `kind` is a string.
2. **The node cooks the same functions the tool uses today** (`starPoints`, `regularPolygonPoints`, `roundRectPath`, `arcPath` in `shapetools.h`), turned into a
   `curve` geometry by `geometryFromPath`. The generator then exists once, in the node; the tool and the Shape box stop calling it directly.
3. **Same output contract as `curvyPath`:** a `curve` geometry, so `curvyTag`, `curvyMovePoints`, the transform and the appearance nodes work unchanged, and an object
   with a tag or move-points node after a `curvyShape` is *driven* exactly as one after a `curvyPath` is.
4. **Point ids:** `geometryFromPath` keeps ids when the layout is unchanged, so animating `outer` or `radius` keeps tags valid; changing `points` renumbers, which tags by
   id cannot survive. Document that; tag by `every` / `box` instead.
5. **Editing a point never destroys the shape; it adds a path node after it** (decided 2026-11-02). `curvyPath` gains an *optional input* (`hasVariableInputs`, as
   `curvyTransform` has for its parent link): `curvyShape -> curvyPath -> curvyTransform`. Dragging a point of a `curvyShape` object (or Convert to path) inserts a
   `curvyPath` wired to the shape and frozen on a snapshot of the shape's current cooked points (the data is the node's *frozen geometry*, not its `curve`
   parameter, which stays empty while an input is wired), and the edit rewrites that frozen geometry. The `curvyShape` stays in the
   graph with its parameters, so going back is unfreezing or deleting the path node. With no input the path node behaves exactly as today, so nothing existing changes.
   Details to settle in S1:
   * **Snapshot, not follow.** The path node's frozen geometry is the output (the baked, hand-edited points); the input records where they came from. So changing the
     star's `points` later does not reach the edited outline (it could not: the point count no longer matches). A parameter that *can* follow (move a point, keep the
     rest parametric) is `curvyMovePoints`, which already exists; use that when the user wants the edit to stay relative to the shape.
   * **Freeze (a jitnodes feature, decided 2026-11-02).** jitnodes has no generic way to stop a node being cooked (checked 2026-11-02): the nearest pieces are a geometry
     parameter as authored data (`node.h:86`), `needsInputs()` (`node.h:137`, false = inputs are not cooked) and the `null` pass-through. Add a **freeze flag to `Node`**,
     for every node type, not only `curvyPath`: *freezing* cooks the node once (its inputs included), keeps the result as the node's frozen geometry, and from then on
     the node returns it and nothing upstream of it is planned or cooked for it; *unfreezing* discards the stored geometry and the node cooks normally again. Useful
     beyond curvy (an expensive simulation or a boolean chain you are done with). For curvy, a point drag on a parametric shape freezes the new `curvyPath` after it
     is wired to the shape, with the edit applied to the frozen points; "go back" is unfreezing, which re-reads the shape.
     Design points for S0, none built:
     * **Saved or memory only?** Memory only would make a reload recook the upstream and lose a hand edit, so for the point-drag case the frozen geometry has to be
       saved with the node (like a geometry parameter, so it takes part in undo snapshots and copies). A pure cache you can throw away is a different flag.
       **Decided 2026-11-02: freeze always saves its geometry inline in the file**, the convenient default. Houdini shows the cost: frozen data makes very large files, and
       people dump it to disk with a file export node. So the way out is a later, separate step, not part of S0: write a frozen node's geometry to a file (jitnodes has
       `geoFile.cpp` and `fileNodes.cpp` to build on) and have the node read that file instead of holding the data. Until then, freeze only what is small (a hand-edited
       shape is); the node view could show the frozen size so a large one is noticed.
     * **Editing frozen data:** the frozen geometry is read-only through the graph; a point edit on a frozen `curvyPath` rewrites it (the node exposes it as its `curve`).
     * **Planning:** the cook planner must treat a frozen node as having no inputs (as `needsInputs()` false does) and its subtree as not required, and freezing or
       unfreezing is a structural `markDirty`. *Unverified:* how `Network` plans fused point-wise chains across a frozen node; test in S0.
     * **Bypass, also a `Node` flag (decided 2026-11-02; jitnodes has none, checked 2026-11-02).** A bypassed node is skipped: it returns its first input unchanged
       and does no work of its own, and the rest of the chain cooks as normal. It is the opposite of freeze (freeze cuts what is *upstream*, bypass removes the node
       itself) and is saved with the node. For a node with no input, bypass gives an empty geometry. Needs the same planner and fusion care as freeze: a bypassed
       point-wise node must drop out of a fused kernel chain, and toggling it is a structural `markDirty`. If both are set, bypass wins (the node does nothing, so
       there is nothing to freeze); the node view shows both as toggles. In curvy it gives "turn the live shape's edit off" for any node (a move-points, a tag, an
       appearance node), not only `curvyPath`.
     * **`curvyPath` optional input:** with the freeze flag, an unfrozen `curvyPath` with an input passes it through live and a frozen one outputs its own data, so no
       separate `locked` or `bypass` parameter is needed.
   * **Both positions in shape space.** The path node sits before the placement node, like `curvyMovePoints`, so the edit is in the shape's own space and the star's
     move, rotate and scale still apply to the edited outline.
   * **Reset.** "Reset to shape" re-snapshots from the input (one undo step).
   * **Point ids.** The snapshot is made with `geometryFromPath(..., keepAttributes = shape geometry)` so tags by id made on the shape keep working.

*How the pieces drive it*

* **Tools:** Star, Polygon, Rounded rectangle and Ellipse/Arc tools create a `curvyShape` object with parameters from the drag (radius, sides, inner ratio), instead of
  `makePolygon`. Their live preview keeps using the generators directly for speed.
* **Canvas handles and Shape box:** `recogniseLiveShape` becomes "read the node's parameters" for a `curvyShape` object (exact, no tolerance guessing) and stays as the
  recogniser for plain paths. Writing a value sets the node parameter inside one undo step, as the box does now with a rewritten outline. `LiveShape` keeps its fields so
  `shapepanel.cpp` and the handle code change little.
* **Old shapes:** no forced migration. A recognised plain path gets "Make parametric" in the Shape box (replace its `curvyPath` by a `curvyShape` with the recognised
  numbers, same outline, one undo step). Files saved before stay plain paths until you choose it; new shapes are parametric.
* **Animation and expressions:** the parameters appear in the dock and the node view like any node's, so keys and `time` / `frame` expressions work with no extra code.
  Inner ratio and corner radius are the first useful targets.

*Stages (each leaves the tree green; rerun ctest, `--check-workspace`, the three perf tests and the GUI suite)*

**S3 is done (2026-11-02, uncommitted):** the star, polygon, rounded rectangle and arc tools add `curvyShape` objects (`SketchDocument::addParametric`); the canvas
handles and the Shape box write the node's parameters (`setParametricOutline`) as one graph undo step; Object > Live Shape > Make Parametric; a hand edit puts a
frozen `curvyPath` after the shape (`freezeParametric`: the path node keeps the object's node id, a copy of the shape goes upstream, and `writeObjectNodes` does it
automatically for any outline edit the parameters cannot hold). Derived nodes now come back under their old id after an object leaves and returns
(`reservedIds_`), and add and delete undo restore the graph. Left: S4, and the document sync cost found on the way, which is the next priority
(`docs/sync-performance-review.md`, tracker DG-12).

**S2 is done (2026-11-02, uncommitted):** an object's `graphNode` may be a `curvyShape` (a fresh parametric object) or a `curvyPath` fed by one. `drawnGeometry` in
`documentlive.cpp` is what the mirror reads (the cook for a shape, a frozen, bypassed or fed path node; `curve` for a plain one); `writeNode` writes an edited outline to
`frozenGeometry()` of a frozen path node and leaves a bare `curvyShape` to its parameters (the object takes the node's outline back). Shapes feeding an object's path node are
not pruned and travel with it when it moves to another group. `SketchDocument::makeParametric(id)` swaps a plain path node for a `curvyShape` with the same id and wires, so
undo and redo find the object's node again. 4 `ShapeNodeObject.*` tests. Not done: an outline edit on a bare `curvyShape` object is reverted by the next pull (S3 inserts the
frozen path node on a point drag); SVG export already writes the cooked outline and import gives plain paths. **For S3:** the tools still create plain paths; `canvastools.cpp`
handles and `ShapePanel` still write via `setObjectOutline`.

**S1 is done (2026-11-02, uncommitted):** `curvyShape` (`include/curvy/shapenode.h`, `src/shapenode.cpp`; it cooks `liveShapePath`, so the generator is shared with the
canvas handles and the Shape box), `kShapeNodeType` in `isShapeNodeType`, and the optional input on `curvyPath` only (text, symbol and image nodes keep none; a path
node now shows one input slot). `shapeNodeLive` / `storeShapeNode` read and write the node from a recognised `LiveShape` (the bridge S3 uses for "Make parametric"). 8 tests
`ShapeNode.*` (each kind against the generators, recognised shapes stored and cooked, keyed parameters and save/load, point ids, `shapeBehind` through a path node, a frozen path node
keeping an edit across an upstream change and a save/load, a path node with no input unchanged); ctest 2113 pass, `--check-workspace` 0 failures. GUI suite 63/63 pass. **For S2:** `documentlive.cpp` prunes shape nodes that no object uses (`used`, around line 1299), so a
`curvyShape` that sits *upstream* of an object's `curvyPath` must count as used; and the object mirror must read a frozen path node's geometry (`frozenGeometry()`),
not its `curve` parameter. Original S1 text:

1. **S1 node:** `curvyShape` registered in `pathnode.cpp` (or a new `shapenode.cpp`), cooks all four kinds, unit tests for each kind against the existing generators
   (same points), id stability under a parameter change, and a save/load round trip. Also the optional input on `curvyPath` (item 5), with tests: no input behaves as
   before, frozen gives the node's own data and does not cook the input, unfrozen with an input gives the input, a file with the old one-node chain still loads.
   **S0 is done (2026-11-02, uncommitted in `D:\code\jitnodes`):** `Node::setBypassed`, `Node::setFrozenGeometry` and `Network::freezeNode`, saved in snapshots, files and paste,
   with 7 tests (jitnodesTests 194 pass, stress quick 0 failed); see `docs/architecture.md` there. Findings: `fileCache` already exists for writing frozen data to
   disk. Not done: the node view toggles, and `executionOrder` still lists the nodes upstream of a frozen node. Original S0 text:
   **S0 comes first, in jitnodes:** the freeze and bypass flags (item 5), with their own tests there (freeze cooks once, a frozen subtree is not cooked, unfreeze
   recooks, both survive save/load and undo, a frozen or bypassed node in a fused chain, a bypassed node passes its first input through). No UI uses it yet.
   Check `shapeBehind` and `isShapeNodeType`: they must see through `curvyShape -> curvyPath` to the placement node.
2. **S2 object mirror:** the document mirrors a `curvyShape` node into a `SketchObject` outline (the same path used for driven objects) and builds the node for an
   object created with parametric intent (`SketchObject::makeStar` style factory). Check `shapeNodeType`, `shapeNodeParams` and `bindLoadedNode` for the new type. SVG
   export writes the cooked outline; SVG and `.skb` import still give plain paths (no type to read).
3. **S3 tools:** the four drawing tools create parametric objects; handles and the Shape box read and write the parameters; `Make parametric` for recognised paths; the
   Direct Selection point drag inserting the path node (item 5). Update the `ShapeBox` GUI tests and add one for keying a corner radius.
4. **S4 node view and docs:** the parameters in the node view, `design.md` Phase 2 text, `ui-design.md`, tracker.

*Costs and risks*

* A node per shape: the 10k-object case already pays one per object; this replaces a `curvyPath`, so the count is unchanged but each cook now generates points. Measure with
  `AThousandsObjectDocumentStaysResponsive` before S3.
* A path node with an input means the document can no longer assume a shape node has no inputs: check the sync that builds and matches node chains (`syncPathNodes`,
  `bindLoadedNode`) and the mirror of "driven" objects for assumptions about a chain's first node.
* Two paths to maintain while old files exist (recogniser for plain paths, parameters for nodes). Retire the recogniser only if "Make parametric" becomes automatic on load.
* Undo: the parameters go through the graph snapshot the same way other node edits do (*unverified* for a kind change, e.g. star to polygon; test it in S1).
* Boolean and other live shapes that take a shape as input need no change: they read the `curve` output.

*Not in this plan:* spirals, grids, text on a shape, per-corner radii, a polygon with unequal sides. Each would be a new `kind` once this exists.

### C. Scale

1. **`pathsnode` (packed paths).** One node holding many paths, members selectable and editable but not wired and not steppable, with pack/unpack. Houdini's packed
   geometry. Answers the 10k-object case, where one node per object is the cost. Noted in `dg04-layers-subnets.md` ("Idea: pathsnode"); explicitly not part of DG-04.
2. **Big-file load is 27 s** and the three perf tests have thin margins (`AThousandsObjectDocumentStaysResponsive` measured 1.23 s against a 2 s limit in a full run).
   Moves cost about 0.25 ms per object per edit; steady state is about 1.5 us per object. Any stage above that adds per-edit work must re-measure.
3. **Never scan all nodes per `changed()`;** walk the scope table. (Lesson from S4b part 7.)

### D. Housekeeping that blocks regeneration

* Done 2026-11-02: the Text box is in `docs/mockups/propertyDock.html` and `Resources/propertyDock.newui` is regenerated from it (text button icons are set in
  `DockController`). `docs/mockups/preview.css` is a browser-only stylesheet linked from the fragment mockups; the converter skips it.
* `docs/design.md` Phase 2 still describes plain paths beside graph-backed live shapes; that is the old plan (`ui-design.md` says the same). Update it to the DG-03 / DG-04 model.
* Flaky or timing-sensitive in the full run: `PathNode.AThousandsObjectDocumentStaysResponsive`, `ClipboardManager` (pass alone).

## Suggested order

1. **Housekeeping D** (small, removes a trap).
2. **B decision** (no code until decided; it shapes whether A4 and the Shape box change).
3. **A2** (cross-scope merge move) only when the node view needs to regroup; it unlocks real graph editing of structure.
4. **A1 / S4d** after A2, since both change how the paint merge is read.
5. **C1 `pathsnode`** when a real document or import needs more than a few thousand objects.
6. **A3, A4, A5** as the need appears; A3 needs jitnodes work first.

Nothing here blocks the other roadmap items (GD-03 key diamonds, GD-06 follow-ups, animation, booleans).
