# Shapes in the geometry (jitnodes)

Design, not yet built. Edits go in `3rdparty/jitnodes` and are carried to `D:\code\jitnodes` afterwards.

## Why

`Geometry` has points and primitives (`{start, count, closed, kind}`). A primitive is one polyline or one cubic path. Three things need a
level above it:

1. **Compound shapes.** A letter "O" is two primitives (ring and counter); an SVG compound path is several. Drawn one primitive at a time
   the hole fills in. Fill rule, stroke and selection belong to the whole shape.
2. **Selection.** A click on a letter should pick the letter, and the node that made it, not a ring.
3. **Animation.** A bone, a rig, a deformer or a keyed transform has to say *which shape* it drives, and that answer has to hold when the
   graph is re-cooked, when a parameter is animated, when a layer is renamed or reordered and when the document is saved and opened.
   A primitive's index in the cooked geometry is none of these: it shifts whenever anything upstream adds or removes a primitive.

So a shape is a **group of consecutive primitives with a stable key**.

## Decision: the shape is a reserved primitive attribute, `shape.key`

Not a separate table of ranges. The reason is in `topology.cpp`: every operation that rebuilds geometry (`flattenCurves`, `resample`,
`subdivide`, `blast`, `extractPrimitives`, `copyToPoints`, `symmetry`, the shape nodes) carries per-primitive data through one function,
`transferPrimAttribs(src, dst, srcIndex)`; `Geometry::append` and `concat` carry it by copying attribute columns. A table of ranges would
need its own code at every one of those sites and would go stale silently when one forgets. A primitive attribute rides all of them
unchanged, shares storage copy-on-write, merges its string table in `concat`, and is saved by the code that already saves attributes.

The shape *concept* is still first class: `Geometry` gets a small API over the attribute (below), so renderers, hit tests, exporters and
rig nodes never read the attribute by hand.

- Type: string attribute, primitive class, name `shape.key` (the `shape.` prefix is reserved, as `group.` is).
- A **shape** is a maximal run of consecutive primitives with the same non-empty key.
- The empty key (string id 0, what zero-fill gives) is **anonymous**: each such primitive is a shape of its own. So all existing nodes and
  geometry keep working with no change, and `concat` of parts that have no keys stays correct.
- The attribute is created only when something stamps a key. Geometry without shapes pays nothing.

## API on `Geometry`

```cpp
struct ShapeRange { uint32_t first; uint32_t count; std::string_view key; };   // key empty = anonymous

void addShapePrimitive(const Primitive& p, const std::string& key);   // adds p and stamps the key; same key again = same shape
void setShapeKey(size_t prim, const std::string& key);
const std::string& shapeKey(size_t prim) const;                       // "" when anonymous or no attribute
std::vector<ShapeRange> shapes() const;                               // one pass over the key column; no attribute: one range per primitive
const ShapeRange* findShape(const std::vector<ShapeRange>&, std::string_view key) const;
std::vector<uint32_t> shapeOfPrimitive() const;                       // shape ordinal per primitive, for nodes that want shape-level behaviour
void checkShapes() const;                                             // debug: a key must not come back after another key's run
```

`shapes()` is O(primitives) over a 4-byte column. Callers that need it more than once keep the result.

## Identity: what makes a key stable

Rules for a key, all of which exist for the animation case:

1. **Built from graph structure only**, never from a parameter value or from the position in the current geometry. Animating a radius,
   a count or a time does not change a key. `<node uid>/<local name>`: a circle is `<uid>`, glyph 5 of a text node is `<uid>#g5`, the
   third subpath of a path node is `<uid>#s2`.
2. **Unique in a cooked geometry.** Nodes that make copies (`copyToPoints`, `symmetry`, symbol instances) derive each instance's key from
   the source key and the copy number (`key@3`). Without this, two adjacent copies with the same key would merge into one shape.
   `checkShapes()` finds the non-adjacent form of that mistake.
3. **Hierarchical.** An object (layer or group) prefixes the keys that pass out of it with its own uid: `layerUid/groupUid/circleUid`.
   A prefix selects a whole subtree, so a rig can bind to "everything in this group" as one rule.
4. **Survives structure edits.** The key uses a persistent per-node `uid`, not the node id. A node id is per network and a group
   gets a new id when `SketchDocument::moveGroup` moves it (see its header); a key built from ids would silently unbind every rig on
   the group the moment the user drags it in the layers panel. The uid is a **per-document counter**, written in base 36 (3 to 5 characters
   per key level), saved with the node. A uid only has to be unique inside its document, so a 128-bit UUID would buy nothing. This needs
   one small jitnodes addition: a `uid` on `Node`, assigned from the document's counter at creation, saved and loaded, and given a fresh
   value whenever a node is pasted or imported from another document.
5. **Not an index into anything**, so inserting a shape before another does not renumber the rest.

### Who stamps keys

Only nodes that produce *document shapes*: the jitnodes generators `circle`, `rect`, `line`, `path` and the file importers, and curvy's
`curvyText`. They call `addShapePrimitive` with a key from the cook context (`CookContext::shapeKey(local)`, built from the node uid
and the object prefix, which the owning subnet supplies). Particles, patterns, trails and other bulk geometry stay anonymous: stamping a
key on 100 000 primitives would cost far more than it is worth, and nobody binds to one particle.

## Contract for operations

| Operation | What happens to shapes |
|---|---|
| `append`, `concat` | The key column is copied like any string column (tables merged, ids remapped). Parts without keys are zero-filled, so anonymous. |
| `transferPrimAttribs` (flatten, resample, subdivide, shape nodes) | Keys follow the primitive. |
| `blast`, `extractPrimitives` | Keys follow the primitives that remain. A shape that loses its outer ring keeps its key on what is left, so identity holds. |
| `copyToPoints`, `symmetry` | Derive `key@copy` for each copy. These are the two sites that must change. |
| Transform, point-wise and fused kernels | Untouched: they never read primitives. |
| Nodes that build from scratch with `addPrimitive` | Anonymous, as today. |
| Anything that reorders primitives (sort, shuffle) | May split a shape; keys stay correct, so only compound fills are affected. Documented, not guarded. |

## Animation and rigging

A rig is a node that takes geometry and a list of *bindings*, each a key or a key prefix, and acts on the shapes it finds.

- **Resolving.** Each cook, the rig calls `shapes()` once, finds each binding's shape(s), and gets the point range of every primitive in
  them. A binding that finds nothing is reported (`Node::warning`) and skipped; the rest still work. This is the "hand-built or edited
  graph" case.
- **Per-shape channels** live next to the key as ordinary primitive attributes: `shape.pivot.x/y` (the rest pivot a rotation turns
  around), later `shape.bone` (a bone key) and weights. They ride the same operations for free.
- **Bones.** A bone node outputs a transform per bone key. A skin node applies it to the points of the shapes bound to that bone. Points
  of a shape are exactly the point ranges of its primitives, which the `shapes()` ranges give.
- **Keyed transforms.** A transform restricted to a shape (`group` or key selector) keys its parameters as today; only the selection is
  new.
- **Timeline UI.** The graph and panels can list animated shapes by key, show the object they belong to (key prefix to object uid)
  and select them on the canvas by the same key.

## Curvy side

- The canvas renderer iterates `shapes()`: one path per shape, all its primitives added, filled with the first primitive's `fill.evenOdd`
  rule (default non-zero) and stroked once. Holes work.
- Hit test per shape. A click yields a key; the key's node uid resolves to the node in the graph, replacing the `src.path` idea. The
  document keeps a `uid -> NodeRef` index, rebuilt when the graph changes (`onGraphChanged`).
- `SelectionModel` gets `selectShapeKey(key)` and the reverse, so canvas and graph view stay in step.
- Not touched now: curvy's PDF/SVG/AI/EPS import and export, and jitnodes' `svgExport.cpp` (it could write real compound `<path>` data
  from `shapes()` later; the user's import/export branch is separate).

## Cost

- Memory: nothing when no key is stamped. Stamped: 4 bytes per primitive plus one string per shape in the table (about 20 bytes for a
  typical key). A document of 10 000 shapes is about 250 KB. If that ever matters the key can be a 64-bit hash in two int columns.
- Time: `concat` and `append` handle one more string column, with the remap they already do. `shapes()` is one pass over primitives per
  cook of the display geometry. Hit test and render both already walk primitives.

## Checks

1. Done: `geoFile.cpp` and `serialize.cpp` both save and load string primitive attributes (`primStrings` in the graph file); the geometry
   file round trip is tested (`shapeKeysSurviveTheGeometryFile`). The graph-file path (a keyed geometry stored as a node's geometry
   parameter) is read from the code, not yet run.
2. Done: a string column survives `transferPrimAttribs` (`extractPrimitives`, `blast`, `flattenCurves` tested), `append` and `concat`
   (the ids are remapped when the tables differ), and copy-on-write (`editingACopyOfAGeometryLeavesTheOriginalKeys`).
3. Open: the cook framework can hand a node its uid and prefix without a signature change that touches every node (a field on `CookContext`).

## Build order

1. Done 2026-11-14: Geometry API (`shapes`, `shapeOfPrimitive`, `findShape`, `addShapePrimitive`, `setShapeKey`, `shapeKey`, `hasShapeKeys`,
   `checkShapes`; `ShapeRange`, `shapeKeyAttrib`) in `geometry.h` / `geometry.cpp`, 9 tests in `jitnodes/tests/testMain.cpp` (run
   `jitnodesTests.exe`, 222 pass). Not built yet: copies get distinct keys (step 3). Until then a keyed geometry through `copyToPoints`
   or `symmetry` would merge adjacent copies into one shape; nothing stamps keys yet, so nothing is affected.
2. Done 2026-11-14: `uid` on `Node` (`uid()`, `uidText()`, `setUid()`): taken from a counter on the root network (`Network::takeUid`,
   `reserveUid`; reset when the root is cleared), saved as `uid` in the standard body, so undo snapshots and files keep it; `pasteNodes`
   gives every pasted node, and every node inside a pasted subnet, a fresh one. A file from before uids gets fresh ones on load. Asset
   instances take their children from the definition, so keys made inside one are not stable across loads (not handled).
3. Done 2026-11-14: `CookContext::shapeKey(local)` = subnet uids (`Network::keyPrefix`, outermost first, each followed by `/`) + node uid
   (+ `#local`). Every subnet contributes, not only objects, so no owner has to supply a prefix. Stamped by jitnodes `circle` and `line`;
   there is no jitnodes `rect` or `path` node yet, and curvy's shape nodes and the importers are still to do. `copyToPoints` and the
   symmetry node (`mirror`) give each copy `key@N`. Tests: 5 in `jitnodes/tests/testMain.cpp` (227 pass).
4. Done 2026-11-14: curvy side, 10 tests in `tests/test_shapes.cpp` (curvyTests 279). `include/curvy/shapepaths.h`: `shapePath` (one path
   per shape) and `shapeAt` (topmost shape under a point: inside its fill, or within a tolerance of its stroke; the shape takes its first
   primitive's look). `CanvasView` paints one path per shape (holes work) and has `shapeAt(viewPoint, tolerancePx)`.
   `GraphDocument::nodeOfUid`, `nodeForShapeKey`, `shapeKeyOf`, `uidsOfShapeKey`: the uid index is rebuilt when a lookup finds it stale, so it
   needs no hook on graph changes. `SelectionModel::selectShapeKey(key, additive)` (a key that names nothing clears) and
   `isShapeKeySelected(key)` (true when the generator or any subnet around it is selected). Wired the same day (5 tests in
   `tests/test_canvasselect.cpp`, curvyTests 284): a left press on the canvas calls `WorkspaceController::selectAt`, which selects the
   generator node of the shape under the pointer in the graph view's selection model (so the graph view follows), Shift or Ctrl adds, a press
   on empty canvas clears, Space held still pans. Selected shapes (and the shapes inside a selected layer or group) get a 2 px accent outline
   from `paintTop`, repainted from the model's `onChanged`. Shapes without a key (anonymous) cannot be selected.
5. `curvyText` emits glyph shapes with counters as continuation primitives of one key.
6. First rig node on top of the API, to prove the binding story before more is built.

## Decisions (agreed 2026-11-14)

- **uid, not node id**, as a per-document counter in base 36, reassigned on paste or import from another document.
- **Keys are stamped only by shape generators**; bulk geometry stays anonymous.
- **Keys are text**, with the storage hidden behind `shapes()` / `shapeKey()` so it can change later. A hash was considered and left out
  for now: a hash of the whole path cannot be searched by prefix, which the rig binding to a group relies on, and with counter uids a
  key is under 20 bytes (about 250 KB for 10 000 shapes). Revisit only if measurements on very large documents show the string table
  is a problem; the likely form is two int columns plus a separate parent column to keep prefix binding.
