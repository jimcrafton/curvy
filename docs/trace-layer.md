# Trace layer

Guide shapes drawn over the artwork: reference outlines, and later the snap targets for drawing tools (see `curvy impl notes.txt`, "Trace").
**Nothing here goes through the node graph**: no node is made, the cook pipeline never sees it, and it is painted with plain Blend2D.

## Pieces

- `TraceItem` (`include/curvy/tracelayer.h`): one shape, in document points: `Kind::Rect` (`x`, `y` top left, `width`, `height` size) and
  `Kind::Line` (`x`, `y` start, `width`, `height` the vector to the end, so the file entry and `translated()` are shared). More kinds
  (polygon, curve) add a `Kind` and a branch in `valid()`, `bounds()`, `path()`, `snapPoints()`, `segments()` (hit testing is `distanceTo()`, built
  on `segments()`), `paintItemAs()` and the Python wrapper.
- `TraceLineTool` (`tracelinetool.h`, id `traceLine`, Rulers ring key I): press, drag, release; Shift locks the angle to 45 degree steps (and
  turns snapping off for the end, which would pull it off the ray), Escape drops it. Both ends snap like the rect tool's. **Perpendicular**: when the
  start snapped onto an edge (or corner) of a trace shape, the directions of the edges passing through it are kept (`findReferences`), and a drag
  within `kPerpendicularDegrees` (5) of square to one of them is projected onto the exact perpendicular, with a green right-angle mark at the start
  (nothing at drags under 4 px). It wins over a weak end snap (edge or guide) but not over a corner, midpoint, centre or crossing within reach, which
  is taken as deliberate; Ctrl and Shift turn it off. Snaps stack: while square-locked, the end is also pulled to where the perpendicular ray meets
  another shape's edge or a ruler guide (`Snapper::snapOnRay`, within reach of where the pointer put it, ahead of the start only), so the line
  is square to one shape and ends exactly on another. It also lines up with another shape's corner, midpoint or centre: the spot on the ray at that
  point's x or y (`SnapResult::Kind::Align`, `reference` is the point it lines up with, painted as a dotted line); a crossing or guide at the
  same distance keeps its own mark.
  **Free line-up** (`Snapper::align`): when the pointer (start or end, no square lock) snapped to nothing, it is lined up with the x and/or y of
  any trace shape's corner, midpoint or centre within reach, and with the line's own start (so level and plumb lines are easy); each matched axis
  gets a dotted reference line (`SnapResult::references`, `reference`, `reference2`). Ctrl turns it off with the other snaps. `TraceRectTool` uses it
  the same way (its first corner counts as an own point), and `TraceSelectTool` uses `Snapper::alignMove` for a drag that `snapMove` left alone: the
  moved snap point that needs the smallest pull to line up decides, on the axes it matches.
- `TraceLayer`: the items plus `visible` and `locked`. A plain copyable value, so the document can snapshot it. Painted outline only, in one
  fixed colour and width (`kColor`, `kWidth`) at 80% (`kLineOpacity`, a little more solid than the sheet), on a **sheet**: `paintSheet` washes the whole canvas white at `opacity` (default 0.6,
  0.05 to 1, so the artwork shows through like tracing paper). The sheet shows while the layer has shapes, or while a trace tool is the current tool
  (`CanvasView::traceSheetActive`, so an empty layer still shows what it is), and not when the layer is hidden. Order in `CanvasView::paint`: artwork,
  sheet, outlines, ruler guides. The layer exists with the document: the first shape is simply added to it. Not clipped to the page.
- `SketchDocument::trace()` and `addTraceItem` / `setTraceItem` / `removeTraceItem` / `clearTrace`: each one undo step through `editTrace` (the
  same snapshot pattern as the guides). `setTraceVisible` / `setTraceLocked` are not undoable. `onTraceChanged` fires after any change.
- `TraceRectTool` (`tracerecttool.h`, id `traceRect`): press, drag, release; Shift square, Alt from the centre, Escape drops. It calls
  `addTraceItem` directly. Reached from the Rulers ring (View, Rulers, "Trace rect", key T); "Trace layer" (H) shows or hides the layer.
- `TraceSelectTool` (`traceselecttool.h`, id `traceSelect`, Rulers ring key S): click a shape's outline to select it (the inside of a rectangle is
  empty canvas), Shift adds or removes, a drag from a selected outline moves everything selected (3 px before it counts as a drag; the moved
  outlines show in the accent colour while dragging), a drag from empty canvas is a marquee that takes the shapes it fully contains, Delete or
  Backspace removes the selection, Escape drops a drag or clears the selection. Moving and deleting go through
  `SketchDocument::moveTraceItems` / `removeTraceItems` (several shapes, one undo step, bad index or locked layer refused before anything changes).
  The selection lives in the tool (indices; stale ones are dropped) and is cleared when the tool is activated; the canvas selection and the graph are
  never touched.
- `Snapper` (`snapper.h`, `snapper.cpp`): a stateless reader of the document, so any tool can make one. `snap(point, zoom, exclude)` pulls a point onto
  the visible trace shapes and ruler guides within `kPixels` (8) screen pixels. Order: a point target (corner, edge midpoint, centre; the nearest
  wins) beats everything; else the nearest point of an edge; then a guide on each axis (a vertical guide sets x, a horizontal one y, and a guide wins
  over an edge on its own axis). `snapMove(points, delta, zoom, exclude)` is for a shape being dragged: it asks the question for each of the
  shape's snap points and applies the pull of one of them (a point landing on a corner, midpoint or centre beats one that only slides along an edge
  or a guide; among equals the smallest pull), so a corner dropped near another corner locks to it on both axes. `paintMarker` draws the green mark
  (square corner, diamond midpoint, ring centre, cross edge or guide). Hidden layers and hidden guides do not snap. `TraceItem::snapPoints()` and
  `segments()` are what a shape offers it; a new kind only has to provide those two. Python: `doc.snap_point(x, y, zoom=1.0)` returns `(x, y, kind)`.
  `TraceRectTool` snaps both corners (before the Shift or Alt constraint) and `TraceSelectTool` snaps a move, excluding the shapes being moved; Ctrl
  turns snapping off in both. The old one-axis `CanvasView::snap` for ruler guides is untouched. Canvas drawing tools will call this class too, once
  they exist.
- `CanvasView::paint` calls `TraceLayer::paint`; the tool paints its preview in `paintOverlay` with `TraceLayer::paintItem`.

## File

`document.trace`: `{ items: [["rect", x, y, w, h], ...], locked, visible, opacity }`, written only when there is something to say. The reader is lenient:
an unknown kind or an invalid shape is dropped. Python: `doc.trace_items` and friends (`docs/py-curvy-module.md`).

## Not done yet

Resizing a shape by hand (handles), nudging with the arrow keys, copy and paste, the other shapes and tools
(polygon, curve, ellipse), snapping perspective rays (edge crossings now snap: `SnapResult::Kind::Intersection`, a point target like a corner), a Settings switch for snapping (only Ctrl for now),
canvas drawing tools calling the snapper (waits for the Rectangle tool), the reference image, the perspective rig, and an export that skips the layer (it is never exported).
