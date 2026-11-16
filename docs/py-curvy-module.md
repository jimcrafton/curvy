# The `curvy` Python module (developer notes)

How scripts reach the document, and what to change when the C++ side grows. Background: `docs/py-binding-plan.md` (the generator and
jitnodes bindings) and `docs/py_scripting_plan.md` (the whole feature). User-facing behaviour of the console panel is in `PyConsolePanel`.

## Pieces

| File | Role |
|---|---|
| `src/scriptmodule.cpp`, `include/curvy/scriptmodule.h` | The `curvy` module: `Document`, `Object`, `Swatch`, `Palette`. Hand-written pybind11. |
| `src/scriptconsole.cpp`, `include/curvy/scriptconsole.h` | `ScriptConsole`: starts the interpreter, owns the one scope console input runs in, runs code with `doc` and `network` bound. |
| `src/pyconsolepanel.cpp`, `include/curvy/pyconsolepanel.h` | The right-dock panel that feeds `ScriptConsole::run`. |
| `src/scripteditorpanel.cpp`, `include/curvy/scripteditorpanel.h` | The script editor panel (edit, open, save, run through the console). |
| `cmake/python.cmake` | Builds jitnodes' `jitnodesPython`, links pybind11 and the bundled Python (`3rdparty/python`), copies the DLLs. |
| `3rdparty/jitnodes/python/` | The `jitnodes` module (graph handles, Geometry, NumPy views) and `PythonRuntime`. |

The module is **hand-written, not generated**. `SketchDocument` takes `NodeRef`, `newui::Color` and palette types and must be reached through
a lifetime guard, which scriptgen does not produce. The price is that nothing updates it for you: see "Adding a feature".

## How a script sees the document

`ScriptConsole::run(code, document)` sets `doc` to `makeScriptDocument(*document)` and `network` to `jitnodes::python::wrapNetwork(
document->network())` before every run (both `None` when there is no document). Names the script defines persist between runs because
there is one scope for the process.

`doc` is a `curvy.Document`; layers and groups come back as `curvy.Object`; graph nodes come back as `jitnodes` `Node` handles.

### Lifetime guard

Python objects can outlive the document (`kept = doc`, then File > New). Raw pointers would then dangle, so:

- `DocumentGuard` holds a `SketchDocument*` plus the document's `lifetime()` weak token. `get()` throws `std::runtime_error("the document is
  closed")` when the token has expired. Pybind11 turns that into a Python `RuntimeError`.
- `ScriptDocument` and `ScriptObject` both hold a `shared_ptr<DocumentGuard>` and call `get()` on **every** member call, never caching the
  reference.
- A `ScriptObject` also checks `hasNode(ref)` (`node()`): undo, `new_blank` or `move_to` can remove or replace the node, and a stale object
  raises "the object no longer exists".
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

Setters that the C++ side documents as not undoable stay that way (`set_page_size`, `ppi`, `new_blank`); they are not part of the step.

### Sub-transactions: `with doc.transaction(name):`

An all-or-nothing block inside a script (`ScriptBlock`, bound as `curvy.Transaction`). `__enter__` notes `UndoStack::depth()`. Leaving
normally calls `mergeSince(mark, name)` (the block's edits become one step, which the run's own step then contains). Leaving with an
exception calls `UndoStack::rollbackTo(mark)`: the steps since the mark are undone and dropped (no redo entries remain) and the exception
carries on, so the script can catch it and continue with the document as it was before the block. `curvy.Cancelled` counts as an
exception, so a cancel inside a block takes that block back (edits made before it stay, per the run rule). A `doc.undo()`, `redo()` or
`new_blank()` inside a block moves the history under its mark; the block then merges or rolls back nothing rather than something wrong
(`mergeSince` and `rollbackTo` ignore a mark at or beyond the current depth). Not undone by a rollback: the non-undoable setters above.
`rollbackTo` was added to newui's `UndoStack` (carry to `D:\code\newui`).

## Values and conversions

- Colours are `(r, g, b, a)` tuples in 0..1 (`Rgba` in the source). `Swatch` is the C++ struct bound directly; palettes come back as copies, so
  editing a returned `Swatch` changes nothing in the document: use `replace_swatch`.
- `color_mode` maps to `ColorSpace` by name; only `'rgb'` and `'cmyk'` are accepted (the document ignores other spaces).
- `artwork()` returns the cooked `GeometryHandle` as a `jitnodes.Geometry` (read-only) or `None`. This needs the `jitnodes` module imported
  first so the type is registered; `ScriptConsole`'s constructor does that.
- Index and range errors on the C++ side are `std::runtime_error`/`out_of_range` and arrive in Python as `RuntimeError`/`IndexError`.

## Running code (`ScriptConsole::run`)

1. stdout and stderr are swapped for one `io.StringIO` for the duration of the run, and restored afterwards.
2. The code is compiled as an expression first; on `SyntaxError` it is compiled as statements. An expression whose value is not `None`
   prints its `repr`, like the interactive interpreter.
3. A Python exception or a C++ exception sets `ok = false` and `error` (the traceback text); `run` never throws for a script error.
4. Output is whatever the script printed, followed by the `repr` line.

Constraints: one `ScriptConsole` per process (CPython cannot be restarted), created and destroyed on the main thread, and scripts run on the
main thread. A long script blocks the UI (the panel does not repaint until it ends). Declaration order in `ScriptConsole::Impl` matters: the
scope (Python objects) must be destroyed before the interpreter.

### Cancel

Scripts run on the main thread, so the UI cannot take a Cancel click while one runs. Two triggers stop it instead:

- `ScriptConsole::cancel()`, callable from any thread (it only sets a flag, and only while a script runs, so a late call cannot hit the next run).
- The Escape key, polled by the watcher thread (`GetAsyncKeyState`, and the foreground window must belong to this process).
  `setCancelOnEscape(false)` turns it off.

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
- `tests/test_scriptmodule.cpp`: page settings, layers and groups, `add_circle` and `artwork`, nodes inside an object, palettes, gone objects.
- `tests/test_pyconsolepanel.cpp` and `tests/gui/test_curvy_pyconsole.py`: the panel (including the "running" line).
- `tests/test_scripteditorpanel.cpp`: the script editor (text round trip, save and load, BOM and CR handling, Run, Open and Save callbacks).
- `tests/test_scriptmodule.cpp` also covers `with doc.transaction():` (rollback on error, one step on success).

## The script editor panel

`ScriptEditorPanel` (`include/curvy/scripteditorpanel.h`, id `pyeditor`, title "Script", right dock, created next to the console panel in
`WorkspaceController`): a multi-line `TextControl` in a `ScrollView` above Open, Save and Run buttons. It knows no console: Run calls
`onRun(code, name)`, and the owner passes it to `PyConsolePanel::runScript`, which echoes `>>> run <name>` and shows the output in the
console's transcript (the input history is left alone). Open and Save ask for a path through `onChooseOpen` / `onChooseSave` (the native
file dialogs, `*.py`); files are UTF-8, a BOM and carriage returns are dropped on load. Not done: syntax highlighting, line numbers, a
"modified" marker, run-selection, and remembering the script across sessions.
- Run `curvyTests.exe` directly after a build and check that the test count includes the new tests (see CLAUDE.md, Build).

## Known limits and next steps

- Scripts block the main thread: cancel exists (see Cancel) but there is no timeout and no Cancel button in the panel (the window does not
  repaint while a script runs, so a button could not be clicked; Escape is the way). The "running... (Esc cancels)" line is painted before
  the script starts (`PyConsolePanel::onBeforeRun`, wired in `WorkspaceController` to `repaintNow` + `UpdateWindow`) and replaced by the
  result afterwards; nothing animates while it runs.
- No selection, shapes/primitives or point-level editing in the module yet; `artwork()` gives read-only data.
- Packaging stops at `cmake --install` (no installer or zip, no signing); see "Bundled Python".
