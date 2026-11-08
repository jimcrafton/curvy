# The brush node cooks (design, 2026-11-08)

Status: design only, nothing built. Follows `graph-brushes.md` (the brush node's placement in the flow is done and looked at).

## Where it stands

Every stroke with a brush has one brush node after its `curvyStroke` node: `Path -> Transform -> Fill -> Stroke -> Brush -> paint`. The node holds that
stroke's values (a `curvyBrush` with the numbers of `BrushDef::kParamNames`, or an instance of a graph brush asset with its promoted floats), and an edit of it
is adopted by the stroke. **Nothing cooks it.** Painting, Expand, PDF and the thumbnails read the stroke's options (`StrokeOptions::brush`,
`graphSettings`) and call the brush code directly: `BrushDef::nibOutline`, `bendAlong`, `placements`, and `cookGraphBrush[Later]` (which builds a private
mini network of a `geometry` node and the asset, per chunk). The node in the document graph is a parameter holder with an identity.

What a node could do that the direct calls cannot: its output is geometry that other nodes can take (a Merge, a Transform, a bake), and a display node on it
shows the brush result. That is what this design adds.

## Goals

* The brush node's cook produces the brushed geometry, for every kind, with the same result as painting today. No special case by kind.
* One implementation of each brush, called by the node and by the paint pipeline; no second copy of the maths.
* The node reads what it needs from the flow: the stroke's width and pressure profile come from the stroke node it follows, not from a second copy on the brush
  node.
* Each shape has its own instance (already so). Painting stays as responsive as now (graph brushes cook on worker threads and are cached).

Not goals: SketchObject reading through to its nodes (main's tier 2, `node-only-plan.md` N6); changing the file format.

## Design

### 1. What the node reads

`Node::cook` gets `CookContext` with the cooking network and the node id. The brush node follows its stroke node, so `network->inputOf(node, 0)` is the stroke
node and its `width`, `widths` table and `brush` number are read directly. Input 0's geometry is the shape outline as the stroke passes it on (placed, in page
space). No new parameters are added to the brush node for the stroke's width: a copy would drift from the stroke node.

### 2. What the node writes

A jitnodes `Geometry`:

| Brush kind | Output |
|---|---|
| Calligraphic | one closed primitive (cubic where the outline has curves) from `nibOutline` |
| Art | the parts of `bendAlong`: one closed primitive per part, a `part` primitive attribute naming the part of the brush artwork it came from |
| Scatter | the placed copies of the artwork, same `part` attribute |
| Graph | what the asset outputs today: points with `radius`, `Cd`, `alpha`, `softness` (dots) or closed polylines with `Cd` (polygons) |

The paint of an art or scatter part (it may be a gradient or a pattern) stays in the brush's artwork; the node outputs only the shape and the `part` index, and
the painter looks the paint up by that index. A graph brush's colours already travel as point attributes.

### 3. One implementation, two callers

Move the kind switch that is today in `SketchObject::paint` (sketchobject.cpp ~1700), `documentexpand.cpp` and the PDF writer into one function:

`BrushOutput brushOutput(const BrushDef& brush, const BLPath& path, double width, const std::vector<WidthPoint>& widths, const GraphBrushSettings& settings)`

* The node's cook builds its arguments from its own floats and the stroke node and calls it, then converts the result to Geometry.
* The paint pipeline keeps calling it with the stroke's options (it has no graph in hand), so painting is unchanged and a test can assert that the node's
  output equals the painted shapes.

### 4. Graph brushes: the cook stays asynchronous

A graph brush cannot be a plain `Network::cook` of the asset instance fed by the stroke geometry: the asset expects a guide in its own units (divided by
`width / referenceWidth`, resampled, pressure in `w`, host-set floats such as `guideLength`, `strokeLength`, `guideStart`, the box floats and `settleTime`), and a
stroke is cooked in chunks on worker threads with a cache, cancel and progress. A cook inside `Network::cook` would run on the UI thread and block it (gas takes
seconds).

So the graph brush node's cook calls the existing host cook (`cookGraphBrushLater`) with the node's own settings and the stroke node's width and profile:

* cached: returns the geometry at once;
* not yet cooked: queues the chunks and returns an empty geometry with the network marked as needing another cook when the ready handler fires (the
  same handler that repaints the canvases);
* a caller that must have the result (Expand, PDF, image export, a bake) holds a `GraphBrushBlockingScope`, as today.

Cooking the asset instance by itself (a user sets it as the display node) keeps working but gives the asset's raw result on the unscaled stroke, as now; this
is documented, not hidden.

### 5. Who reads the node's output

Step by step, each shippable alone:

* **S2a** (small): `brushOutput` and the `curvyBrush` cook for calligraphic, art and scatter. Tests: node output equals `nibOutline` / `bendAlong` /
  `placements` for the same stroke, follows an edit of the stroke width and of the pressure profile, and an edit of the brush node.
* **S2b**: the graph brush node's cook through `cookGraphBrushLater`, with the not-ready handling above. Tests: cached and pending paths, a width edit recooks,
  blocking scope returns the result.
* **S2c**: Expand and the PDF writer take the node's output (bake = the node's geometry) instead of the second switch. Tests: same outlines as before.
* **Later (needs main's N6):** painting reads the node's cooked output, and `StrokeOptions::graphSettings` and the private brush copy go away because the node
  is the only holder.

## Decisions I recommend (say if you disagree)

1. **Painting keeps reading the object's options for now**, and the node's output is checked equal to it by test. Reason: `SketchObject::paint` has no graph; making
   it read nodes is main's tier 2 and large.
2. **Graph brushes cook through the existing asynchronous host cook**, not inside `Network::cook`. Reason: it would block the UI.
3. **The paint of art parts stays on the brush artwork**; the node outputs shapes plus a `part` index. Reason: a gradient or pattern is not a point attribute.
4. **Start with S2a** (built-in kinds): it is the smallest change that makes the node real, and S2b and S2c reuse its function.

## Risks

* Cooking a stroke node from the document network for 10 000 objects: the node cooks only when asked (a display node, a bake), not on every edit; the
  three perf tests (`AnEditCostsLittleWhenManyObjectsAreGrouped`, `AThousandsObjectDocumentStaysResponsive`, `TheFrameOfALargeGraphIsCheapToMeasureWhileScrubbing`) are
  re-measured after S2a.
* Flattening: `geometryFromPath` keeps cubics, so art brush curves survive; a scatter brush with thousands of copies makes a large geometry (capped as the
  graph brush is, `kMaxShapes`).
* The node's cache key must include everything its output depends on (its floats, the stroke width and profile, the shape's outline), or an edit shows stale
  geometry.
