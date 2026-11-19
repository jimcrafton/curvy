# Canvas tools

Written 2026-11-18 (tracker TC-07). The v1 code in `tmp/` had an enum `SketchTool` and `if (tool_ == ...)` chains in the canvas and the
controller. v2 uses one class per tool instead.

## The pieces (`include/curvy/tool.h`, `src/tool.cpp`)

- `ToolEvent`: one press, drag step or release: the view point (canvas pixels), the document point, buttons, key modifiers, `secondary`
  (right mouse or the pen's barrel button) and `pen`. `ToolKeyEvent`: virtual key and modifier mask.
- `ToolContext`: what a tool reaches the editor through: the document, the `SelectionModel`, the `CanvasView`, the `Settings`, and
  callbacks (`say`, `saveSettings`, `openRing`, `showSelection`, `documentOpen`, `graphShown`, `menuOpen`). `WorkspaceController::makeTools`
  fills it in. A tool never sees the controller, so tools can be tested with a plain fixture.
- `Tool`: `id()` / `title()`, `activate` / `deactivate` / `cancel` / `busy`, `inputDown` / `inputMove` / `inputUp`, `keyDown` / `keyUp`,
  `paintOverlay`, `options()` and `optionsNote()`. No `exec()`: a tool changes the document through the same operations scripts use, so a
  finished gesture is one undo step (see the undo-step rule in `docs/py-curvy-module.md`).
- `ToolOption`: one setting the tool offers for its next use (`Choice`, `Toggle`, `Number`; id, label, choices, range, `reset`, `get`,
  `set`, optional `enabled`). Values are doubles: a choice is an index, a toggle 0 or 1. The tool draws no UI.
- `ToolRegistry`: tools by string id in the order added; the first is current; `setCurrent(id)` deactivates the old tool and activates the
  new one. Ids are strings (not an enum) so ring slots, commands and Python can name tools and later tools can register themselves.

## What stays in `WorkspaceController`

Everything above a tool: Space panning and Ctrl+Space zoom, the rings, the pen's eraser end and barrel button, the "Tap Space to start"
state, the recent-colour keys, the graph toggle, Ctrl+A / Tab cycling routing, and the options card's dismissal (Esc, a press outside it).
The controller turns presses, moves, releases and keys into events and hands them to `tools_.current()`; `paintTop` draws the selection
outline and then `current()->paintOverlay`. A secondary press that the tool does not use opens the ring.

## `SelectTool` (`selecttool.h`, `selecttool.cpp`)

The only tool so far, and the default. It owns the `Marquee` gesture and the marquee's hit list, and has click-select (`selectAt`), Esc
(cancel a region, leave a group, or clear the selection), `M` (cycle the region), `selectAll` (Ctrl+A) and `cycle`
(Tab / Shift+Tab; the controller calls it and keeps Tab's graph toggle when it returns false). Options: Region, "Secondary button picks
what it touches", Containment (greyed while the two-button rule is on). They read and write `Settings::selectRegion`,
`selectTwoButton`, `selectContainment` and save the settings file. `WorkspaceController` keeps `selectAt`, `marquee`, `marqueeHits` and
`cycleSelectRegion` as one-line forwards so callers and tests have one entry point.

### Groups: click picks, double-click enters (Illustrator's isolation)

Changed 2026-11-18. A click selects what is under the pointer **at the current level**, `SelectionModel::pickShapeKey(key)`: the shape key
holds the uids of the subnets around the shape, outermost first, so the pick is the first group (a non-layer `ObjectNode`) after the
isolated group, or after the layers at the top level; a shape in no group is picked as its own node. Layers are never picked or entered.
Clicking the selected thing again changes nothing (the old click-again-goes-up rule is gone; `SelectionModel::stepUp` stays for scripts).

- **Double-click a group** (no modifier): `enterGroupAt` makes it the *isolation* (`isolation()`), and selects what is under the pointer
  one level down. The double-click is the system's own: Windows sends `WM_LBUTTONDBLCLK` in place of the second press (a pen tap too), newui
  raises `onMouseDblClick` on the canvas, `CanvasView::onDoubleClickFilter` calls `WorkspaceController::doubleClicked`, which offers it to
  `Tool::inputDoubleClick`; a tool that does not use it gets an ordinary press, so the system's double-click speed and distance settings
  apply and no timing code is ours. Tests call `doubleClicked` after a `pressed`.
- **Inside a group** the artwork outside it is drawn at 30% (`CanvasView::shapeDimmed`, a hook the controller points at
  `isShapeKeyDimmed`) and cannot be picked: a click on it only clears the selection; marquee hits, Ctrl+A and Tab skip it; the Object ring
  does not open over it.
- **Leave** with Esc (goes up one level and selects the group just left; the next Esc clears the selection) or a double-click on empty
  canvas. A deleted isolated group ends the isolation (`prune`).
- Marquee, Ctrl+A and Tab use the same pick (`SelectionModel::selectNodes` takes the picked nodes), so a marquee at the top level selects
  whole groups. The select panel and `shapes_where` / `select_where` still select the nodes that made the shapes (not the pick).
- Python: `selection.isolation`, `selection.enter_group(group)`, `selection.exit_group()`.

## The options card (`tooloptionscard.h`, `tooloptionscard.cpp`)

A floating card in the canvas. **Placement:** `show(tool, anchor)` opens it beside the anchor, to its right and 18 px above it, or to its
left when the right side has no room, then slides it back inside the canvas; it never covers the anchor. The anchor is the option box that
was chosen: `RadialMenu::optionAnchor()` records where that box sat (also when it is chosen by Shift + key), so the first control is a few
pixels from where the pointer already is. The head and body are placed by hand (the frame's vertical layout kept the body at the mockup's
94 px and clipped the rows; a test checks the body is as tall as its rows and footer). The frame (head with the tool's name and a close button, a body, a foot with Reset and the tool's note)
is `Resources/toolOptionsCard.newui`, from the approved mockup `docs/mockups/toolOptionsCard.html`; the two slider rows of that mockup are
removed on load and the rows are made from `Tool::options()`: a choice is a `SegmentedControl` under its label, a toggle a `Toggle` with
its text, a number a `Slider` and a field. The card widens to fit the widest segmented control. Controls are named `option_<id>` (GUI
tests find them by it). A row whose `enabled` says no is greyed; Reset applies every option's `reset` value. Opened by
`WorkspaceController::showToolOptions(toolId, at)`; the option box on the Create ring's Select slot (command `options.select`) calls it. This
supersedes `tmp/include/curvy/tooloptionscard.h`, which was tied to the `SketchTool` enum and the `CanvasView` defaults; port the shape
tools' options by giving each tool an `options()` list, not by reviving that file.

## The rings (`WorkspaceController::openRing`, `radialmenu.h`)

Brought back from v1 (`tmp/src/workspacecontroller.cpp`). Which ring opens depends on where the pointer is:

| Pointer | Ring |
| --- | --- |
| No document | New |
| Graph view up | the node ring |
| Over a shape (the shape is selected first, unless it already is) | Object (`objectRing`; its slots are not wired yet) |
| Over the page, no shape | Create: Rectangle R, Ellipse E, Polygon P, Star S, Line L, Freehand B, Text T, Select V |
| Over the grey workspace around the page | Document (New artboard, New, Open, Save, Export, Import, View, Select > By... / Similar) |

**Disabled slots.** `RadialMenu::isEnabled(command)` is asked for every slot, option box and hub when the ring is drawn and chosen. A
command it refuses is drawn dimmed (faded pill, chip, label, option box and hub "...") and never lights up; a tap, flick or key does nothing
and the ring stays open. `WorkspaceController::commandWired(command)` is the one list of what `runCommand` really does: when a command is
built, add it there and its slot comes alive. At the time of writing the dimmed ones are the Create ring's tools other than Select, their
option boxes, the Object and Arrange rings' actions, Export, Import (Picture, Trace, PDF), New artboard, Rulers, Timeline and the hubs of
the Create and Object rings.

Only `tool.select` works from the Create ring (`tools_.setCurrent("select")`); the other tool slots say "The <name> tool is not built yet".
A slot with options carries an **option box** at its outer end, drawn inside the pill as in `docs/mockups/tooloptions.html` (a key chip, the
label, and a small dialog glyph with a heavier top edge; on the left side of the ring the box is on the left). A tap on the box, or Shift +
the slot's key, runs the slot's `optionsCommand`. Slots with a box today: Select (`options.select`, opens the card), and v1's Polygon,
Star and Freehand (`options.polygon`, `options.star`, `options.pencil`: nothing behind them until those tools exist). The press-and-hold
and barrel routes of the mockup are not built.

## Adding a tool

1. A class in `include/curvy/<name>tool.h` + `src/<name>tool.cpp` (with `// @reflect ignore=true` above it), listed in `CMakeLists.txt`.
2. Register it in `WorkspaceController::makeTools`; give it a ring slot or command that calls `tools_.setCurrent(id)`.
3. Return its settings from `options()` so the card, and later the Settings dialog, show them.
4. Tests in `tests/` against a `ToolContext` or the `WorkspaceTest` fixture.

## Not done

- Switching tools: only `select` exists, so nothing calls `setCurrent` yet (the registry and its test do). The v1 behaviour to port with the
  shape tools: a drawing tool is armed from the Create ring for one shape and then the controller returns to Select (`toolSticky_`).
- Options on ring slots as an option box (`RadialSlot::optionsCommand`) for tools other than Select.
- Python: a `curvy` wrapper for `doc.tools` (list, current, options) is not written; `ToolOption` was shaped for it (string ids, plain
  values). Add it, with tests and `docs/py-curvy-module.md`, when a second tool exists.
- Pen tool: its code is still in `WorkspaceController` and `sketchobjects.cpp`; moving it into a `PenTool` is the next candidate.
