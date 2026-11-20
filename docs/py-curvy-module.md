# The `curvy` Python module (developer notes)

How scripts reach the document, and what to change when the C++ side grows. Background: `docs/py-binding-plan.md` (the generator and
jitnodes bindings) and `docs/py_scripting_plan.md` (the whole feature). User-facing behaviour of the console panel is in `PyConsolePanel`.

## Pieces

| File | Role |
|---|---|
| `src/scriptmodule.cpp`, `include/curvy/scriptmodule.h` | The `curvy` module: `Document`, `Object`, `Selection`, `Swatch`, `Palette`. Hand-written pybind11. |
| `src/scriptconsole.cpp`, `include/curvy/scriptconsole.h` | `ScriptConsole`: starts the interpreter, owns the one scope console input runs in, runs code with `doc` and `network` bound. |
| `src/pyconsolepanel.cpp`, `include/curvy/pyconsolepanel.h` | The right-dock panel that feeds `ScriptConsole::run`. |
| `src/scripteditorpanel.cpp`, `include/curvy/scripteditorpanel.h` | The script editor panel (open, save, run through the console, scratch file, title); the editor itself is a `CodeEditorView`. |
| `src/codeeditorview.cpp`, `include/curvy/codeeditorview.h` | `CodeEditorView`: the reusable code editor (text, line numbers, folds, editing keys, find and replace, problem line, highlight and syntax-check timers). Also meant for the wrangle code section of the Node panel. |
| `src/codelanguage.cpp`, `include/curvy/codelanguage.h` | `CodeLanguage`: what an editor asks of a language (style sheet, style ranges, new-line indent, folds). The base is plain text; `PythonLanguage` (in `pythonhighlight.h`) overrides all four. |
| `include/curvy/syntaxproblem.h` | `SyntaxProblem` (line, column, message), shared by `ScriptConsole::checkSyntax` and the editor view. |
| `src/pythonhighlight.cpp`, `include/curvy/pythonhighlight.h`, `3rdparty/lex` | Python colours and folds for the editor (see "The `lex` library"). |
| `cmake/python.cmake` | Builds jitnodes' `jitnodesPython`, links pybind11 and the bundled Python (`3rdparty/python`), copies the DLLs. |
| `3rdparty/jitnodes/python/` | The `jitnodes` module (graph handles, Geometry, NumPy views) and `PythonRuntime`. |

The module is **hand-written, not generated**. `SketchDocument` takes `NodeRef`, `newui::Color` and palette types and must be reached through
a lifetime guard, which scriptgen does not produce. The price is that nothing updates it for you: see "Adding a feature".

## How a script sees the document

`ScriptConsole::run(code, document, selection)` sets `doc` to `makeScriptDocument(*document, ..., selection)`, `network` to
`jitnodes::python::wrapNetwork(document->network())` and `selection` to `doc.selection` before every run (all `None` when there is no
document). Names the script defines persist between runs because
there is one scope for the process.

`doc` is a `curvy.Document`; layers and groups come back as `curvy.Object`; graph nodes come back as `jitnodes` `Node` handles.

### Lifetime guard

Python objects can outlive the document (`kept = doc`, then File > New). Raw pointers would then dangle, so:

- `DocumentGuard` holds a `SketchDocument*` plus the document's `lifetime()` weak token. `get()` throws `std::runtime_error("the document is
  closed")` when the token has expired. Pybind11 turns that into a Python `RuntimeError`.
- `ScriptDocument` and `ScriptObject` both hold a `shared_ptr<DocumentGuard>` and call `get()` on **every** member call, never caching the
  reference.
- A `ScriptObject` also checks `hasNode(ref)` (`node()`): undo or `move_to` can remove or replace the node, and a stale object
  raises "the object no longer exists".
- `new_blank` and `open` replace the whole document, and node ids then start again, so `hasNode` alone would let an object from before
  silently point at an unrelated new one. `SketchDocument::replaceCount()` (bumped in `install()`, which New and every load go through) is
  noted by each `ScriptObject` when it is made; `ScriptObject::document()` and `liveRef()` (used by `refOf`, so also by `doc.remove_node(old)`)
  raise "the object no longer exists" when it differs. `repr` shows `Object(<gone>)`. Not checked for plain `jitnodes` node handles.
- Node handles are `jitnodes` handles (id + network token + generation); they go stale on their own, with `StaleHandleError`.

Every new wrapper type that reaches into the document must hold a `DocumentGuard` and use it the same way.

### Naming nodes: `NodeRef` and handles

The C++ document names a node by `NodeRef{scope, id}`: `scope` is an interned number for the network the node lives in (0 is the root graph;
each layer/group subnet has its own). Python names a node by a `PyNode` (a pointer to a network plus an id), or by a `curvy.Object`.

- `refOf(document, value)` accepts either. For a `ScriptObject` it returns the stored `NodeRef`. For a `PyNode` it searches the scopes for
  the network the handle points at (`findNetwork(scope) == handle.network().raw()`) and builds `NodeRef(scope, id)`; no match means the node
  is not in this document and it throws `invalid_argument`. That is why a node from another document is refused.
- `wrapNode(document, ref)` goes the other way: a borrowed `NetworkRef` to `networkOf(ref.scope)` plus the id.
- `scopeFor(document, parent)` turns the `parent=` argument of `add_node` into a scope (`scopeOfSubnet`); `None` is the root.
- `Object.network` wraps the object's inner network, so a script can read what is inside a layer with the `jitnodes` API.
  Edits made on a `jitnodes` network directly are **not** undo steps; use `doc` for changes.

## Undo

Each mutating `Document` or `Object` method calls one `SketchDocument` / `GraphDocument` operation, and each of those is one undo step (the
document already says so in its header comments). The wrapper adds no undo logic of its own. `doc.undo()`, `redo()`, `can_undo()` and
`can_redo()` go to the document's `UndoStack`.

### One script, one undo step

`ScriptTransaction` (declared in `scriptmodule.h`, owned by `ScriptConsole::Impl`) makes every `run` that has a document a single step:

1. `run` calls `begin(document)`, which notes `UndoStack::depth()`.
2. The script's edits push their own steps as usual.
3. After the script (finished, failed or cancelled) `run` calls `end()`, which calls `UndoStack::mergeSince(mark, "Script")`. The steps
   pushed in between become one composite step (undo runs the parts last to first, redo first to last). A single step is only renamed; no
   steps leaves the stack alone. `end()` is safe when the document closed meanwhile.

`mergeSince` and `depth` were added to newui's `UndoStack` (`3rdparty/newui`; carry to `D:\code\newui`).

`doc.undo()`, `doc.redo()` and `doc.new_blank()` move or clear the history, so a depth noted earlier would be wrong afterwards. Each wraps
itself in `HistoryChange`: it ends the transaction (what the script did so far becomes one step), runs the call, and begins a new
transaction. So `add B, add C, undo, add D` undoes the B+C step and leaves D as the script's step. Any new wrapper method that clears or
rewinds the history needs the same `HistoryChange` line.

Setters that the C++ side documents as not undoable stay that way (`set_page_size`, `ppi`, `unit`, `angle_unit`, `new_blank`); they are not part of the step.

### Sub-transactions: `with doc.transaction(name):`

An all-or-nothing block inside a script (`ScriptBlock`, bound as `curvy.Transaction`). `__enter__` notes `UndoStack::depth()`. Leaving
normally calls `mergeSince(mark, name)` (the block's edits become one step, which the run's own step then contains). Leaving with an
exception calls `UndoStack::rollbackTo(mark)`: the steps since the mark are undone and dropped (no redo entries remain) and the exception
carries on, so the script can catch it and continue with the document as it was before the block. `curvy.Cancelled` counts as an
exception, so a cancel inside a block takes that block back (edits made before it stay, per the run rule). A `doc.undo()`, `redo()` or
`new_blank()` inside a block moves the history under its mark; the block then merges or rolls back nothing rather than something wrong
(`mergeSince` and `rollbackTo` ignore a mark at or beyond the current depth). Not undone by a rollback: the non-undoable setters above.
`rollbackTo` was added to newui's `UndoStack` (carry to `D:\code\newui`).

## Shapes, paths and points

jitnodes has no rectangle, star or path generator (only `circle` and `line`), so every shape added from Python is a **`geometry` node**
(its `geometry` parameter holds the points and one primitive) followed by a `curvyFill` and/or `curvyStroke`, wired on top of the object's
merge. `SketchDocument::addPath` builds it as one undo step; the Python shape methods only compute points:

| `Object` method | Points made |
|---|---|
| `add_rect(x, y, w, h, radius=0, fill, stroke, stroke_width)` | 4 straight points; with `radius` a closed curve of 8 segments (24 control points) |
| `add_ellipse(cx, cy, rx, ry, ...)` | closed curve, 4 Bezier segments (12 points, handle factor 0.5523) |
| `add_polygon(cx, cy, radius, sides, rotation=-90, ...)` | regular polygon, straight points |
| `add_star(cx, cy, outer, inner, points=5, rotation=-90, ...)` | 2n straight points |
| `add_line(x1, y1, x2, y2, stroke, stroke_width)` | open, 2 points, no fill |
| `add_path(points, closed=False, curve=False, fill=None, stroke, stroke_width, name)` | the points as given; `curve=True` takes Bezier control points in SVG order (3n+1 open, 3n closed) |

Each returns the last node of the chain (stroke, else fill, else the points node), like `add_circle` returns its fill. Fill and stroke are
`(r, g, b, a)` or `None`. The geometry gets the shape key `keyPrefix + uidText` at creation (the same key a generator's cook would give), so
the canvas can select it; `shapeKeyOf` stays correct because the key is made from the node's own uid.

Point editing works on the `geometry` node found by `SketchDocument::pathNodeOf` (walks input 0 upstream), so any node of the chain can be
passed. Each is one undo step: `points`, `set_points`, `set_point`, `move_points(dx, dy, indices=None)`, `insert_point`, `remove_point`,
`is_closed`, `set_closed`, `is_curve`, `path_node`. Rules in `setPathPoints` / `setPathClosed`: the same point count moves the points
(any number of subpaths); another count needs exactly one primitive, which keeps its kind and closed flag, and a curve must still fit 3n / 3n+1.
`insert_point` and `remove_point` refuse curves (handles are points: the layout would break); closing a curve drops its last anchor
(which equals the first) and opening it adds it back. A node that is not a path (a circle's fill, a layer) raises.

## Object transforms

A layer's or group's transform is its `transform` node (tagged `object:xform`, the object's display node), so the whole object, shapes and
subgroups, moves with it. `SketchDocument::objectTransform` / `setObjectTransform` read and write its parameters (`tx ty rot sx sy skew px py`;
degrees; pivot in the object's own space; an unknown name throws before anything changes) as one undo step, through `setGraphFloats`. The
Transform panel's Object tab edits the same node. Python, on `Object`: `transform` (a dict copy), `set_transform(**params)`, and the relative
helpers `translate(dx, dy)` (adds to tx, ty), `rotate(degrees, px=None, py=None)` (adds to rot; px, py set the pivot) and `scale(sx, sy=None)`
(multiplies sx, sy), plus `reset_transform()`. The helpers act on the parameters, not on a composed matrix: rotating and then scaling is the
node's own order (pivot, translate, rotate, skew, scale), and moving the pivot of an already transformed object moves the result.

### Single shapes

A shape's own transform is a `transform` node named `localXform` wired right after its generator (before the fill and stroke, so stroke
width is not scaled), the same node the Transform panel's Local tab edits. `SketchDocument::localTransformOf`, `addLocalTransform`,
`shapeTransform` and `setShapeTransform` find the shape from any node of its chain (through `SelectionModel::find(Role::Shape)` on a
temporary model); a layer or group, or a node that is in no shape, throws. `addLocalTransform` is also what the panel's Add button calls.
`setShapeTransform` creates the node when missing and sets the values in the same undo step; `shapeTransform` only reads (identity values
when there is no node yet, nothing is created). Shapes are `jitnodes` node handles, which cannot take methods, so these are `Document`
calls: `shape_transform(node)`, `set_shape_transform(node, **params)`, `translate_shape`, `rotate_shape`, `scale_shape`,
`reset_shape_transform`, with the same parameter-level meaning as the object helpers. The pivot defaults to the page origin, not the shape's
centre: pass `px`/`py` to `rotate_shape`, or set them with `set_shape_transform`.

## Fills and strokes of existing shapes

A shape's look is the `curvyFill` / `curvyStroke` node in its chain. `SketchDocument::shapeLook(node, LookKind)` reads its parameters (`visible r g b
alpha opacity`, plus `evenOdd` on a fill and `width` on a stroke; nothing when the shape has no such node) and `setShapeLook` sets named ones as
one undo step, making the node first when needed: a fill goes after the shape's local transform (or its generator), a stroke after the fill. An
unknown name throws and changes nothing. The shape is found from any node of its chain, as for transforms.

Python, on `Document`: `fill(node)` and `stroke(node)` give `(r, g, b, a)` or `None` (no node, or hidden), `stroke_width(node)` gives the width or
`None`, `set_fill(node, color, opacity=None, even_odd=None)` and `set_stroke(node, color, width=None, opacity=None)`. `color=None` **hides** the
look (`visible = 0`) rather than deleting the node, so a later colour needs no new node and keeps its width; a colour makes it visible again. To
change only the opacity or width, pass the current colour (`doc.set_fill(s, doc.fill(s), opacity=0.5)`). Not exposed: cap, join and miter, which
the stroke node stores but the canvas does not draw yet (it reads only `stroke.width`); expose them when the renderer honours them.

## Files: `doc.path`, `doc.save()`, `doc.open()`

Only the native `.curvy` format is reachable from scripts. Export (SVG, PDF, EPS, PNG) is not yet. The import/export work is merged on main,
and `tmp/` holds that whole tree (the old Sketch app with the file-formats and skel-strokes branches merged), but it is written for the old
Sketch document (`layers()`, `artboards()`, symbols, brush objects). `src/io` and `include/curvy/io` in v2 are identical to the `tmp/` copy, are not in
`CMakeLists.txt`, and need headers that exist only in `tmp/` (`gzip.h`, `imagedata.h`, `textoutline.h`, `sketchexport.h`, `graphbrush.h`). Offering
export to scripts therefore means porting the exporters to read the v2 graph document (its cooked geometry), then wrapping them as `doc.export_*`.

- `doc.path`: the file the document was opened from or last saved to, or `None`. `doc.is_modified`: changed since then (the script's own edits count).
- `doc.save(path=None)`: writes the file and returns the path. No argument saves to `doc.path` (`RuntimeError` when there is none yet); with a
  path it is Save As and the document belongs to that file afterwards. Not an undo step. A failure raises `RuntimeError("could not save ...: <reason>")`.
  `Document::save` keeps a one-time `<path>.bak` of an existing file, as the app's Save does. The saved view (zoom and pan) is the one from the last
  Save or Open in the window, since a script has no canvas to ask.
- `doc.open(path, discard_changes=False)`: replaces the document with a file, as File > Open does, but **refuses while the document is modified**
  unless `discard_changes=True` (the window would ask; a script cannot). A file that cannot be read leaves the document as it was. Everything from
  before is gone (see "Lifetime guard"), and the undo history starts again, so later edits in the same script are one step of the new
  document (`HistoryChange`, like `new_blank`). Damaged objects are left alone (the window cannot prompt in the middle of a script); the status bar says how many.
- Both run on the main thread (every `Document` call does), so a big file freezes the window for its duration, like a `cook()`.
- The window is told through `ScriptConsole::setDocumentFileHook` (`setScriptDocumentFileHook`): after a save `WorkspaceController` refreshes the title,
  after an open it calls `showDocument()` (canvas, graph view, panels, saved view) and says "A script opened <file>". `onExecuted` refreshes the
  title as well, because the modified marker is not updated while a script runs.
- Sample: `Resources/samples/scripts/fileRoundTrip.py`. Tests: `ScriptModule.*` (save, Save As, open, refusals, failures, stale objects, the hook) and
  `tests/gui/test_curvy_pyfiles.py`.

## Running a script from the command line

`curvy.exe --run script.py [--exit-after-run] [-- arg ...]` (main.cpp, `WorkspaceController::runScriptFile`):

- The script runs once the window is up, through the same path as Run in the editor: in the background, shown in the console panel, with
  the status bar, Cancel, Esc and the time limit. `--open file` loads first (the run waits while the open is busy).
- `args` in the script is the list of what follows a bare `--` (`ScriptConsole::setArguments`; empty for the editor's runs).
- What the script prints goes to stdout as it goes (a 100 ms timer forwards `ScriptConsole::outputSoFar()`, flushed each time, so a long
  job can be watched or piped); the rest and its error (stderr) arrive when it ends (`PyConsolePanel::onFinished`).
- `--exit-after-run` closes the window when the script ends and sets the exit code: 0 ok, 1 the script raised, 2 cancelled or timed out,
  3 the script file cannot be read, 4 Python is not available. Without it the window stays open and the exit code is 0.
- The window is closed by posting `WM_CLOSE`, so teardown happens in the normal order (an earlier version ended the run loop directly and
  crashed in the destructors). `forcedClose_` skips the "discard changes?" prompt and the busy refusal, which a batch run could not answer, and
  does not save the window placement, so a batch run never rewrites the user's settings. A script that wants its changes kept must `doc.save()`.
- A UTF-8 byte order mark at the start of the file is dropped.
- Example: `curvy --open in.curvy --run fix.py --exit-after-run -- out.curvy` with `fix.py` doing the edits and `doc.save(args[0])`.
- Tests: `ScriptConsole.argsIsTheListGiven...` (unit) and `tests/gui/test_curvy_cli.py` (6 tests, each starts the exe with no input: output, error,
  `args`, exit codes 0 to 3, open-change-save as a batch job).

## The Scripts menu

The top bar's **Scripts** button (`scriptsToggle`, next to Panels) runs a script without opening the editor. It lists, as `PanelMenuEntry` lines
built by `ScriptsMenu::entries` (the same data-then-popup pattern as the Panels menu, shown through `WorkspaceController::presentMenu`):

1. Your scripts: the `.py` files directly in the `scripts` folder next to the settings file (`WorkspaceController::userScriptsFolder`, so a
   `--settings` file brings its own folder), ordered by name without regard to case. An empty or missing folder shows a line that says so.
2. The samples that come with the app (`Resources/samples/scripts` next to the exe), each marked "(sample)".
3. **Open scripts folder**: creates the folder if needed and opens it in Explorer.

A picked script runs like Run in the editor (`runMenuScript`: console panel, status bar, Cancel, time limit; refused with the usual message while another
script or a file task is busy). The menu is built each time it opens, so a new file shows at once. `ScriptsMenu::readScript` (shared with `--run`) drops a
UTF-8 byte order mark. Subfolders are not listed. The button's `.newui` entry was added by hand next to Panels (the file has hand-made parts that a
regeneration from `workspace.html` would lose); the mockup has the button too. Tests: `ScriptsMenu.*` (unit). The popup itself is native and blocks, so
it is checked by hand, not by a GUI test.

## Selection

`doc.selection` (also the global `selection`) is a `curvy.Selection` over the editor's `SelectionModel`, so a script reads what the user
picked and a script's pick shows in the canvas, graph view and panels. `ScriptConsole::run(code, document, selection)` takes the model
(null in tests and for a run without an editor: `selection` is then `None`); `PyConsolePanel::setSelection` is wired to
`graphView_->selectionModel()` in `WorkspaceController`. `ScriptSelection` holds the `DocumentGuard` and re-checks
`model.document() == &guard.get()` on every call, because the model outlives a closed document. Items come back as `Object` (layer/group)
or `jitnodes` `Node` (`wrapAny`). Members: `items`, `primary`, `len`, iteration, `in`, `set`, `add`, `remove`, `toggle`, `replace`,
`clear`, `step_up`, `isolation`, `enter_group(group)`, `exit_group()` (the group being edited on the canvas, see `docs/tools.md`), and the role queries `object`, `layer`, `fill`, `stroke`, `shape` (the nodes around the primary, or `None`).
Changing the selection is not an undo step.

### Finding shapes

These `Document` methods read `doc.artwork()` (the cooked geometry the canvas draws) and answer with the node that made each shape, the same
node a canvas click selects (`GraphDocument::nodeForShapeKey`, so every shape needs a key; see the key stamping in the selection plan).
They share the C++ queries of the canvas: `ShapeGeometry::hitAt` and `keysInRegion` (`shapegeometry.h`) and `ShapeTable` (`shapetable.h`:
`having`, `keysWith`, `counts`; what the Select panel uses). Units are document units.

- `doc.shape_at(x, y, tolerance=2.0)`: the topmost shape's node under the point (inside its fill or within `tolerance` of its stroke), or `None`.
- `doc.shapes_in_rect(x, y, width, height, inside=True)`: nodes of the shapes wholly inside the rectangle, or touching it with `inside=False`.
- `doc.shapes_where(tag=, fill=, stroke=, width=, type=, layer=)`: nodes of the shapes matching every given criterion. `fill`/`stroke` are
  `'#RRGGBB'` (any case) or `None` for "not drawn"; `width` is a number rounded to half a unit; `tag` is any user tag of the node or the
  layers and groups around it (tags with a `:` are internal and never match); an unknown criterion raises.
- `doc.select_where(combine='replace', **criteria)`: selects the shapes `shapes_where` finds (`'replace'`, `'add'` or `'subtract'`) and
  returns their nodes; raises when the run has no editor selection.

A node that made several shapes is listed once. The tests are `shapesAreFoundByPointRectangleAndProperty` in `tests/test_scriptmodule.cpp`.

## Values and conversions

- Colours are `(r, g, b, a)` tuples in 0..1 (`Rgba` in the source). `Swatch` is the C++ struct bound directly; palettes come back as copies, so
  editing a returned `Swatch` changes nothing in the document: use `replace_swatch`.
- `color_mode` maps to `ColorSpace` by name; only `'rgb'` and `'cmyk'` are accepted (the document ignores other spaces).
- `artwork()` returns the cooked `GeometryHandle` as a `jitnodes.Geometry` (read-only) or `None`. This needs the `jitnodes` module imported
  first so the type is registered; `ScriptConsole`'s constructor does that.
- Index and range errors on the C++ side are `std::runtime_error`/`out_of_range` and arrive in Python as `RuntimeError`/`IndexError`.

## Running code (`ScriptConsole::run`)

1. stdout and stderr are swapped for one C++ `OutputSink` (a `curvy._Output` object with a mutex-guarded string) for the duration of the run,
   and restored afterwards. Being C++, the main thread can read what a background script printed so far (`outputSoFar()`) without the GIL.
2. The code is compiled as an expression first; on `SyntaxError` it is compiled as statements. An expression whose value is not `None`
   prints its `repr`, like the interactive interpreter.
3. A Python exception or a C++ exception sets `ok = false` and `error` (the traceback text); `run` never throws for a script error.
4. Output is whatever the script printed, followed by the `repr` line.

Constraints: one `ScriptConsole` per process (CPython cannot be restarted), created and destroyed on the main thread. `run()` runs the script
on the calling thread and blocks it (tests, and the fallback when no poster is set). The application uses `runInBackground()` (next section).
Declaration order in `ScriptConsole::Impl` matters: the scope (Python objects) must be destroyed before the interpreter.

### Background runs (`ScriptConsole::runInBackground`)

The script runs on a worker thread; the window stays alive, so the console panel can show a Cancel button, live output and progress.

- **Main-thread rule.** The document, graph, undo stack and selection belong to the main thread. Every method and property of the `curvy`
  classes (`Document`, `Object`, `Selection`, `Transaction`) and of the `jitnodes` handle classes (`Network`, `Node`, `History`) is replaced at
  registration (`guardClass` in `scriptmodule.cpp`) by a C++ wrapper that calls `callOnMain`. On the main thread, or with no poster set, it
  calls straight through. On another thread it releases the GIL, posts a task through the poster (`RunLoop::post` in the app; a queue in
  tests) and waits; the task takes the GIL on the main thread, calls the original, and hands the result or the exception object back.
  Wrappers are C++ (`py::cpp_function`), so they add no frames to a traceback. `__init__`, `__new__`, `__del__` and the pickling hooks stay
  as they are.
- **New bindings are covered automatically** as long as they are methods or properties of those classes. A *new class* that touches the
  document must be added to the `guardClass` list in `registerCurvyModule`; a plain-data class (like `Swatch`) needs nothing.
- **GIL.** The main thread holds it for the interpreter's whole life, so `runInBackground` gives it up (`mainRelease`) until the run is reaped
  and takes it back in the completion task. While a background script runs, the main thread must not touch Python except inside a posted
  call; `checkSyntax()` therefore answers "no problem" while one runs.
- **Calls cost a thread hand-off** (some tenths of a millisecond, more while the UI is busy painting). A script making thousands of tiny
  calls is slower than on the main thread; batching with `doc.transaction()` does not change that.
- **Completion.** The worker posts a task that joins it, ends the undo step (`ScriptTransaction::end`), restores stdout and calls `done`
  on the main thread. A run id guards against a stale completion that arrives after `abort()`.
- **`abort()`** (window closing, the panel's destructor, `~ScriptConsole`) cancels, makes waiting scripts give up with `curvy.Cancelled`
  (`setScriptDispatchAborting`) because nobody will run their calls, joins the worker and drops `done`.
- **While it runs**, `WorkspaceController` sets `busy_` (menu commands, open, save and closing are refused, as during a file task), the editor's
  Run button reads "Stop", and the console shows the output so far, `running... 40% message` and a Cancel button.
- **Status bar.** A script still running after 2 seconds gets the line "A script is running for 35 s (40%) - message - Esc cancels",
  refreshed every 5 seconds by run-loop timers (`startScriptStatus`), and "Script ended after 1 min 5 s" when it ends. A shorter script
  leaves the status bar alone: the line would replace the hint a script's own selection change just set (the `pyshapes` GUI test caught
  that). The percent and message are what `curvy.progress()` last reported.
- **`curvy.progress(fraction, message="")`** reports progress (0..1, negative for unknown); it needs no main-thread call.
- A callback a script gives to jitnodes (a progress function for `cook`) runs on whichever thread jitnodes calls it from, under the GIL.

### Cancel

Triggers (they work the same for both kinds of run):

- `ScriptConsole::cancel()`, callable from any thread (it only sets a flag, and only while a script runs, so a late call cannot hit the next run).
- The Escape key, polled by the watcher thread (`GetAsyncKeyState`, and the foreground window must belong to this process).
  `setCancelOnEscape(false)` turns it off.
- A time limit: `ScriptConsole::setTimeout(seconds)` (zero, the default, is no limit). The same watcher compares the elapsed wall time
  with the limit (read when the run starts) and raises `curvy.Cancelled`. The result has `cancelled = true` and `timedOut = true`, and
  the error says "Script stopped: it ran longer than N seconds". An explicit cancel that came first is reported as a plain cancel.
  The limit is the **Stop scripts after** row of Settings, Script editor (`Settings::scriptTimeout`, off by default, and
  `scriptTimeoutSeconds`, default 60, range 1 to 86400; file keys `scriptEditor.scriptTimeout` and `scriptTimeoutSeconds`).
  `WorkspaceController` applies it at start-up and whenever settings are applied, so it takes effect for the next run. It counts time
  waiting for the main thread too, and has Cancel's limits: a long call into C++ or a `time.sleep` is only interrupted when it returns.

How: every `run` starts a watcher thread (25 ms poll). When a cancel is wanted it takes the GIL and calls `PyThreadState_SetAsyncExc(
scriptThread, curvy.Cancelled)`; the script's thread raises it at its next Python instruction, repeated every poll until the run ends.
The script itself runs untraced, at full speed. `run` then returns `cancelled = true`, `ok = false`, `error = "Script cancelled"`.

- `curvy.Cancelled` derives from `BaseException` (like `KeyboardInterrupt`), so a script's `except Exception:` does not swallow it. A bare
  `except:` or `except BaseException:` can.
- It only fires between Python instructions. A long call into C++ (`network.cook()` of a big graph, `render_png`, one huge NumPy operation)
  finishes first; a script that sleeps in C (`time.sleep`) is interrupted only when the sleep returns.
- Edits made before the cancel stay, as one undo step for the whole run (see "One script, one undo step"); `doc.undo()` takes them back.
- The end of `run` stops and joins the watcher with the GIL released (the watcher may be waiting for it) and then withdraws any pending
  async exception, so a cancel that arrives just as the script finishes cannot fire in later Python code.

## Bundled Python

Where the interpreter comes from (`findPythonHome`, `executableDirectory` in `scriptconsole.h`): the `python` folder next to the executable
when it contains `Lib/os.py`, otherwise `CURVY_PYTHON_HOME` (the `3rdparty/python` the build was configured with, baked in by CMake). Nothing
else is consulted: no PATH, registry, `PYTHONHOME` or user site folder, because `PythonRuntime` builds an isolated `PyConfig`. `main.cpp` and
the tests (`tests/pythonconsole.h`) both go through it, so a test run from an installed folder exercises the bundled copy.

**Making a distributable folder:**

```
cmake --build --preset x64-release
cmake --install out/build/x64-release --prefix <folder>
```

The folder holds `curvy.exe`, `Resources/`, the compiler's runtime DLLs (app-local, from `InstallRequiredSystemLibraries`, so no Visual C++
redistributable is needed), `python313.dll` and `python3.dll` next to the exe, and `python/` (about 83 MB in all, from 123 MB). The rules are
`curvyInstallPython()` in `cmake/python.cmake` and the `install` lines in the root `CMakeLists.txt`.

What `python/` leaves out (an embedded interpreter never uses it): `test`/`tests` packages, IDLE, tkinter and Tcl/Tk, `turtledemo`,
`ensurepip`, `venv`, `lib2to3`, `pydoc_data`, pip, `Scripts`, headers, import libraries, `python.exe`, the launchers and the `_test*.pyd`
modules. It keeps the standard library, `DLLs`, NumPy (the geometry views need it) and the stdlib's `__pycache__` (the install folder may be
read-only, so bytecode could not be written back). `sys.executable` points at `python/python.exe`, which is not shipped; nothing here starts a
subprocess with it.

**Checking a bundle:** copy `curvyTests.exe` into the installed folder and run it there. The suite (410 tests when this was written) passes, and
`ScriptConsole.theInterpreterRunsFromTheFolderFindPythonHomeChose` shows `sys.prefix` is the folder `findPythonHome` chose (so the
installed `python/`, not the development copy). `numpyWorksInTheEmbeddedInterpreter` covers the trimmed NumPy.

**Adding a Python package to the bundle:** install it into `3rdparty/python/Lib/site-packages` (`python.exe -m pip install <pkg> --target
...`); the install rule copies it. Watch the size, and check the exclude patterns do not drop a folder it needs (a package directory named
`test`, `tests`, `bin`, `include`, `libs` or `tcl` is excluded wherever it sits).

**Licences:** the install also writes a `licenses/` folder (Python, NumPy, pybind11 texts and `third-party-notices.md`, from
`docs/third-party-notices.md`). The notice file says which other linked libraries (jitnodes/newui dependencies, zlib, Blend2D) are still
missing from it.

Not done: an installer/zip step, code signing, the remaining licence notices, and a smaller Python (a custom stripped build).

## Adding a feature

When a `SketchDocument` or graph feature should be scriptable:

1. Add the method or property in `bindDocument` or `bindObject` in `src/scriptmodule.cpp`. Call `d.get()` (or `o.document()`) inside the
   lambda, never capture a `SketchDocument&`.
2. Convert node arguments with `refOf` and node results with `wrapNode` / `wrapObject`; colours with `colorOf` / `rgbaOf`.
3. Use snake_case names, keyword arguments with defaults where the C++ has them, and a short docstring for anything non-obvious.
4. Add a test to `tests/test_scriptmodule.cpp` (it shares one console through `tests/pythonconsole.h`, because CPython starts once per
   process). Check the undo behaviour and that a closed document or gone object raises.
5. Update this file and the tracker (PY-01).

If a new C++ class needs to appear in Python, give its wrapper a `DocumentGuard` first; do not expose a raw reference.

## Tests

- `tests/test_scriptconsole.cpp`: running code, output capture, errors, document lifetime, foreign-node refusal, cancel (a loop that never ends,
  `except Exception` cannot swallow it, a cancel with nothing running, edits before the cancel stay and undo, a late cancel). The Escape key
  itself is not covered by a unit test.
- `tests/test_scriptmodule.cpp`: page settings, layers and groups, `add_circle` and `artwork`, nodes inside an object, palettes, gone objects,
  the shape methods, point editing (undo, curve layout rules, errors) and the selection (read, change, `None` without an editor, a document
  that went away).
- `tests/test_pyconsolepanel.cpp` and `tests/gui/test_curvy_pyconsole.py`: the panel (including the "running" line).
- `tests/test_scripteditorpanel.cpp`: the script editor (text round trip, non-ASCII, save and load, BOM and CR handling, colours and folds,
  the modified marker, run-selection, Ctrl+Enter and F5, the scratch file, Run / Open / Save callbacks).
- `tests/test_pythonhighlight.cpp`: the lexer's styles (keywords, strings, prefixes, escapes, triple quotes, numbers, decorators) and the
  folds.
- `tests/test_scriptmodule.cpp` also covers `with doc.transaction():` (rollback on error, one step on success).

## The script editor panel

`ScriptEditorPanel` (`include/curvy/scripteditorpanel.h`, id `pyeditor`, title "Python Script Editor", right dock, created next to the
console panel in `WorkspaceController`): a `newui::TextFoldingControl` in a `ScrollView` above Open, Save and Run buttons. It is built the
way cpp_codetools' `CppEditor` is (`TextFoldingControl` + `HistoryTextModel` for undo, in a `ScrollView`).

- **Run:** it knows no console. Run calls `onRun(code, name)`, and the owner passes it to `PyConsolePanel::runScript`, which echoes
  `>>> run <name>` and shows the output in the console's transcript (the input history is left alone). With text selected the button
  reads "Run Selection" and sends the *whole lines* the selection touches, dedented (so a block from deep inside a script still runs);
  the name then ends in " (selection)". Ctrl+Enter and F5 do the same as the button (`EditorController`, a `TextFoldingController`
  subclass, because a text control would take Enter).
- **Colours and folds:** `include/curvy/pythonhighlight.h` / `src/pythonhighlight.cpp` turn the text into styled ranges and folds with
  the `lex` library (see below). They are recomputed 60 ms after typing stops (at once when no run loop runs, as in tests); the control
  moves the old ones along meanwhile. Folds are indentation blocks (a line ending in `:` to the last line of its block); lines inside
  triple-quoted strings or brackets, blank lines and comment-only lines do not end a block. A fold that was closed stays closed when
  the folds are recomputed (matched by where it starts). Malformed tokens (an unterminated string, a bad number) get a squiggle. The
  sheet is chosen by `Settings::darkTheme()` and re-applied on every restyle, so a theme change is followed.
- **Gutter:** line numbers and the fold markers come with `TextFoldingControl`.
- **Editing keys** (`EditorController`, a `TextFoldingController` subclass in `codeeditorview.cpp`, whose Enter indent comes from the `CodeLanguage`; each is one edit, so one undo step):
  Enter keeps the line's indentation, adds four spaces when the code before the caret ends in `:` (a `#` comment, a colon inside a
  string, or an unclosed bracket does not count; `pythonNewLineIndent`, lexer-based) and takes one level off after a line that starts with
  `pass`, `return`, `break`, `continue` or `raise`. Tab inserts spaces up to the next multiple of four (replacing a selection within one
  line) or, with a multi-line selection, indents every selected line (blank lines stay empty). Shift+Tab takes up to four spaces (or one
  tab character) off the caret's line or the selected lines. The tab character that follows the Tab key press is swallowed
  (`handleKeyPress`), or it would be typed as well.
- **Font:** by default ("Automatic", `automaticEditorFont()` in `include/curvy/editorfonts.h`) the first installed of
  JetBrainsMono-Medium, Hack-Regular, SourceCodePro-Medium, FiraCode-Medium, Fira Mono, Consolas (then newui's default). The system
  default (Cascadia Mono) looked very thin. newui lists every weight as a face of its own named after the font file
  (`JetBrainsMono-Medium`), so a heavier weight is a different entry in the list.
- **Settings page:** Settings > **Script editor** (`pageScriptEditor` in `docs/mockups/settings.html`, converted to
  `Resources/settings.newui`; wired in `settingsdialogsession.h`, `wireScriptEditor()`). It has the **font** (a drop-down of
  `installedMonospaceFonts()`: installed faces whose names look monospace, without italics, plus "Automatic"; a face from a hand-edited
  file that is not in the list is added), the **size** (8-32), the **tab width** (2-8: what Tab inserts, how far Enter indents after a
  colon, how wide a tab character is drawn), and **one colour per syntax style** (`pythonSyntaxStyles()`: keyword, True/False/None,
  string, number, comment, name, function/class, decorator, operator, punctuation) as a `#RRGGBB` field with a colour well. Typing the
  look's own colour, or nothing, removes the override. A **preview** under the colours (a read-only `TextControl` with sample code)
  follows every change. Changes apply live: the workspace's `restyle()` calls `ScriptEditorPanel::applySettings(settings)`, which
  also makes the editor follow the dark/light theme (the style sheet is chosen by `Settings::darkTheme()`). **Reset page** puts the page
  back. Stored in the settings file under `scriptEditor` (`font`, `fontSize`, `tabWidth`, `colors`: only the changed styles). The
  editor's text colour and background can be overridden too (see "Text and background colours" below). Settings.cpp ignores
  out-of-range numbers and malformed colours.
- **Regenerating the dialog:** `settings.newui` is generated from `settings.html` (command in the mockup's header comment). Two
  properties exist only in the `.newui` and must be put back after a regeneration: `cancelControl: true` on `settingsCancel` and
  `defaultControl: true` on `settingsOk`.
- **Title / modified marker:** `Python Script Editor - <file or untitled>`, plus ` *` while modified. Modified is `!HistoryTextModel::
  isClean()`, so undoing back to the saved text clears it. It is evaluated on the model's `onHistoryChanged`, not `onChanged`: `onChanged`
  fires before the edit is recorded and still sees the old clean state.
- **Across sessions:** `setScratchFile(path)` (the workspace passes `scratch.py` next to the settings file; none when there is no
  settings file) writes the text there 800 ms after typing stops while it is modified, and restores it as an untitled, modified script
  when the editor starts empty. The file is deleted when the script is saved or another file is loaded. The panel's destructor flushes a
  pending write. Which file was open is remembered separately (see "Remembered file" below), so unsaved text comes back under that file's name.
- **Files:** Open and Save ask for a path through `onChooseOpen` / `onChooseSave` (the native dialogs, `*.py`). Files are UTF-8; a BOM and
  carriage returns are dropped on load. The text is converted with `curvy::utf8ToWide` / `wideToUtf8` (`include/curvy/utf8.h`); the
  console panel uses them too (it used to widen byte by byte, which garbled non-ASCII).

- **Find:** Ctrl+F (handled in `EditorController`) opens a find bar between the editor and the buttons: a `TextField`, a match count
  label ("2 of 7", "No match", "7 found" when the selection is not on a match), Prev, Next and a close button. A selection within one
  line pre-fills the field. Typing selects the first match at or after the selection start (so the current match stays while the word is
  extended); Enter, F3 or Next go to the next match, Shift+Enter, Shift+F3 or Prev to the previous one, both wrapping; Esc closes the
  bar and gives the focus back to the editor. The search is smart-case: case-insensitive unless the query has a capital letter. The
  match is the editor's selection, so while it is selected Run says "Run Selection" and runs that line. `revealCaret()` scrolls the
  `ScrollView` bar to the match when it is off screen. The field's own keys (Esc, Shift+Enter, F3) come from `FindFieldController`.
  **Marks:** a `TextControl` does not draw its selection while another control has the focus, so with the find field focused the
  match would be invisible. `updateFindStatus()` therefore sets `TextDecoration`s on the editor: a translucent `Background` on every
  match and a `Box` frame on the current one (the match the selection is on). It runs after every query change, step, selection change
  and text edit (`refreshHighlight`), and clears the marks when the bar closes.
- **Replace:** Ctrl+H (or `showReplace()`) adds a second row under the find row: the replacement field, **Replace** and **All**. Replace
  (also Enter in the replacement field) replaces the selection when it is a match of the query, then selects the next match; **All**
  replaces every match, working from the end so offsets stay valid, inside `HistoryTextModel::beginGroup()/endGroup()` so one Ctrl+Z
  undoes all of it. The replacement is literal text (no regular expressions, no case adaptation). Ctrl+F keeps the replace row if it
  is already open; closing the bar hides both.

- **Find switches:** the row under the find field has three toggle buttons, **Aa** (match case), **W** (whole word) and **.\*** (regular
  expression; `pyEditorMatchCase`, `pyEditorWholeWord`, `pyEditorRegex`). One routine, `collectMatches`, finds every match for the
  status count, the marks, Next/Prev, Replace and All, so they cannot disagree. Plain text is smart-case (case-sensitive only with a
  capital letter) unless Aa is on; a regular expression (`std::wregex`, ECMAScript) is case-insensitive unless Aa is on. Whole word
  drops matches whose word-character edges touch another word character (letters, digits, `_`; an edge that is a symbol is not
  checked, so `(id)` still matches inside `f(id)`). Empty regex matches are skipped. A pattern that does not parse gives the status
  "Bad pattern" and no matches. The replacement of a regex may use `$1`.. and `$&` (`match_results::format`, computed per match so
  lookahead context is right); All replaces from the end inside one undo group as before.
- **Syntax check:** `ScriptEditorPanel::onCheckSyntax` (wired to `ScriptConsole::checkSyntax` in `WorkspaceController`) is asked for the
  script's first error 400 ms after typing stops (at once without a run loop, and after `setScript`). `checkSyntax` calls Python's
  `compile(code, "<script>", "exec")` and never runs anything; it reads `msg`, `lineno`, `offset`, `end_lineno`, `end_offset` from the
  `SyntaxError` (IndentationError and TabError are subclasses). The panel shows `Line N, col M: message` in a red line under the editor
  (`pyEditorProblem`, `kProblemHeight`; the editor shrinks by that line) and a squiggle from the column to the end of the token (or
  line). The squiggle goes through `applyMarks`, which also carries the find marks, so neither replaces the other. While a script runs
  (UI blocked anyway) `checkSyntax` returns nothing. The lexer's own squiggles (unterminated strings, bad numbers) are separate and
  still there.
- **Remembered file:** `Settings::editorFile` (under `scriptEditor.file`; not reset by the page's Reset, it is state). The panel's
  `onFileChanged` fires after a successful Open or Save and the workspace stores it and saves the settings. At start
  `restoreFile(path)` runs after `setScratchFile`: unsaved text that came back from `scratch.py` becomes the text of that file (title
  shows it, still modified); otherwise an empty editor loads the file if it still exists; a missing file does nothing.
- **Text and background colours:** Settings > Script editor > Editor colours (rows `syntaxHex_text` / `syntaxHex_background`, from the
  mockup like the others; `pythonBaseColors()` lists them next to `pythonSyntaxStyles()`), stored in `editorColors` under the keys
  `text` and `background`. `applySettings` sets the control's text and caret colour and the background of the text control and its
  scroll view; when the background override is cleared the theme's fill is put back (its kind and colour are kept when the override
  first replaces them, because `gfx::Fill` cannot be copied). Gutter colours are still the theme's.

- **Run and syntax errors:** `run()` (button, Ctrl+Enter, F5) asks `onCheckSyntax` about the code it is about to send (the selected whole
  lines, dedented, or the script) and sends nothing when it does not compile: `onMessage` (the hint bar) gets "Not run - line 4, col 7:
  invalid syntax", with "selection" in front for a selection (its line is counted from the selection's first line). So a selection that
  compiles still runs in a script that does not. While the whole script has a problem the button reads "⚠ Run" (U+26A0) instead of
  "Run"; "Run Selection" is unchanged.

Not done: a find history, editing the gutter and selection colours.

## The `lex` library

`3rdparty/lex` is copied from `D:\code\cpp_codetools` (`include/lex`, `src/lex`): the base lexer (`lexer_base`) and the incremental
highlighter (`highlight`), standard library only, built as the static library `lex` in the root `CMakeLists.txt`. The Python lexer
(`python_lexer`) and language (`python_language`) were written here, modelled on the library's CMake lexer, and still have to be
carried back to cpp_codetools. Notes on the lexer: a triple-quoted string, or a string continued with a backslash, is split per line and
carried in `LexState` (so a line can be lexed on its own); `@` first on a line is a `Decorator` (class Preprocessor, shown as
"decorator"); the name after `def` / `class` is styled as `PropertyName` ("definition"). f-strings are plain strings. Tests:
`tests/test_pythonhighlight.cpp`.
- Run `curvyTests.exe` directly after a build and check that the test count includes the new tests (see CLAUDE.md, Build).

## Known limits and next steps

- Scripts run in the background in the app (see "Background runs"). The time limit is opt-in (see Cancel). A long call into C++ made from a script (a cook)
  runs on the main thread and freezes the window for its duration. `run()` (tests, no poster) still blocks, which is why
  `PyConsolePanel::onBeforeRun` exists. The script editor's own "Run Selection" and the console input share the one running slot.
- No stroke cap/join/miter, effects, boolean operations or text yet; `artwork()` gives
  read-only data. Paths are built from the `geometry` node (see "Shapes, paths and points"), so there is no live rectangle or star to
  re-parameterise: the numbers are baked into the points.
- Packaging stops at `cmake --install` (no installer or zip, no signing); see "Bundled Python".

## Wrangle helpers

`curvy.check_wrangle(code)` compiles wrangle code (`checkWrangle` in `wranglecheck.h`, which runs `jitnodes::parseWrangle`) without running it
and returns `None` when it parses, else `(line, column, message)` (1-based, the message without the parser's offset prefix); the Node panel's
Code editor shows the same problem under the text. `curvy.wrangle_functions()` lists `(name, arity)` of the language's functions. A script
sets a wrangle's text with the existing `doc.set_string(node, "code", text)` (declared `| name = v(min, max) |` parameters appear as the
node's spare floats), and `Resources/samples/scripts/wrangleNode.py` shows it. Test: `ScriptModule.checkWrangleNamesTheFirstProblemAndListsTheFunctions`.

## Assets: `jitnodes.assets`

The asset API lives in the `jitnodes` module, not in `curvy` (the library is process-global, not per document); `docs/asset-manager-plan.md`
is the design and `3rdparty/jitnodes/docs/python.md` ("Assets") the API. What curvy adds on top is in `registerCurvyModule`:

- `jitnodes.assets.AssetDef` and `_Restricted` go through `guardClass`, and every function of the submodule (`list`, `has`, `get`, `install`,
  `install_directory`, `reload`, `uninstall`, `update_instances`, `create`, `unlock`, `rename_parameter`, `check_scripts`, `restricted`) is wrapped
  with `guarded`, so a script on a worker thread reaches the registry and the library through the main thread. A new function added to
  `pyAssets.cpp` must be added to that list.
- `AssetDef.validate()` / `apply()` / `save()` refuse a definition whose scripts break the rules: the script check is installed as
  `AssetDefinition::setScriptValidator` when the module registers. The same check is `jitnodes.assets.check_scripts(scripts)` and, from C++,
  `jitnodes::python::checkAssetScripts` (`pyAssets.h`), which the Asset Properties dialog calls for the problem line while the user types.
- **`doc.assets`** (`ScriptAssets`, holds the `DocumentGuard`): `update_instances(name=None) -> int` and `unlock(node) -> node` do what the
  Assets panel does: snapshot the graph, call `jitnodes::updateAssetInstances` / `unlockAssetInstance`, then `commitGraphEdit`, so the change is
  an undo step of the document (merged into the script's single step) and the shapes resync. Use these instead of `jitnodes.assets.update_instances`
  / `unlock` for the open document, which act on a bare network and bypass the undo stack. Both honour `EventRestriction`. Test:
  `ScriptModule.docAssetsUpdateAndUnlockAreUndoStepsOfTheDocument`.
- Events are fired by `AssetEvents` (see the plan). An application that runs an event wraps it in `with jitnodes.assets.restricted(node, event)` (or the C++ `EventRestriction`,
  `pyGuard.h`): `onFrameChanged` then cannot change the node, what is inside it or what it reads from, and `onVersionUpdated` cannot change
  anything; a second restriction for the same node and event raises. What the node "reads from" is found by following its wiring upstream;
  a node that only reaches it through a parameter expression (`other.param`) is not seen.
- **Event scripts and parameters.** An event is a script body (statements, no `def`) stored in the asset's `scripts`; the names it gets are listed in
  the plan (`node`, `kwargs`, and for `onVersionUpdated` `old`, `fresh`, `inputs`, `from_version`, `to_version`). `node`, `old` and `fresh` are
  `jitnodes.Node` handles, and a handle has no `parm()` object: it forwards the engine's `Node` methods under snake_case names (the list is
  generated from `node.h`, `3rdparty/jitnodes/docs/python.md`, "How the binding is built"). The ones an event uses for parameters:

  | Call | Meaning |
  |---|---|
  | `get_float(name)`, `base_float(name)` | the value resolved at the last cook, or the stored value ignoring expressions and keyframes; in `onVersionUpdated` nothing has cooked, so `base_float` is the safer read |
  | `set_float(name, v)` | set the stored value (an edit: refused on a restricted node) |
  | `has_float(name)`, `float_names()`, `spare_names()`, `is_spare(name)` | what the node has; a promoted asset parameter is a spare float |
  | `add_spare_float(name, default)`, `remove_spare_float(name)`, `rename_spare_float(from, to)` | change the spare parameters |
  | `get_string(name)`, `set_string(name, v)`, `string_names()` | string parameters |
  | `set_expression(name, text)`, `expression(name)`, `is_animated(name)` | expression, also keyframes (`set_keyframes`, `clear_animation`) |
  | `float_minimum(name)` / `float_maximum(name)` | `(ok, value)`, the limit a node type declares |

  For a rename that only moves a value, list it in the asset's `renames` instead of writing a script. A script such as
  `fresh.set_float("sizeCm", old.get_float("size") * 2.54)` is what `onVersionUpdated` is for (`tests/test_assetpanel.cpp`,
  `updatingInstancesRunsOnVersionUpdatedWithTheOldAndTheNewInstance`). Reading `old` and writing `fresh` is allowed; every other node, the document and
  `network` are out of reach (see the restriction above).
- Tests: `ScriptModule.anAssetDefinitionIsEditedRegisteredAndInstancedFromPython`, `aDefinitionIsSavedToAFileAndInstalledAgain`,
  `theScriptCheckNamesWhatAnEventMayNotDo`, `aDefinitionWithAScriptThatBreaksTheRulesIsRefused`,
  `whileAnEventRunsTheHandlesRefuseEditsToTheAssetAndWhatItReadsFrom`.

## Python nodes: the trust gate

jitnodes' `python` node (see `3rdparty/jitnodes/docs/python.md`) runs the code in its `code` parameter, and refuses to until the application
allows it. curvy's answer is `PythonNodeTrust` (`include/curvy/pythonnodetrust.h`), made by `WorkspaceController::wirePythonNodeTrust` whenever a
script console is set (without a console there is no Python and no policy, so the nodes fail):

- The policy is `jitnodes::python::setPythonNodePolicy`, asking `PythonNodeTrust::allows(code)` on every cook, from any thread. It is allowed by
  the exact text of the code (a hash, kept in `trustedPythonNodes.txt` next to `trustedAssets.txt`), so an edited text asks again. There is no
  folder rule: a node's code lives in the document.
- The first refused text queues `askPending()` on the run loop (the cook that refused is on a worker thread and cannot show a dialog). One
  prompt covers every refused text at that moment ("N Python nodes in this document have code that has not been allowed yet"). Yes: the texts
  are remembered and the canvas and graph view recook (the nodes that failed are still dirty). No: they are declined for the session, are not
  asked about again and are not saved; the status line says they stay disabled.
- This covers every way code arrives (open, paste, an asset's contents, undo, a script that writes the text), because it is the cook that asks.
  `PythonNodeTrust::trust(code)` allows a text without asking; blank code is always allowed (a node that was just made has none).
- **Node panel editor.** The code row of `NodePanel` (the wrangle one, `Resources/nodePanelCode.newui`) also edits a `python` node: the panel holds
  two `CodeEditorView`s in the one host (a view's language is fixed when it is made), `wrangleEditor_` and `pythonEditor_` (`PythonLanguage`), and
  shows the one for the node on show; the class button is hidden for Python. `NodePanel::onCheckPython` is the syntax check
  (`ScriptConsole::checkSyntax`) and `onPythonCodeCommitted` is called with the text before the edit, so the controller trusts what the user typed and
  the cook that follows does not prompt. Commit, Revert and Ctrl+Enter work as for a wrangle. The cook error line is the first line of the
  exception message (`ValueError: boom`; pybind puts the frames after it). Selecting a python node cooks it, as for a wrangle, so its code runs then
  (and an untrusted text asks). When the cook was refused (the message starts with `jitnodes::python::pythonNodeNotAllowedMessage`) an **Allow** button
  (`wrangleAllow`, at the right end of the error line, from `docs/mockups/nodePanelCodeSection.html`) is shown: `NodePanel::allowCode` calls
  `onPythonCodeAllowed` with the node's own code (the controller trusts it, which also undoes a decline) and shows the node again, so the way back from
  "No" does not need an edit or a restart. Tests: `PythonCodeTest` in `tests/test_nodepanel.cpp`.
- A script can call `jitnodes.allow_python_nodes(True)`, which replaces the policy: scripts are code the user ran, so this is not a
  boundary. The window resets the policy when it closes or its console changes.
- **Time limit and Esc.** A python node runs while the canvas draws, on the main thread, so a runaway script would freeze the application. The window sets
  `jitnodes::python::setPythonNodeTimeLimit` from `Settings::pythonNodeSeconds` (default 10, 0 = no limit; Settings > Script editor > "Stop Python nodes after",
  file key `scriptEditor.pythonNodeSeconds`; it is separate from the script time limit above, which is off by default) and
  `setPythonNodeInterruptCheck` to "Esc is down while this application has the focus" (the check the script console uses). A node that was stopped
  stays stopped until its code, a parameter or an input changes (docs in `3rdparty/jitnodes/docs/python.md`). Tests: `PythonNodeTest` (limit,
  `except Exception`, stopped node, cancel, host check) and the default in `tests/test_settings.cpp`.
- Tests: `tests/test_pythonnodetrust.cpp` (the class, and a window refusing and then allowing).

## Object opacity and blend

`Object.opacity` (0..1, clamped) and `Object.blend` (a name from `curvy.blend_modes()`, case-insensitive; an unknown name raises `ValueError`)
read and set the layer's or group's `opacity` and `blend` parameters as one undo step each. They are ordinary float parameters of the
`curvyObject` node (`doc.set_param(layer, "blend", 1)` works too; `blend` is the index into `BlendModes::names()` in `blendmodes.h`).
Test: `ScriptModule.anObjectsOpacityAndBlendAreParametersOfItsNode`.

### Units (2026-11-18)

`doc.unit` ('px', 'pt', 'mm', 'cm', 'in', 'pc', 'm', 'ft', 'dip') and `doc.angle_unit` ('deg', 'rad') are the units the document is worked in
(`SketchDocument::units()`, saved as `document.units`). They change only how the app shows numbers: every number a script reads or writes
stays in points and degrees. `curvy.to_points(value, unit, ppi=72)` and `curvy.from_points(points, unit, ppi=72)` convert (`'px'` uses the
ppi). Plan: `docs/units-dimensions-guides-plan.md`.

### Dimensions (2026-11-18)

`selection.dimensions()` is a list of read-only `curvy.Dimension` objects, the numbers the canvas labels around the selection (`kind`: `'width'`,
`'height'`, `'radius'` for one circle, `'length'` for one other shape; `value` in points; `anchor`, an `(x, y)` pair in points; `editable`).
`doc.set_dimension(kind, value, about_pivot=False)` resizes the selection so that measurement is `value` points, as one undo step: a width scales along the
document's x axis about the left edge of the selection's box, a height along y about the top edge, a radius uniformly about the centre; with
`about_pivot=True` (Alt in the canvas) the pivot of the transform being edited stays where it is instead. A transform that does nothing yet gets
its pivot at the centre of what it holds the first time it is used (a new shape transform, or `set_transform` on a layer or group that gives no
`px`/`py`); a transform that already changes something keeps its pivot. It edits
the transform of each selected object (a shape gets a local transform when it has none), composed with the objects around it, so it is exact
through rotated or scaled groups; a group and a member of it that are both selected are scaled once. It raises when nothing is selected, the kind
is unknown or not editable now (`'length'`), or the value is not above zero.
`selection.pivots()` lists the pivots the canvas marks as `(level, x, y)` tuples in points (`'shape'`: the shape's own transform; `'object'`: its
layer or group). `doc.move_pivot(x, y, level='shape')` moves the first pivot of that level to `(x, y)` without moving anything on screen (the
transform's translation is adjusted to match), as one undo step; it raises when nothing is selected, the level is unknown, the selection has no
pivot of that level, or a coordinate is not finite. C++: `DimensionModel::movePivot` (`beginPivotMove` / `previewPivotMove` / `commit` are the
live drag). C++: `DimensionModel` (`include/curvy/dimensions.h`; `begin` /
`preview` / `commit` / `cancel` are the live session the canvas scrub uses, `apply` is the three in a row). Tests:
`ScriptModule.selectionDimensionsAreReadAndSetInOneUndoStep`, `tests/test_dimensions.cpp`.

### Guides (2026-11-18)

`doc.guides` is a list of `curvy.Guide` copies (`vertical`, `position` in points from the page's top left, read-only; `curvy.Guide(vertical, position)`
builds one). `doc.add_guide(vertical, position)` (returns the index), `doc.move_guide(i, position)`, `doc.remove_guide(i)` and `doc.clear_guides()`
(False when empty) are one undo step each and raise on a bad index or a non-finite position. `doc.guides_locked = True` makes move, remove and
clear raise (adding stays allowed); `doc.guides_visible` hides them. Lock and visibility are view choices: not undoable, not a modification.
`doc.ruler_origin` is an `(x, y)` pair, the point the rulers read zero at (points from the page's top left, default `(0, 0)`; set it from a tuple; raises
for a non-finite number; not undoable, saved as `document.rulerOrigin`). The C++ side is `SketchDocument::guides()` and friends (`include/curvy/guides.h`); the file keeps them in `document.guides`
(`{ list: [["v", 120.5], ["h", 40]], locked, visible }`, lenient reader: bad entries are dropped). Tests: `ScriptModule.guides`, `tests/test_guides.cpp`.
