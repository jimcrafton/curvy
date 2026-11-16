# The `curvy` Python module (developer notes)

How scripts reach the document, and what to change when the C++ side grows. Background: `docs/py-binding-plan.md` (the generator and
jitnodes bindings) and `docs/py_scripting_plan.md` (the whole feature). User-facing behaviour of the console panel is in `PyConsolePanel`.

## Pieces

| File | Role |
|---|---|
| `src/scriptmodule.cpp`, `include/curvy/scriptmodule.h` | The `curvy` module: `Document`, `Object`, `Swatch`, `Palette`. Hand-written pybind11. |
| `src/scriptconsole.cpp`, `include/curvy/scriptconsole.h` | `ScriptConsole`: starts the interpreter, owns the one scope console input runs in, runs code with `doc` and `network` bound. |
| `src/pyconsolepanel.cpp`, `include/curvy/pyconsolepanel.h` | The right-dock panel that feeds `ScriptConsole::run`. |
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

Not done yet: a whole script as a single undo step (`with doc.transaction():`). Today a script that adds ten nodes makes ten steps.
Setters that the C++ side documents as not undoable stay that way (`set_page_size`, `ppi`, `new_blank`).

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
main thread. A long script blocks the UI; there is no cancel yet. Declaration order in `ScriptConsole::Impl` matters: the scope (Python
objects) must be destroyed before the interpreter.

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

- `tests/test_scriptconsole.cpp`: running code, output capture, errors, document lifetime, foreign-node refusal.
- `tests/test_scriptmodule.cpp`: page settings, layers and groups, `add_circle` and `artwork`, nodes inside an object, palettes, gone objects.
- `tests/test_pyconsolepanel.cpp` and `tests/gui/test_curvy_pyconsole.py`: the panel.
- Run `curvyTests.exe` directly after a build and check that the test count includes the new tests (see CLAUDE.md, Build).

## Known limits and next steps

- No script cancel or timeout; scripts block the main thread.
- No single-undo-step script (`doc.transaction()`).
- No selection, shapes/primitives or point-level editing in the module yet; `artwork()` gives read-only data.
- The Python path is baked into the executable (`CURVY_PYTHON_HOME`); a distributable build needs the bundled folder next to the exe.
