# Units, dimension labels and guides: plan

Written 2026-11-18. Plan only, nothing is built. Three parts that build on each other: **U** units, **D** dimension labels on the canvas, **G** guides.

## Feasibility

All three are possible with the current design. What I checked:

- **Units.** Everything is stored in document units (points; `CanvasView` says "a document unit (a point)"). `SketchDocument` already has `ppi()`.
  `NewDocumentModel` has a `LengthUnit` enum with `pointsPer(unit, ppi)`, `toPoints`, `fromPoints`, but it is private to the new-document dialog
  and lacks meters, feet and angles. It moves into a core header. Nodes keep storing canonical numbers; conversion happens only where a person
  types or reads a number, so the graph, files, wrangle code and Python are unaffected.
- **Parameter tag.** The Node panel already reads tags on parameters (`unit:`, `min:`, `max:`, `choices:`, `group:`; `Node::tagParam`, saved
  as `ptags`). **Name clash:** the existing `unit:0.01` tag means *scrub step* (the 0..1 opacity shown as 0..100), not a physical unit. The new
  tag must have another name: `quantity:length` and `quantity:angle`. No file change (tags are already saved; untagged = plain number).
- **Dimension labels.** Selection outlines are drawn in `WorkspaceController::paintTop` from the cooked artwork, so a bounding box and labels can
  be drawn the same way. Editing by dragging a label reuses `NumberScrubber`. The system double-click already reaches tools
  (`Tool::inputDoubleClick`).
- **Guides.** v1 had them (`tmp/src/guides.cpp`, `documentguides.cpp`, 300 lines, `Guide{vertical, position}`, written as `["v", pos]` in
  the file header). v2's file writer (`sketchdocument.cpp`) already keeps non-graph document state beside the graph (`document.palettes`, the
  `view` section), so guides follow that route, outside the graph as asked.

Things that are **not** there and affect scope:

1. **No rulers in v2.** Only the `rulersVisible` setting and the Rulers menu entry exist; there is no ruler view in `src/`. Guides are pulled from rulers, so
   rulers are part of G (ruler view along the canvas top and left, in the active unit).
2. **No sub-selection yet** (points, edges). The Select tool picks whole shapes and groups. "Select a single edge to show its length" needs a
   direct-select tool, which does not exist. What works now: the length of a whole selected open or closed curve. Per-segment length comes
   with direct selection.
3. **No transform handles on canvas yet** (resize, rotate). Editing a width therefore has to be written as an operation, see D3.

## Questions I need answered

1. **"Display points"** = newui's `DisplayUnit::Dip` (`newui/displayunit.h`, 1/96 inch, the unit UI sizes are written in; its
   `DisplayValue::fromString` / `toString` already parse and write `"12dip"`). So 1 Dip = 0.75 pt. **"Pixels"** = a document pixel, 1/`ppi` inch
   (at 72 ppi equal to a point, so documents behave as today), *not* newui's `Px` (raw device pixel, which depends on zoom and monitor and
   has no meaning for artwork). Plain "points" (1/72 in) is not in your list; I would add it as it is what PDF/SVG use, tell me if not.
   newui's `Dlu` is a font-relative UI unit and is left out. Correct?
2. **Default unit**: pixels is the *document default*, saved per document (`document.units`), with a user default in Settings. Fine?
3. Which angle convention do node parameters use today (degrees or radians)? I will audit and keep the stored value as it is; this decides the
   conversion factor in the tag, not the design.

## U: units

### Core classes (`include/curvy/units.h`, `src/units.cpp`, marked `@reflect ignore=true` as needed)

```cpp
enum class LengthUnit { Pixels, DisplayPoints, Points, Millimeters, Centimeters, Meters, Inches, Feet };
// DisplayPoints = newui::DisplayUnit::Dip (1/96 in). Units::pointsPer(DisplayPoints) is 0.75 and does not read DisplayMetrics: the
// screen DPI scale is a view concern (it sizes UI), not a property of the artwork. The parser accepts the suffix "dip" so it is
// compatible with newui's DisplayValue strings; Picas stays for the dialog only if wanted.
enum class AngleUnit  { Degrees, Radians };

struct UnitContext { double ppi = 72.0; LengthUnit length = Pixels; AngleUnit angle = Degrees; };   // plain aggregate, bare names

class Units {                                   // static helpers, no state: scripts can call them
public:
    static double pointsPer(LengthUnit, double ppi);            // 1 unit in points (px = 72/ppi, mm = 72/25.4, ft = 864 ...)
    static double toPoints(double v, LengthUnit, double ppi);
    static double fromPoints(double pt, LengthUnit, double ppi);
    static double toDegrees(double v, AngleUnit);  static double fromDegrees(double deg, AngleUnit);
    static const char* name(LengthUnit);  static const char* name(AngleUnit);          // "px", "mm", "deg" ...
    static std::optional<LengthUnit> parseLength(std::string_view);                    // for typed suffixes
    static std::string format(double value, LengthUnit, int maxDecimals);              // "12.5 mm", trailing zeros trimmed
    // "12.5mm", "3in", "1 ft 2 in", "45", "45deg", "0.5rad": a bare number means the active unit; returns false on garbage
    static bool parseLength(std::string_view text, const UnitContext&, double& points);
    static bool parseAngle(std::string_view text, const UnitContext&, double& degrees);
};
```

- **Storage:** `SketchDocument` gets `units()` / `setUnits(UnitContext-ish)` (length, angle); `ppi` stays where it is. Saved as an optional
  `document.units: { length: "mm", angle: "deg" }` (additive, file version stays 3; a missing key = pixels, degrees; an unknown name drops the
  key, as the lenient `view` reader does). **The unit is a property of the document, not a user preference:** a document made in mm opens
  in mm on any machine, with its rulers, dimension labels, Node/Transform panel rows and typed bare numbers all in mm. It is chosen in the
  New Document dialog (which already has a unit list; its choice is stored instead of being thrown away after sizing the page), and
  Settings only supplies the default for the next new document. Documents imported from SVG/PDF/AI open as pixels (or points for
  PDF/AI/EPS, which are 1/72 in; decide at the port from `tmp/`). Changing the unit later (Document setup) is a
  document edit that marks it modified; it changes display only, never the stored geometry, which stays canonical points (1/72 in),
  so converting a document between mm and inches loses nothing. A document keeps its own `ppi` for the Pixels unit.
- **Parameter tags:** `quantity:length`, `quantity:angle`, optional `quantity:area` is **not** planned. Tag the real parameters in the
  node types that have lengths and angles (shape radius/size/position, transform translate/rotate, stroke width, brush widths). `NodePanel::Entry` gains
  `quantity` read next to `unit`/`min`/`max`; when set the row shows `Units::fromPoints(value)` with the unit's short name and parses typed
  text with `parseLength` (so "5mm" works in a pixel document); min/max and the scrub step (`unit:`) are converted along with it.
  Scrubbing still steps by the displayed unit. Same change in `TransformPanel` (its rows are length and angle by construction) and
  `ToolOption` (add an optional `quantity` field next to `min`/`max`).
- **jitnodes:** only `tagParam(...)` calls in node constructors, edited in `3rdparty/jitnodes` (the `python` branch). No new API.
- **Python:** `doc.units` (get/set, "px", "mm"...), `curvy.units.to_points(v, unit)`, `from_points`. Scripts and the Python node keep working in
  document points, so existing scripts do not change. Wrapper, tests (`tests/test_scriptmodule.cpp`) and `docs/py-curvy-module.md` updated in the same task.
- **Settings:** `Settings::defaultLength`, `defaultAngle` (only the starting choice in New Document) plus a Units row in the Settings dialog; New Document
  dialog switches to `Units` (its own conversion code is deleted).
- **Tests:** conversion table, round trips, parse ("1ft 2in", "5 mm", rubbish, locale), file round trip, Node panel row shows converted
  value and a typed value commits the right canonical number (one undo step).

## D: dimension labels

### Classes

- `DimensionModel` (`include/curvy/dimensions.h`), pure data, no UI, the thing Python can also read:

  ```cpp
  struct DimensionItem {
      enum class Kind { Width, Height, Radius, Diameter, ArcAngle, CurveLength /* later: SegmentLength */ };
      Kind kind;
      double value;                // canonical: points or degrees
      BLPoint anchor;              // where the label sits, document units
      std::string target;          // what an edit changes: a node uid + parameter, or empty for "transform the selection"
      bool editable;
  };
  class DimensionModel {
  public:
      std::vector<DimensionItem> compute(const SketchDocument&, const SelectionModel&) const;   // selection -> items
      bool apply(SketchDocument&, const SelectionModel&, const DimensionItem&, double newValue);  // one undo step
      void preview(...);  // live value while dragging, no undo step (same rule as NumberScrubber's onChange/onEnd)
  };
  ```

- `DimensionOverlay` (`dimensionoverlay.h`): paints the bounding box brackets and the label pills, hit-tests labels, owns the in-place
  editor. Colour is its own theme token `Theme::dimension` (not `accent`, which the selection uses) added to `theme.h` for light and dark.
  Owned by `SelectTool` (it is a selection feature): `paintOverlay` draws it, `inputDown/Move/Up/DoubleClick` offer events to it first.

### How each piece works

- **D1 Compute.** Selection bounds = union of the selected shapes' path boxes (`ShapeRange` + `shapePath` already exist; use the artwork
  after cook, i.e. including the object transform). Width and height labels sit on the bottom and right edge of the box. Single shape: if
  its shape node is a circle/ellipse type, with `rx == ry`, show Radius and Diameter (one label, toggled by Alt-click or a setting); an arc node
  shows the Arc angle from its sweep parameter, at the arc's midpoint. A single open or closed curve shows CurveLength (flattened path
  length via the existing flatten code). Shape kind is read from the node type of `nodeForShapeKey`, so this is a table of node
  type -> (which parameters mean radius, sweep) kept in one place (`DimensionModel`), not scattered through tools.
- **D2 Display.** Value formatted with `Units::format` in the document's units; hidden when the box is smaller than ~24 px on screen (or the
  label would cover the box) and while a drag moves the selection. A setting `Settings::showDimensions`.
- **D3 Edit by typing.** Double-click a label (`Tool::inputDoubleClick`, system double-click, as groups do) opens a small `TextBox` placed over the
  label on the canvas. Enter parses with `Units::parseLength/Angle` (so "12mm" works), Esc cancels, one undo step. `apply`:
  - a parametric target (circle radius, arc sweep): set the node parameter (`setParam`), the same call the Node panel uses;
  - Width/Height on a general selection: scale about the box's anchor (opposite edge; Shift on commit = about centre, a setting later) by
    `new/old`, by editing the object's transform node, the same operation the Transform panel uses. Proportional constrain follows the
    Transform panel's lock if it has one, otherwise a lock icon on the label (open question for the first UI pass).
- **D4 Edit by dragging.** Press on a label starts a `NumberScrubber` session (`onBegin` returns the value, `onChange` calls `preview`,
  `onEnd` makes one undo step). Shift/Ctrl-Shift fine steps come for free. The distinction between "drag" and "double-click to type" is
  the system's: a press-drag scrubs; a double-click opens the editor.
- **D5 Python.** `selection.dimensions()` returns the items; `doc.set_dimension(kind, value)` goes through `apply`. Same rules as other wrappers.
- **Tests (GoogleTest, no UI code in the app):** compute for rect, rotated rect (box, not shape), circle, arc, open curve, group, multi-selection;
  apply width on a rect gives the exact size in points, one undo step, in mm too; scrub preview does not add undo steps; the label hit test.
  GUI test for double-click-type (asked first, as per the project rule).

## G: guides

### Classes

```cpp
struct Guide { bool vertical = false; double position = 0.0; };          // document units, the v1 shape (plain aggregate; Python-friendly)
// in SketchDocument (document state, NOT graph nodes):
const std::vector<Guide>& guides() const;
std::size_t addGuide(bool vertical, double position);   bool moveGuide(std::size_t, double);
bool removeGuide(std::size_t);   bool clearGuides();   bool guidesLocked() const;   void setGuidesLocked(bool);
```

- **Storage / file:** `document.guides: [["v", 120.5], ["h", 40]]` (v1's format), optional; plus `guidesLocked` and `guidesVisible` in the `view`
  section (view state, since they are not content). Reader lenient like `view`. Not in the graph, so a graph snapshot, copy/paste or asset
  update never touches them.
- **Undo:** guides are document content, so edits are undo steps. I must check how the palettes (also non-graph document state) get undo in v2 and
  copy that mechanism; if there is none I will add one small command stack hook, and I will tell you before doing it.
- **Rulers:** `RulerView` (newui `View`) top and left of the canvas, drawing ticks in the active unit with a unit-aware tick step
  (1, 2, 5 x 10^n by zoom), the pointer position marker, the origin at the page's top-left (an origin setting later). Drag out of a ruler creates
  a guide (a `GuideDrag` gesture in the canvas), drag a guide to move it, drag it back onto a ruler to delete it, locked = no change.
  Shows/hides with the existing `rulersVisible` setting. Ruler corner click opens the unit dropdown (writes `doc.units`).
- **Canvas:** `CanvasView::paintGuides` (cyan line, full view extent, hit width 4 px), `guideAt(pt)`, hover cursor. A tool-independent
  layer in the controller, so it works under any tool; v1 code in `tmp/` is the reference to port, adapted to `Tool`/`ToolEvent`.
- **Snapping:** `CanvasView::snap(value, axis)` (v1 had it): guides always snap within a few screen pixels; later Smart Guides use the same
  entry point. Tools call it through `ToolContext`; the dimension editor does not snap.
- **Python:** `doc.guides` list of `(vertical, position)`, `add_guide`, `remove_guide`, `clear_guides`; position in document points.
- **Tests:** add/move/remove/clear with undo and redo, file round trip, malformed file section ignored, snap results, guides survive graph
  edits, tick-step table for the ruler.

## Status

- **U1 done.** **U2 done (2026-11-18):** `Quantity` (None/Length/Angle) and `Units::toDisplay`, `parseQuantity`, `displayScale` are the one conversion
  path; `NodePanel::Entry::quantity` and `TransformPanel`'s parameter table use it. Tagged: transform `tx ty px py` (length) and `rot skew`
  (angle) and circle `radius cx cy` in jitnodes, stroke `width` in `appearancenode.cpp`. Node parameter angles are stored in degrees (audited).
  Settings: `workspace.defaultLength` / `defaultAngle` (dialog rows on the Workspace page). Not done: `ToolOption::quantity` (no tool has a
  length option yet and the options card has no document; add it with the first one), tagging further shape parameters as their nodes appear.

- **G1 done (2026-11-18):** `Guide` (`include/curvy/guides.h`), `SketchDocument::guides/addGuide/moveGuide/removeGuide/clearGuides`, one undo step each
  (`editGuides`, same pattern as `editPalettes`), `onGuidesChanged`. Differences from the plan: lock and visibility are saved inside
  `document.guides` (`locked`, `visible`) beside the list, not in the `view` section, because the document class owns them; a locked set refuses
  move, remove and clear but still allows adding. Positions are doubles written with the file's 9 digit precision. No live-drag preview call yet:
  G2's drag will need one (set without an undo step, then one step at the end, like `NumberScrubber`).

- **G2 done (2026-11-18):** differences from the plan: the rulers are two `RulerView` child views of the canvas (`rulerview.h`; the canvas makes and places them, the corner it paints itself) and `CanvasView`
  paints the guides; strips are `Rulers::kThickness` = 18 px ( `setRulersVisible`, driven by `Settings::rulersVisible` and the `view.toggleRulers` command), so no
  layout change. `Rulers` (`rulers.h`) is the pure tick arithmetic (1, 2, 5 x 10^n step at least 64 px apart, minor ticks 10/4/5 per step, labels in the
  document unit, zero at the page's top left). `GuideDrag` (`guidedrag.h`) is the gesture, above every tool: a press in the top ruler pulls out a
  horizontal guide, in the left ruler a vertical one, a press within 4 px of an unlocked visible guide picks it up. The drag only moves
  `CanvasView::setGuidePreview` (no document call, so no undo steps while dragging); the release makes the one edit: add, move, or remove when dropped
  back on its ruler (a new guide released on the ruler is cancelled). Pulling out of a ruler turns hidden guides on. `CanvasView::guideAt` and
  `CanvasView::snap(value, vertical)` (guides only, 6 px) are the hit test and the one snapping entry point; no tool calls `snap` yet (the Select tool
  does not drag shapes). The pointer marker is on the rulers (`CanvasView::setPointer`; only the ruler marks itself dirty). Presses, moves and releases on a ruler are forwarded to the canvas, so the guide drag is unchanged. A dirty ruler still repaints the canvas under its strip, clipped, and that includes building every shape's path: measure on a big document. **G2 leftovers finished (2026-11-18):** the canvas shows a resize cursor over an unlocked guide; a click in the rulers' corner or a right click on a ruler opens the ruler menu (`WorkspaceController::rulerMenuEntries`: length and angle units with the document's ticked, show/lock/clear guides, reset origin); the ruler origin is `SketchDocument::rulerOrigin` (saved as `document.rulerOrigin`, Python `doc.ruler_origin`, setup not an undo step), `Rulers::ticks` counts from it, and dragging out of the corner sets it with a crosshair preview (`GuideDrag`); the View ring's Rulers slot opens a Rulers ring (Rulers, Guides, Lock guides, Clear guides, Reset origin). Guide positions are still page coordinates. Settings has its own Units page (default length and angle). Not done: tools calling `snap`.

- **D1-D5 done (2026-11-18):** `DimensionModel` (`dimensions.h`) and `DimensionOverlay` (`dimensionoverlay.h`, owned by `SelectTool`). Differences from the plan:
  the kinds are Width, Height, Radius (one circle) and CurveLength (one other shape, not editable); there is no ArcAngle (no arc node exists yet) and no
  Diameter. The model has no `target` field: every edit is a document-axis scale of the selection's box (width and height about the box's top-left,
  radius about the centre), written into the transform node of each selected object, or into a shape's local transform (made when missing, by
  `SketchDocument::ensureLocalTransformLive`, which makes no undo step of its own), after composing it with the transforms of the objects around it
  and decomposing the result back into `tx ty rot sx sy skew` (any 2D matrix fits that form), so it is exact through rotated and scaled groups. A
  circle is edited by the same transform, not its `radius` parameter, so it works for every shape. `begin / preview / commit / cancel` is the live
  session (preview always restarts from the originals; cancel is `restoreGraph`), `apply` is the three in a row. The overlay recomputes the labels
  on every paint and hit test (no cache to go stale; paintTop already walks the selected shapes). Scrub step is the power of ten at or below one point in
  the document unit (1 px, 0.1 mm, 0.01 in); the typed field takes anything `Units::parseQuantity` does. Escape cancels a scrub or the field.
  `Settings::showDimensions` (Settings > Workspace > Dimensions). Python: `selection.dimensions()`, `doc.set_dimension(kind, value)`. Not done: the
  proportional-resize lock, Shift = about the centre, edge length (direct select), a GUI test (ask first).

## Order of work (each one finishes with tests, tracker rows, docs, Python wrapper)

1. **U1** `Units` + tests; `UnitContext` in the document + file key; New Document dialog switched over. (small, no UI risk)
2. **U2** `quantity:` tags + Node panel / Transform panel / tool option conversion; tag the node parameters; Settings row; Python `doc.units`.
3. **G1** guides in the document, file, undo, Python (no UI yet), tests.
4. **G2** `RulerView`, drag-out guides, canvas painting, snapping.
5. **D1-D3** `DimensionModel`, overlay, typed edit.
6. **D4-D5** drag edit, Python, GUI test.
7. Later, with direct selection: edge length labels.

## Risks

- Width/height edit on a *group or multi-selection* with rotation: scaling the box is not the same as scaling each shape in its own frame. First
  version: scale along the document axes about the box corner, documented as such.
- A node type tagging mistake (marking a non-length as `quantity:length`) would change what the person sees; the audit lists every tagged
  parameter in a test so a change is deliberate.
- Parameters inside Python/wrangle expressions stay canonical; a person typing "5mm" into a field that holds an *expression* is refused with a
  message (expressions are not unit-aware).
