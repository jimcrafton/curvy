# Implementation Plan: First-Class Python Scripting Architecture

**Stack:** C++20 · CMake 3.24+ · `pybind11` · `jitnodes` (SIMD Node Graph) · `newui` (Blend2D UI Framework)  
**Target:** Embedded, isolated CPython runtime with Pythonic node-graph bindings, zero-copy NumPy geometry buffers, custom `PythonNode` cooking, spare parameters, unified undo history, Safe Mode security, and an in-app REPL/Script Editor.

---

## Architecture Overview

```text
┌──────────────────────────────────────────────────────────────────────┐
│                        newui Application Layer                       │
│  ┌────────────────────┐  ┌────────────────────┐  ┌────────────────┐  │
│  │  Python Console &  │  │ Parameter Inspector │  │ Safe Mode UI   │  │
│  │  Script Editor     │  │ (Dynamic Widgets)  │  │ Banner & Audit │  │
│  └─────────┬──────────┘  └─────────┬──────────┘  └───────┬────────┘  │
│            │                       │                     │           │
│            ▼                       ▼                     ▼           │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │           DocumentHistoryBridge & Master AppUndoStack          │  │
│  └─────────────────────────┬──────────────────────────────────────┘  │
└────────────────────────────┼─────────────────────────────────────────┘
                             │
┌────────────────────────────┼─────────────────────────────────────────┐
│             pybind11 Embedded Module (`pyjitnodes` / `app`)          │
│  ┌────────────────────┐  ┌────────────────────┐  ┌────────────────┐  │
│  │  PyNode Proxy      │  │ Zero-Copy NumPy    │  │ PyTransaction  │  │
│  │  (`a >> b`, `.x`)  │  │ (`py::capsule`)    │  │ (`with` block) │  │
│  └─────────┬──────────┘  └─────────┬──────────┘  └───────┬────────┘  │
└────────────┼───────────────────────┼─────────────────────┼───────────┘
             │                       │                     │
             ▼                       ▼                     ▼
┌──────────────────────────────────────────────────────────────────────┐
│                         jitnodes Core Engine                         │
│  ┌──────────────┐  ┌──────────────────┐  ┌────────────────────────┐  │
│  │   Network    │  │  GeometryHandle  │  │      PythonNode        │  │
│  │  & History   │  │ (SIMD Buffers)   │  │ (GIL + Async Cancel)   │  │
│  └──────────────┘  └──────────────────┘  └────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Build System & Isolated Python Runtime
**Goal:** Vendor `pybind11` and package a relocatable, isolated CPython runtime so builds remain self-contained and end-users never depend on a host Python installation.

### Tasks
1. **Vendor `pybind11`:**
   - Add `pybind11` to `3rdparty/pybind11` alongside `newui`, `jitnodes`, `Blend2D`, `asmjit`, and `GoogleTest`.
   - Set `CMAKE_CXX_STANDARD 20` globally in the root `CMakeLists.txt` (compatible with `jitnodes` C++20, `newui` C++17, and `pybind11`).
2. **Bundle `python-build-standalone`:**
   - Download the `install_only_stripped` archive for target platforms (Windows x64, macOS arm64/x64, Linux x64).
   - Point CMake's `Python3_ROOT_DIR` to the bundled SDK directory.
   - Pre-install `numpy` into `python/Lib/site-packages` during the CMake install/package target.
3. **Configure Isolated Initialization (`PyConfig`):**
   - Replace default `py::scoped_interpreter` with `PyConfig_InitIsolatedConfig(&config)` + `Py_InitializeFromConfig(&config)`.
   - Explicitly set `config.home`, `config.executable` (pointing to bundled `python/python.exe` so `multiprocessing` never spawns duplicate GUI processes), and `config.module_search_paths` (bundled stdlib, `DLLs`, `site-packages`, app `scripts/`, and user `%APPDATA%/VectorApp/python_packages`).
   - On Windows, register `python/DLLs` and native package binary paths via `os.add_dll_directory()`.
   - Release the main thread GIL after initialization (`PyEval_SaveThread()`) so background worker threads can acquire it freely; ensure clean teardown (`Py_FinalizeEx()`) after all `py::object` instances are destroyed.

### Deliverables
- `CMakeLists.txt` updated with `pybind11::embed` and bundle copy rules.
- `src/scripting/PythonRuntime.h/.cpp` handling isolated startup, search paths, and shutdown.

---

## Phase 2: Core `pyjitnodes` Bindings & Zero-Copy NumPy
**Goal:** Expose `jitnodes` primitives as an idiomatic, memory-safe Python API usable both inside the GUI app and in headless CLI scripts.

### Tasks
1. **Safe Lifetime Wrappers:**
   - Wrap `jitnodes::Network` in `std::shared_ptr` (and `std::weak_ptr` inside handles) so closing a document in `newui` never leaves dangling C++ pointers in Python.
2. **Implement `PyNode` Proxy (`{std::weak_ptr<Network>, NodeId}`):**
   - Implement `__getattr__` and `__setattr__` for natural parameter access (`wr.a = 5.0`, `wr.code = "..."`) plus `__getitem__` / `__setitem__` (`wr["a"] = 5.0`).
   - Implement `__rshift__` (`>>`) returning the target `PyNode` for fluent graph wiring (`line >> wr >> render`), alongside explicit multi-port `.connect(target, out_port, in_port)`.
   - Validate `net->hasNode(id)` on every call and raise a clean `py::value_error` if the node was deleted.
3. **Zero-Copy `GeometryHandle` NumPy Views:**
   - Implement `get_point_float_attrib(geo, name)` returning `py::array_t<float>` backed directly by `jitnodes`'s contiguous SIMD float buffers.
   - Attach a `py::capsule` holding a copy of `jitnodes::GeometryHandle` to guarantee memory stays valid for the lifetime of the NumPy array.
   - Clear the `NPY_ARRAY_WRITEABLE_` flag on cached geometry arrays so Python scripts cannot corrupt upstream cook caches in place.
4. **Type Stubs (`.pyi`):**
   - Generate `.pyi` stub files during build (via `pybind11-stubgen` or reflection metadata export) for full autocomplete in VS Code / PyCharm.

### Deliverables
- `src/scripting/PyNodeBindings.cpp`
- `src/scripting/PyGeometryBindings.cpp`
- `stubs/pyjitnodes.pyi`

---

## Phase 3: Unified Undo History & Transaction Context Manager
**Goal:** Synchronize `jitnodes`'s differential `History` with `newui`'s master `UndoStack` and give Python scripts atomic, rollback-safe batching.

### Tasks
1. **Master-Slave Undo Adapter (`GraphHistoryCommand`):**
   - Designate `newui`'s `AppUndoStack` as the single master stack bound to `Ctrl+Z` / `Ctrl+Y`.
   - Create `GraphHistoryCommand` wrapping a single `jitnodes::History` step and triggering a `newui` graph/inspector/viewport refresh callback on `undo()` and `redo()`.
   - Enforce identical `max_steps` limits on both stacks and clear both simultaneously on `File -> New / Open`.
2. **Transaction Coordinator (`DocumentHistoryBridge`):**
   - Implement re-entrant `begin_transaction(label)`, `commit_transaction()`, and `rollback_transaction()` using a nesting depth counter (`tx_depth_`).
   - Suppress viewport invalidations while `tx_depth_ > 0` and trigger a single repaint when the outermost transaction commits.
   - Route standalone interactive REPL edits (`wr.a = 10` outside a `with` block) through `execute_atomic()` so single commands still produce one clean undo step.
3. **Python Context Manager (`with doc.transaction("Label"):`):**
   - Bind `PyTransaction` with `__enter__` and `__exit__`.
   - In `__exit__`, if `exc_type` is not `None` (including `KeyboardInterrupt` from script cancellation), automatically call `rollback_transaction()` to revert partial edits.

### Deliverables
- `src/app/GraphHistoryCommand.h/.cpp`
- `src/app/DocumentHistoryBridge.h/.cpp`
- `PyTransaction` bindings in `src/scripting/PyDocumentBindings.cpp`

---

## Phase 4: Custom `PythonNode` & Dynamic Spare Parameters
**Goal:** Allow users to author custom procedural nodes in Python with NumPy acceleration and dynamically generated `newui` parameter controls.

### Tasks
1. **Implement `PythonNode` (`jitnodes::Node` subclass):**
   - Clone upstream input `GeometryHandle` (Copy-on-Write) into a fresh mutable `Geometry` during `cook()`.
   - Expose `MutablePyGeometry` providing writable zero-copy `py::array_t<float>` views (`geo.attrib('x')`) and `resize_points()`.
   - If Python stashes a reference to a writable NumPy view globally (`out_geo.use_count() > 1`), clone `out_geo` before returning it to the graph cache so cached geometry remains immutable.
   - Cache compiled bytecode keyed by `std::hash<std::string>` of `"code"`, wrapped inside an isolated `def _jitnodes_eval(ctx):` function with a per-node `py::dict` namespace.
   - Catch `py::error_already_set` and surface tracebacks directly onto the node's `setError()` badge in `newui`.
2. **Dynamic Spare Parameters (`ParmSpec`):**
   - Add `std::vector<ParmSpec>` metadata (`name`, `label`, `ParmType`, `min_val`, `max_val`, `step`, `is_spare`) to `jitnodes::Node`.
   - Bind `node.add_float_parm()`, `node.add_int_parm()`, `node.add_string_parm()`, and `node.remove_parm()` on `PyNode`.
   - Update `saveNetwork` / `loadNetwork` / `saveAsset` to serialize `"spareParms"` before restoring `"parms"` values, and support promoting spare parameters when packaging subnets into `.asset.json`.
3. **Dynamic `newui` Parameter Inspector:**
   - Render `newui` sliders, spinboxes, toggles, and text fields dynamically from `node.parmSpecs()`.
   - Bracket slider drags with `begin_transaction` (mouse-down) and `commit_transaction` (mouse-up).

### Deliverables
- `src/nodes/PythonNode.h/.cpp`
- `ParmSpec` extensions in `jitnodes` + JSON serialization updates
- Dynamic inspector builder in `newui`

---

## Phase 5: Cancellation, Watchdog & Safe Mode Security
**Goal:** Prevent runaway scripts from freezing the app and protect users from untrusted `.json` / `.asset.json` files.

### Tasks
1. **Async Script Cancellation (`ScriptExecutionGuard`):**
   - Run Script Editor executions and heavy cooks on a worker thread while `newui` renders at full frame rate.



- Track PyThreadState_Get()->thread_id while executing Python; expose request_cancel() calling PyThreadState_SetAsyncExc(thread_id, PyExc_KeyboardInterrupt).

- Monkey-patch time.sleep at startup to sleep in 50ms chunks so blocking sleeps interrupt immediately.

- Add an optional configurable cook timeout (e.g., 5.0s) for PythonNode cooks.

2. **Safe Mode Trust System (****TrustManager****):**

o Note: jitnodes  wrangle nodes compile via asmjit with zero OS access and require no gating; gate only PythonNodes.

o On .json or .asset.json load, audit the network for PythonNodes with non-empty "code".

o Auto-allow files inside app examples//assets/ or user-configured Trusted Folders, or files matching a stored SHA-256 (canonical_path + combined_code) hash.

o Load untrusted files in ScriptTrustPolicy::SafeMode (passing upstream geometry through untouched in PythonNode::cook()), and show a non-modal newui banner with **Inspect Scripts**, **Allow Execution**, and **Trust Folder** buttons.

**Deliverables**

- src/scripting/ScriptExecutionGuard.h/.cpp

- src/security/TrustManager.h/.cpp

- Safe Mode banner & read-only code audit modal in newui

* * * * *

**Phase 6: In-App Python Console & Script Editor Widget (****newui****)**

**Goal:** Provide a built-in coding environment inside newui with real-time stdout/stderr capture and REPL interactivity.

**Tasks**

1. **Stream Redirection (****PyStreamRedirector**** & ****ConsoleBuffer****):**

o Bind _StreamRedirector (.write(), .flush(), .isatty(), .encoding) and assign instances to sys.stdout and sys.stderr.

o Coalesce consecutive chunks of the same stream type inside a mutex-protected ConsoleBuffer and drain them on newui's main runLoop tick.

2. **REPL & Script Execution Engine (****PythonConsoleEngine****):**

o Maintain a persistent __main__ dictionary pre-populated with live references (doc, net, app).

o Compile single-line REPL commands with Py_single_input (auto-printing non-None expression results) with automatic fallback to Py_file_input for statements/loops.

3. **Build ****newui**** Console Panel:**

o **Toolbar:**  Run (Ctrl+Enter), Stop (wired to ScriptExecutionGuard::request_cancel()), Clear, and a Target Node selector synced to the currently selected PythonNode/Wrangle node.

o **Editor Pane:** Monospace editor with 4-space Tab indentation.

o **Log View:** Scrollable Blend2D text log coloring stdout and stderr distinctly.

o **REPL Bar:** Single-line >>> input with Up/Down command history.

**Deliverables**

- src/scripting/PythonConsoleEngine.h/.cpp

- src/ui/PythonConsolePanel.h/.cpp

* * * * *

**Verification & Testing Plan**

|

**Test Suite**

 |

**Framework**

 |

**Coverage**

 |
|

**C++ Binding & Lifetime Tests**

 |

GoogleTest (CTest)

 |

Isolated PyConfig init, PyNode deleted-node exceptions, GeometryHandle  py::capsule ref-counting, History undo/redo sync

 |
|

**Python API & NumPy Tests**

 |

pytest (via CTest)

 |

node_a >> node_b chaining, zero-copy NumPy read/write buffers, with doc.transaction(): rollback on exception, spare parameter serialization round-trip

 |
|

**Security & Cancellation Tests**

 |

GoogleTest + pytest

 |

while True: pass interruption via PyThreadState_SetAsyncExc under 100ms; untrusted .json Safe Mode blocking & hash invalidation on edit

 |
|

**UI & Automation Smoke Tests**

 |

newui_automation

 |

Open Python Console panel, execute script via named-pipe driver, verify log output and viewport render

 |-   Track `PyThreadState_Get()->thread_id` while executing Python; expose `request_cancel()` calling `PyThreadState_SetAsyncExc(thread_id, PyExc_KeyboardInterrupt)`.
-   Monkey-patch `time.sleep` at startup to sleep in 50ms chunks so blocking sleeps interrupt immediately.
-   Add an optional configurable cook timeout (e.g., 5.0s) for `PythonNode` cooks.

1.  Safe Mode Trust System (`TrustManager`):
    -   Note: `jitnodes` `wrangle` nodes compile via `asmjit` with zero OS access and require no gating; gate only `PythonNode`s.
    -   On `.json` or `.asset.json` load, audit the network for `PythonNode`s with non-empty `"code"`.
    -   Auto-allow files inside app `examples/`/`assets/` or user-configured Trusted Folders, or files matching a stored SHA-256 `(canonical_path + combined_code)` hash.
    -   Load untrusted files in `ScriptTrustPolicy::SafeMode` (passing upstream geometry through untouched in `PythonNode::cook()`), and show a non-modal `newui` banner with `Inspect Scripts`, `Allow Execution`, and `Trust Folder` buttons.

Deliverables

-   `src/scripting/ScriptExecutionGuard.h/.cpp`
-   `src/security/TrustManager.h/.cpp`
-   Safe Mode banner & read-only code audit modal in `newui`

* * * * *

Phase 6: In-App Python Console & Script Editor Widget (`newui`)

Goal: Provide a built-in coding environment inside `newui` with real-time `stdout`/`stderr` capture and REPL interactivity.

Tasks

1.  Stream Redirection (`PyStreamRedirector` & `ConsoleBuffer`):
    -   Bind `_StreamRedirector` (`.write()`, `.flush()`, `.isatty()`, `.encoding`) and assign instances to `sys.stdout` and `sys.stderr`.
    -   Coalesce consecutive chunks of the same stream type inside a mutex-protected `ConsoleBuffer` and drain them on `newui`'s main `runLoop` tick.
2.  REPL & Script Execution Engine (`PythonConsoleEngine`):
    -   Maintain a persistent `__main__` dictionary pre-populated with live references (`doc`, `net`, `app`).
    -   Compile single-line REPL commands with `Py_single_input` (auto-printing non-`None` expression results) with automatic fallback to `Py_file_input` for statements/loops.
3.  Build `newui` Console Panel:
    -   Toolbar: `Run (Ctrl+Enter)`, `Stop` (wired to `ScriptExecutionGuard::request_cancel()`), `Clear`, and a `Target Node` selector synced to the currently selected `PythonNode`/`Wrangle` node.
    -   Editor Pane: Monospace editor with 4-space `Tab` indentation.
    -   Log View: Scrollable `Blend2D` text log coloring `stdout` and `stderr` distinctly.
    -   REPL Bar: Single-line `>>>` input with `Up`/`Down` command history.

Deliverables

-   `src/scripting/PythonConsoleEngine.h/.cpp`
-   `src/ui/PythonConsolePanel.h/.cpp`

* * * * *

Verification & Testing Plan

| Test Suite | Framework | Coverage |
| C++ Binding & Lifetime Tests | `GoogleTest` (`CTest`) | Isolated `PyConfig` init, `PyNode` deleted-node exceptions, `GeometryHandle` `py::capsule` ref-counting, `History` undo/redo sync |
| Python API & NumPy Tests | `pytest` (via `CTest`) | `node_a >> node_b` chaining, zero-copy NumPy read/write buffers, `with doc.transaction():` rollback on exception, spare parameter serialization round-trip |
| Security & Cancellation Tests | `GoogleTest` + `pytest` | `while True: pass` interruption via `PyThreadState_SetAsyncExc` under 100ms; untrusted `.json` Safe Mode blocking & hash invalidation on edit |
| UI & Automation Smoke Tests | `newui_automation` | Open Python Console panel, execute script via named-pipe driver, verify log output and viewport render |


---

## Suggested Implementation Order & Milestones

| Milestone | Scope | Exit Criteria |
| :--- | :--- | :--- |
| **M1: Headless Runtime & Core Bindings** | Phases 1 & 2 | Standalone `pyjitnodes` builds via CMake, passes `pytest` graph-wiring and zero-copy NumPy tests without a host Python install |
| **M2: Undo Sync & Custom `PythonNode`** | Phases 3 & 4 | `with doc.transaction():` groups edits into single `Ctrl+Z` steps; `PythonNode` cooks NumPy arrays and saves/loads spare parameters in `.json` |
| **M3: Console UI & Hardening** | Phases 5 & 6 | In-app `newui` REPL/Script Editor captures `stdout`/`stderr`, cancels infinite loops cleanly, and enforces Safe Mode on untrusted `.json` files |

---

## Pre-Release Checklist
- [ ] **Shutdown Order:** Verify all C++ holders of `py::object` / `py::function` (compiled node functions, console globals, stream redirectors) are reset **before** `Py_FinalizeEx()` runs on app exit.
- [ ] **No Host Leakage:** Test on a clean VM (and a machine with Anaconda installed) to confirm `sys.path` only resolves bundled and user-package directories.
- [ ] **Headless CLI Parity:** Ensure `jitnodesCli` can load and cook graphs containing `PythonNode`s when compiled with scripting enabled.
- [ ] **Memory Stability:** Run a 1,000-frame timeline cook loop with a `PythonNode` + NumPy views under AddressSanitizer / leak detector to confirm zero `GeometryHandle` leaks.
