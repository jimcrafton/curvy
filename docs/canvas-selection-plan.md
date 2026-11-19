# Canvas shape selection: finishing plan

Written 2026-11-18. Tracker row TC-06 (the marquee part also touches TC-03). Builds on `SelectionModel`, `CanvasView::shapeAt`, `WorkspaceController::selectAt`
(see `docs/shape-structure.md`).

## Where it stands

Built and unit-tested (curvyTests): click selects the node that made the shape, Shift/Ctrl adds, empty canvas clears, a click on
the selected shape steps up (generator, group, layer, start over), accent outline on selected shapes and on shapes inside a selected
layer/group, status bar names the node, selection shared with the graph view and panels, Space still pans, double-click enters a
group, paths from `sketchobjects.cpp` are selectable and stay selectable after point edits (`test_canvasselect.cpp`, 10 tests).

Not done:
1. Nobody has tried it live and there is no GUI test.
2. A shape is selectable only if its geometry carries a `shape.key`. jitnodes `circle`/`line` and the path nodes from
   `sketchobjects.cpp` stamp one. Anonymous shapes (Python `add_primitive` without a key, wrangle output that drops keys, anything
   imported, text glyphs) cannot be clicked: `selectAt` treats them as empty canvas.
3. No marquee on the canvas (TC-03 is mockup only; the graph view has one).
4. Scripts cannot ask "what is under this point" or "select what is in this rectangle".
5. Point-level selection is not covered here.

## Steps

Each step ends with: unit tests, the GUI test for it, `docs/` updated, tracker row updated, release build + `curvyTests.exe` run.
GUI tests take the real mouse: ask before running them.

### S1. GUI suite for what exists (do first, it shows whether the foundation is sound)
DONE 2026-11-18: `tests/gui/test_curvy_canvasselect.py`, 8 tests, 7 pass live, 1 expected failure. Covers click, replace, Shift/Ctrl add,
clear (page and pasteboard), step-up and start over, stroked-line hit vs gap, Space pan, accent outline pixels. Findings: (a) the canvas has
no double-click handler, so "double-click a grouped shape enters the group" is not built (only the graph view does it; the test is
`expectedFailure` until it is); (b) a right-button press opens the ring (`pressed()`), which S3's secondary-button marquee must
coexist with; (c) not covered: graph view highlight, Ctrl+Z after a script selection. The text below is the original list.
New `tests/gui/test_curvy_canvasselect.py`, class on `CurvyCase`, console-driven setup as in `test_curvy_pyshapes.py`.
- Setup: `new_a4()`, open the console, run a line that draws known shapes (two circles side by side, a rectangle path, a layer or
  group holding one). Positions come from the document-to-canvas mapping; read the canvas rect with `app.find("canvas")` and
  derive click points from the drawn coordinates instead of hard-coding pixels.
- Tests:
  - click a shape: the Node panel shows its node (read the panel field with `app.prop`/`text_of`), status line names it;
  - click another: the selection is replaced;
  - Shift-click adds, Ctrl-click adds;
  - click empty canvas: cleared;
  - click the selected shape again: steps up to the group/layer, again: starts over;
  - a stroked open line is hit on the line, not in the gap beside it;
  - hold Space and drag: pans, selection unchanged;
  - double-click a grouped shape: enters the group (WR-05);
  - the graph view highlights the same node (shared model);
  - Ctrl+Z after a script-made selection change is not an undo step (selection is not undoable).
- Screenshot the outline once and compare against the unselected canvas (accent pixels appear), so the paint path is covered.
- Fix whatever the live run finds before moving on.

### S2. Make every shape selectable (key stamping)
BUILT 2026-11-18 (curvyTests 706). Stamping is done once, in the engine: `Network::setStampAnonymousShapes(true)` (off by default so plain
jitnodes keeps "no keys, no allocation"; `GraphDocument` turns it on). In `Network::runTask`, right after a node cooks, a result with
anonymous primitives is copied and each one gets `<node key>#p<index>` (`Geometry::hasAnonymousPrimitives`, `stampAnonymousShapes`), so
every node that makes primitives is covered without touching it: the jitnodes curve/generative/pattern/particle/file nodes, the python
node, wrangle, `sketchobjects.cpp` (already keyed). Keys the producer set are kept. Bypassed and frozen results are not touched (their
source was stamped when it cooked); fused point-kernel chains copy their input and keep its keys. Tests: four in `test_pythonnode.cpp`
(opt-in off, stamped and distinct, stable across a recook, explicit key kept, document network on). Still to do here: importers and text
glyphs after the port (S6) must be cooked by a node, so they get this for free, but add them to a test then; a GUI test that clicks a
python-node shape (S1 extension); a unit test per other producer family if you want more than the central one. The text below is the original list.
Decided (2026-11-18): auto-stamp (option A). Every shape that reaches the canvas has a non-empty `shape.key`; nothing selects "the
producing node" in place of a shape. Keys are generated at the display boundary (and by `add_primitive` when `key` is omitted), and
must be deterministic (node name plus primitive index) so a recook does not lose the selection. Work:
- Audit every producer of shape geometry and write the result into this file: jitnodes nodes, `sketchobjects.cpp`, the python
  node (`MutableGeometry.add_primitive(key=...)`), wrangle, and (after the port, S6) importers and text.
- Add a test per producer: "every primitive it outputs has a non-empty key". One parameterised gtest in `test_shapes.cpp`.
- Auto-stamp unkeyed primitives at the end of a python/wrangle cook (and at the display boundary for any other producer).
- Python: `add_primitive` without a key generates one; `3rdparty/jitnodes/docs/python.md` says so and recommends explicit keys.
- GUI: extend S1 with a python-node shape and a wrangle shape.

### S3b. Select by panel and ring slots
BUILT 2026-11-18 (curvyTests 732, GUI `test_curvy_selectpanel.py` 2 tests pass). `shapeproperties.h/.cpp` is the one query: `describeShapes`
(per keyed shape: tags of its node and the layers/groups around it without the internal `:` tags and the bare layer/group tags, fill and
stroke `#RRGGBB` from `fill.*`/`stroke.*`, stroke width to half a pixel, node type, layer name), `valuesOf`, `countValues`,
`shapesWithValue`. `SelectPanel` (`selectpanel.h/.cpp`, id `select`, title "Select by", loaded from `Resources/selectPanel.newui`,
collapsed by default, listed after Transform): property tabs and Within as `SegmentedControl`s, the values in a `ListView` (own item
class `SelectValueItem`: swatch, text, count, accent outline on the row the selected shape has), Add / Subtract / Similar buttons.
A click on a value replaces the selection (Shift/Ctrl held adds, Alt subtracts); Add / Subtract apply the last value clicked;
Similar selects what has the single selected shape's value of the chosen property. Ring: Document ring got `Select` (L) with
`By...` (B, shows the dock and expands the panel) and `Similar` (S). Selection uses `SelectionModel::selectShapeKeys`. Not done:
Python (`shapes_where`, S5), custom attributes, a "select similar" key, a button state for Add/Subtract that persists, Ctrl+A etc. (S4).
Marking: tags like `swatch:...` and `object:xform` are hidden by the `:` rule, so a user tag must not contain a colon.

### S3. Marquee on the canvas
BUILT 2026-11-18 (curvyTests 724, GUI `test_curvy_canvasselect.py` 12 tests: 11 pass + 1 expected failure). `shapesInRegion` /
`rectRegion` / `ellipseRegion` (shapepaths), `Marquee` (gesture), `SelectionModel::selectShapeKeys`, controller wiring, outline while
dragging, `M` cycles the region, Settings `selectRegion` / `selectTwoButton` / `selectContainment` (file only, `pen` group). Ring: a
secondary press on empty canvas with the two-button rule opens the ring on release instead of on the press; with the rule off it
opens on the press as before. Two existing pen tests were changed for that. Not done: the options card (no toolbar tool concept in
code yet: the Select tool is just "the" tool), a Settings dialog row for the three values, Python (`shapes_in_rect`, S5), S3b.
Note for S4: Tab (without Shift) already toggles the graph view in `filterKey`, so Tab cycling needs another key or a rule.
MOCKUP DONE 2026-11-18: `docs/mockups/selectionModes.html` (new page; the approved `transform.html` is untouched and its direction rule,
left to right contained / right to left touching, is superseded by the two-button rule below). Scope grew on 2026-11-18: region shapes
Rectangle, Ellipse, Freehand, Polygon, and "select by" (tag, fill, stroke, stroke width, type, layer). Pending the user's look and OK
before code. Select-by sources: node tags (`Node::tags()` of the producing node and its layer/group), primitive attributes
`fill.r/g/b/a`, `stroke.r/g/b/a`, `stroke.width` (stamped by the look nodes), producing node type, enclosing layer. One query,
`shapesWhere(artwork, predicate)`, feeds the marquee, the card and Python (`doc.shapes_in_rect`, `doc.select_where`). The old text follows.
DECIDED 2026-11-18: "Select by" is a dedicated collapsible panel in the right dock (next to Document, Node, Transform), plus a
**Select by...** slot in the empty-canvas ring that shows and focuses the panel, and a "Select similar" ring slot/key that applies the
selected shape's own value directly. Region shape and Inside/Touches live in the Select tool's options card, not in the panel. The work
splits in two so each can be tried alone: S3 = canvas marquee (four region shapes, two-button rule, option fallback, `shapesInRect`);
S3b = Select panel + ring slots (`shapesWhere`, `.newui` from the mockup panel via the html-mockup-to-newui skill, panel conventions from
`dockPanels.html`; build after S3 since both use the query). Open: the right-button press currently opens the ring (`pressed()`), so
two-button mode must move the ring off the right button on empty canvas, or fall back to the single Containment setting.
PANEL MOCKUP DONE 2026-11-18: `docs/mockups/selectPanel.html` (static layout, 268 px dock width, for html-mockup-to-newui; the
interactive behaviour is `selectionModes.html`). Property tabs (Tag, Fill, Stroke, Width, Type, Layer), a value list with swatch and count
(the selected shape's own value is the "on" row), Within (Document / Selection), Add / Subtract / Similar buttons, a status line.
Add / Subtract are for a pen with no keyboard (they stand in for Shift / Alt on a click of a value). Not converted to `.newui` yet:
waiting for the user's OK on the look.
Mockup first (`docs/mockups/transform.html` is the approved TC-03 mockup; check it covers the marquee rectangle and its
colours, extend it if not), then code. Never hand-write UI.
- `shapesInRect(rect, mode)` in `shapepaths` (mode: touches or fully inside), returns keys. Decided (2026-11-18): two gestures, the
  primary button drags a fully-inside marquee, the secondary button drags a touches marquee. Fallback if the secondary button
  proves awkward (pen barrel button, ring-menu conflict): a selection-tool option for the mode, which brings back the small
  option box curvy v1 had for ring-menu items (needs its own mockup).
- Press on empty canvas then drag past a small threshold starts a marquee; a press with no drag is still the click that clears.
  Shift adds, a drag that starts on a shape moves nothing (moving is a separate feature). Space still pans, other tools keep
  their own drags.
- Overlay drawn in `paintTop`; result via `SelectionModel::addAll` (its comment already names the marquee), one repaint, status
  bar says "N shapes".
- Unit tests: rect hits, fully-inside vs touching, additive, nothing hit clears, drag threshold, Space pan wins.
- GUI test: drag a rectangle round two of three circles, check the Node panel/selection count; Shift-drag adds the third;
  drag on empty space away from shapes clears.

### S4. Keyboard and small gestures
BUILT 2026-11-18 (curvyTests 736, GUI `test_curvy_canvasselect.py` 14 tests: 13 pass + 1 expected failure). Ctrl+A selects every shape of the
document (not "the current level" yet: the canvas has no level notion apart from the graph view's), Esc clears the selection (an empty
selection leaves Esc alone; a marquee or the ring still take it first), Tab / Shift+Tab step to the next / previous shape in drawing
order and wrap. Decided with the user: Tab cycles only while a shape is selected; with nothing selected (or a graph node selected, or the
graph showing) Tab keeps toggling the graph. Finding: newui reserves Tab for focus navigation and never delivers it to a view unless the
view sets `wantsTabKey`, so the canvas's old "Tab shows the graph" never fired from the keyboard; `CanvasView` now sets it, which
makes both work (and Tab no longer moves the focus off the canvas). Note for GUI tests: reading the selection goes through the console,
which takes the keyboard focus, so a test that sends a key after a read must give the canvas the focus back (Shift-click on empty
canvas keeps the selection). Original text:
Decided (2026-11-18): all three. Ctrl+A select all shapes at the current level, Esc clears, Tab/Shift+Tab cycles shapes. Unit + GUI
test each.

### S5. Python reach (CLAUDE.md: scripting is a requirement)
BUILT 2026-11-18 (curvyTests 737): `doc.shape_at`, `doc.shapes_in_rect`, `doc.shapes_where(**criteria)`, `doc.select_where(combine, **criteria)`
in `scriptmodule.cpp`, documented in `docs/py-curvy-module.md` ("Finding shapes"); one test. Sample `Resources/samples/scripts/selectShapes.py` (covered by
`everySampleScriptRunsAndDrawsSomething`) and GUI test `test_shapes_are_found_by_point_rectangle_and_property_and_selected` in
`test_curvy_pyshapes.py` added (GUI suite `test_curvy_pyshapes.py` 5/5 pass live; `shape_at` / `select_where` hand back the shape's `path` node; the hint bar for a script-selected path is not asserted). The text below is the original plan.
In `scriptmodule.cpp` and tests: `doc.shape_at(x, y, tolerance=...)` returning the owning node (or `None`), and
`doc.shapes_in_rect(x, y, w, h, inside=True)` returning nodes; `selection` setters already exist. Same lifetime guard and
main-thread rules as the rest of the module. Update `docs/py-curvy-module.md`, add a sample script that selects by rectangle and
an `everySampleScriptRunsAndDrawsSomething`-compatible form, and a GUI line in `test_curvy_pyshapes.py`.

### S6. After the `tmp/` port (see `curvy-port-tmp-to-graph` in memory)
Everything the port brings in must be selectable: importers (SVG, PDF, AI, EPS), text glyph shapes (shape-structure step 5),
skeletal-stroke output. Add each producer to the S2 key test and to a GUI test that imports a sample (`skyDreamer.svg`,
`girl-sketch.svg`) and clicks a known shape. This step cannot start before the port.

### S7. Point-level selection (separate decision)
Not planned here. If wanted, it gets its own plan and mockup.

### Close-out
Tracker TC-06 row (status, notes, Verified, plus the Summary formulas and Notes snapshot figures), memory note, `docs/` pages
(`shape-structure.md` selection section, `py-curvy-module.md`). No commits: the user commits.

## Decisions needed from the user
1. DECIDED: anonymous shapes are auto-stamped with a key (S2).
2. DECIDED: primary-button marquee = fully inside, secondary-button marquee = touches; a selection-tool option is the fallback (S3).
3. DECIDED: S4 builds all three: Ctrl+A, Esc, Tab / Shift+Tab cycling.
4. Order: S1 first (recommended), then S2, S3, S5; S6 waits for the port.
