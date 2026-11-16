# Python binding plan (scriptgen / pybind11)

Status: planned, nothing implemented yet (2026-11-15). Builds on `docs/py_scripting_plan.md` (the architecture and phases for the whole scripting feature); this document covers only the binding generator and the first foundations. The hand-written `curvy` module and the console are described in `docs/py-curvy-module.md`.

## Goal

- Refactor reflectgen's parsing half into a reusable library, `reflectgen_lib`.
- Add a second generator, `scriptgen`, that emits pybind11 bindings (and `.pyi` stubs) for classes marked `// @scriptable`.
- Use it in jitnodes first, then lay the foundation in curvy.

## Findings that shape the design

1. reflectgen is a Python tool (libclang through the `clang` pip package in `newui/tools/reflectgen/.venv`), not C++. `reflectgen_lib` is therefore a Python package. The parse half of `reflectgen.py` (about 1,700 of 2,550 lines) moves into it.
2. reflectgen's annotation parser only knows `@reflect key=value`, and reflectgen scans every header by default. `@scriptable` is a separate marker with its own filter, so one comment line can carry both.
3. jitnodes stores node parameters in string-keyed maps (`setFloat("a", 5)`) that nodes declare at run time, so they cannot be generated from headers. Static classes (`Network`, `Node`, `Geometry`, `History`) are generated; parameter access is hand-written once on `PyNode` (`__getattr__`, `__setattr__`, `[]`).
4. `jitnodes::Network` is non-copyable and not owned by a `shared_ptr`. Handles need a liveness token: `std::shared_ptr<void> aliveToken_` on `Network`, held weakly by `PyNode`/`PyNetworkRef`.
5. pybind11 supports C++17, so no global C++20 change is needed. Curvy stays on 17; jitnodes is already on 20.
6. NumPy views of `AlignedArray<float>` cannot be generated; they are hand-written.

## Step 1: reflectgen_lib (behavior preserving)

`newui/tools/reflectgen_lib/`:

| Module | Contents |
|---|---|
| `annotations.py` | `@reflect` and `@scriptable` parsing, key vocabulary |
| `model.py` | `ClassInfo`, `Field`, `Method`, `Ctor`, `BaseInfo`, `PropertyAccessor`, `CollectionAccessor`, `EnumInfo` |
| `clangutil.py` | libclang setup, type spelling, `qualified_name` |
| `collect.py` | `collect_class`, `collect_enum`, `find_declarations` |
| `driver.py` | shared argparse, input expansion, include spelling, `parse_headers()` |

`reflectgen.py` keeps only its `emit_*` and `generate`. Acceptance: generated output for newui and curvy is byte-identical to before, and `tools/reflectgen/unittests` still pass.

## Step 2: scriptgen

`newui/tools/scriptgen/scriptgen.py`. Parses with the lib, keeps classes and enums marked `// @scriptable`, emits:

- `<target>_scripting_generated.cpp` with `void registerXScripting(pybind11::module_&)`: `py::class_<T, Base, Holder>`, `.def`, `.def_property`, `.def_readwrite`, `py::enum_`, constructors.
- `<target>.pyi` stubs (scriptgen already has the types, so pybind11-stubgen is not needed).

Annotation keys: `@scriptable name=Foo`, `holder=shared_ptr`, `ignore=true` (member), `name=...` (method), `release_gil` (method), `policy=reference_internal`.

Naming: camelCase to snake_case by default (`setFloat` becomes `set_float`); `name=` overrides.

Dependencies (the "unless a dependency is determined" rule):

- Bases of a scriptable class are always registered (pybind requires it).
- Enums used in a scriptable signature are registered automatically.
- Other unmarked types in signatures become opaque `py::class_` with no members, so signatures still work.
- A method that cannot be bound (raw `T**`, `std::function`, templates) is skipped with a warning; `--strict` makes it an error.

Return policy: `const T&` is `reference_internal`, `unique_ptr` is a transferred holder, `shared_ptr` is kept, raw pointers are copied unless annotated. Overloads use the `static_cast<R(C::*)(A)>` form reflectgen already emits.

CMake: `cmake/ScriptGen.cmake` provides `newui_add_scriptgen_output(<target> SCAN_DIRS ... INCLUDE_DIRS ... MODULE <name> REGISTER_FUNCTION ...)`, mirroring `newui_add_reflectgen_output` and reusing the same `.venv`.

## Step 3: jitnodes

Edit only `3rdparty/jitnodes`; carry the edits to `D:\code\jitnodes`.

- Mark `Network`, `Node`, `Geometry`, `History`, `Keyframe`, `AttribClass`, `AttribType` as `@scriptable`. Add the alive token to `Network`.
- Hand-written: `PythonRuntime` (isolated `PyConfig`, `PYBIND11_EMBEDDED_MODULE(jitnodes, m)`), `PyNode` proxy, `PyGeometryView` (read-only zero-copy arrays, capsule holds the `GeometryHandle`), `PyTransaction` (`with` blocks over `History::commit`).
- Behind `JITNODES_BUILD_PYTHON` (default OFF); the core `jitnodes` library gets no Python dependency.
- Tests: gtest with the embedded interpreter, plus a few `.py` scripts. Include a micro-benchmark (a million `set_float` calls against one NumPy bulk write).

## Step 4: curvy foundation

- Add pybind11 and Python3 to the root CMake; a `curvy` module generated from `@scriptable` document-model classes (`SketchDocument`, `SelectionModel`, shape/prim/point types), importing the jitnodes module and an `app`/`doc` object.
- One `ScriptTransaction` makes a script a single undo step. Curvy's undo path needs a look before this is designed.
- Deferred (as in `py_scripting_plan.md`): console panel, safe mode, `PythonNode` cook, runtime bundling.

## Core classes

| Class | Where | Role |
|---|---|---|
| `ClassInfo`/`Method`/`EnumInfo` (existing) | reflectgen_lib | parse model |
| `ScriptableFilter`, `NameMapper`, `TypeBinder` | scriptgen | marker filter, snake_case, C++ to pybind type spelling |
| `DependencyResolver` | scriptgen | pulls in bases/enums, opaque classes, warnings |
| `BindingEmitter`, `StubEmitter` | scriptgen | `.cpp` and `.pyi` output |
| `PythonRuntime` | jitnodes, curvy | interpreter init, GIL release, teardown before `Py_Finalize` |
| `PyNode`, `PyNetworkRef` | jitnodes | liveness-checked handles |
| `PyGeometryView` | jitnodes | zero-copy read-only arrays |
| `ScriptTransaction` | jitnodes, curvy | `with` blocks as one undo step |

## Performance (jitnodes)

- The cook path (SIMD kernels, asmjit fusion, thread pool) never calls Python; the bindings are a separate library. The only core change is the alive token, touched at construction and teardown only.
- Binding calls cost roughly 100 to 300 ns each (typical pybind11 figure, not measured here); bulk work goes through the NumPy views, so `Geometry` gets no per-element accessors.
- The real cost is a later `PythonNode`: cooks run under the GIL and break kernel fusion for their chain. Wrangle stays the fast path; a cook timeout limits stalls.

## GIL

No released CPython removes the GIL by default. 3.13 has an experimental free-threaded build; 3.14 supports it officially but as an optional, separate build (PEP 703, PEP 779). Plan on a normal GIL build; keep binding code free of global mutable state so free-threaded (`py::mod_gil_not_used()`, NumPy support needed) stays a later packaging switch.

## Downloads

- pybind11: header-only, source release from `github.com/pybind/pybind11/releases` (2.13.x or newer) into `3rdparty/pybind11/`. Not shipped.
- Python runtime: python-build-standalone (`github.com/astral-sh/python-build-standalone/releases`), file `cpython-3.13.<patch>+<date>-x86_64-pc-windows-msvc-install_only_stripped.tar.gz`, not the free-threaded variant. Extract to `3rdparty/python/` (gitignored). Build against its `include/` and `libs/python313.lib`; ship the whole `python/` folder next to `curvy.exe`.
- NumPy (optional, for the geometry views): `3rdparty/python/python.exe -m pip install numpy --target 3rdparty/python/Lib/site-packages`.
- Downloaded and checked 2026-11-15: `3rdparty/pybind11/` (3.1.0) and `3rdparty/python/` (3.13.18 x64, normal GIL build, `python313.dll`/`python313.lib`). The free-threaded build of the same version is in `3rdparty/python-freethreaded/` (`python313t.dll`/`python313t.lib`, `Py_GIL_DISABLED` = 1); it is not used yet and must stay in its own folder. Both `python` folders are downloads and should be gitignored.
- For development builds the system Python 3.13.1 works; do not ship it.
- The `x64-debug` preset links the release `python313.lib`; that is normal.

File names above are from memory; check the release pages before downloading.

## Risks

- libclang may not parse some jitnodes headers standalone (as happened with newui's scan-all); budget time for fixes.
- Parity after the reflectgen refactor is the largest regression risk, hence step 1 first with a byte-identical check.
- Worker-thread cooks need the GIL once `PythonNode` exists.
- The reflectgen `.venv` path is hard-coded to `Scripts/python.exe` (Windows only), as it already is.

## Defaults assumed (change if wrong)

- `@scriptable` is separate from `@reflect`.
- Python names are snake_case.
- `reflectgen_lib` is a Python package under `newui/tools/`.
- newui edits are made in `3rdparty/newui` and carried to `D:\code\newui`.

## Progress

- Step 1 done (2026-11-15): `3rdparty/newui/tools/reflectgen_lib/` holds `parser.py` (libclang setup, annotations, model, collection, `find_declarations`) and `driver.py` (`parse_headers`, `expand_inputs`, `include_spelling`, `enclosing_namespace`, clang argument setup); `reflectgen.py` keeps the emitter and re-exports the library names. Generated output for newui and curvy is byte-identical, the 109 reflectgen unit tests pass, and the CMake generation steps run (`NEWUI_REFLECTGEN_LIB_DIR` added to `cmake/ReflectGen.cmake` and the root `CMakeLists.txt`). Not yet carried to `D:\code\newui`.
- Step 2 done (2026-11-15): `3rdparty/newui/tools/scriptgen/` (`scriptgen.py` emitter and CLI, `scriptcollect.py` collector, `README.md`, `unittests/`) and `cmake/ScriptGen.cmake` (`newui_add_scriptgen_output`). The shared library gained `marker_annotations()` and the `translation_unit()` context manager; reflectgen output is still byte-identical. Marker vocabulary and limits are in the scriptgen README. Verified by 15 unit tests plus a hand build of the generated sample bindings (MSVC, pybind11 3.1, Python 3.13) that imports and passes its checks, and a build through the CMake function.
- Step 3 started (2026-11-15): `// @scriptable` markers added to the jitnodes headers (comments only, in `3rdparty/jitnodes`): classes `Network`, `Node` (shared holder), `Geometry` (shared holder), `History`, struct `Keyframe`, enums `AttribClass`, `AttribType`, `PrimKind`, `KeyInterp`; `// @scriptable ignore=true` on the internals `AudioStep`, `ThreadPool`, `AttribArray`, `Primitive`, `ShapeRange`, `CookContext`, `NodeStats`, `PointKernel`, `NodeWriter`, `NodeReader`, `RestoreStats`, `KernelCompiler` and on `Geometry::pointAttrib/findPointAttrib/x/y`, `Network::replaceNode/touchedSince` (the raw-pointer ones are for the hand-written NumPy views). scriptgen run over `include/jitnodes` with `-std=c++20`: no warnings under `--strict`, 5 classes, 4 enums. scriptgen gained: marked and hidden enums/classes, no constructors for abstract classes (fixed a compile error on `Node`), snake_case parameter names, const/non-const twin merging. Verified: the generated file compiles (MSVC, C++20) and, linked against the built `jitnodes.lib` and `jitnodesAsmjit.lib`, a Python script builds a line/wrangle/null graph, cooks it (8 points), edits parameters, uses `History` commit/undo, catches the cycle error, and sets a `Keyframe` - all pass. This used a hand-compiled module in the scratchpad (`jit/`), not yet the jitnodes CMake.
- Handle layer built (2026-11-15, option D chosen over shared ownership, see the comparison in the conversation: undo and `remove_node` replace node objects, so sharing the `Network` alone cannot make raw `Node&` safe). In `3rdparty/jitnodes`: `Network::lifetime()` (`weak_ptr<void>` from a `shared_ptr<void>` member; a subnet's inner network has its own, so it expires with the subnet node), `python/pyHandles.{h,cpp}` (`NetworkRef`, `PyNetwork`, `PyNode`, `PyHistory`, `registerModule`, `wrapNetwork`), `python/pyModule.cpp`, `python/CMakeLists.txt` and option `JITNODES_BUILD_PYTHON`, `python/README.md`. The generated classes live in `jitnodes._core`; scripts see handles (id + token, checked on every call, `StaleHandleError` which is a `ReferenceError`) that forward to them by name, turning handle arguments into ids and returned ids into handles; `__getattr__`/`__setattr__` read and write node parameters (an unknown name is an error), `a >> b` wires, `Network` is iterable and indexable by name or id. `release_gil` is marked on `Network::cook/cookDisplay/freezeNode` (progress callbacks run on worker threads).
- Verified: jitnodesTests 232 pass (2 new for the lifetime token, in jitnodes' own build and in curvy's C++17 build of it); `jitnodesPythonScripts` 26 tests pass (handles, parameters, wiring, stale after remove/undo, kept bound methods checked at call time, History, keyframes, cook on another thread with a progress callback and 4 pool threads); `jitnodesPythonEmbedded` 11 checks pass (an application-owned network: handles do not keep it alive and go stale when it is destroyed). Built in `D:/code/curvy/out/jitpy` with `-DJITNODES_BUILD_PYTHON=ON -DJITNODES_BUILD_EXAMPLES=OFF -DJITNODES_BUILD_TOOLS=OFF` (Ninja, vcvars64). Gotcha found: `wrapNetwork` failed with "Unregistered type" until it imported the `jitnodes` module itself.
- Lifetime test round (2026-11-15 night). Added tests for the gaps found in review; they found and fixed three real bugs: (1) `Network::clear()` restarts ids and uids, so a handle to the old node 1 silently became the new node 1: handles now remember `Network::generation()` (new, bumped by `clear()`); (2) a node handle from another network passed to a `Network` method was converted to its id and used in the wrong network: now `ValueError`; (3) a script could change a cooked `Geometry` that the node cache shares (`g.resize_points(3)` corrupted the next `cook`): scriptgen got a class key `readonly` (only const/static methods, read-only fields) and `Geometry` uses it. Also `PyNetwork.__contains__` now checks generation. Test totals: jitnodesTests 233, Python 34, embedded 27 checks (script-created networks are destroyed when the last handle/bound method/history goes, shown with a counting node type; 200 create-and-drop rounds destroy all 1000 nodes; 300 app-owned networks, 296 at the previous address, none revives an old handle; history on a destroyed network; shutdown with live handles). Still untested by design: threads (documented), raw `_core` objects, ASAN/leak-detector runs.
- AddressSanitizer run (2026-11-15): built `out/jitpy-asan` (RelWithDebInfo, `-DJITNODES_SANITIZE=ON`; the generated bindings file needed `/bigobj`, now set in `python/CMakeLists.txt`). Embedded tests (27 checks), `jitnodesTests` (233) and the 34 Python tests against the instrumented module (needs `os.add_dll_directory` for the ASAN runtime) all ran with no reports. Limits: MSVC ASAN has no leak detector (the counting probe node covers destruction), `python.exe` itself is not instrumented, and ASAN's quarantine hides address reuse (0 of 300 versus 296 of 300 in the normal build), so that test stays in the normal build. Recipe is in `jitnodes/docs/python.md`.
- Documentation written: `newui/tools/scriptgen/README.md` (calling it, marker vocabulary, how it works, how it uses pybind11, what it emits, limitations) and `jitnodes/docs/python.md` (how the binding was built, what was added to jitnodes, object lifetime, the Python API, embedding, building and testing, limitations); `jitnodes/python/README.md` points to it and `jitnodes/README.md` links it.
- NumPy read-only views done (2026-11-16): NumPy 2.5.5 installed into `3rdparty/python/Lib/site-packages` (user approved). `python/pyArrays.cpp` adds `Geometry.point_attrib/prim_attrib/attrib/attrib_names/attrib_type/strings/string_values`; zero-copy, `writeable=False`, base capsule holds the `shared_ptr<const Geometry>`; without NumPy the module still imports and the call raises ImportError. 4 new Python tests (read-only, shared storage, array outlives geometry and network, names and errors): Python 38, jitnodesTests 233 and embedded pass. ASAN not re-run for the views.
- Handle stubs done (2026-11-16): `python/genHandleStubs.py` (run as a POST_BUILD step of `jitnodesPython`) writes `jitnodes/__init__.pyi` from the generated `_core.pyi`: Network/Node/History/Geometry with node results as `Node`, `Node | int` arguments, `Any` parameters, and the NumPy methods; `tests/test_stubs.py` (5 tests) checks every declared member exists at runtime. Python tests now 49. The result and argument tables in the script mirror `pyHandles.cpp` and must be kept in step by hand.
- PythonRuntime done (2026-11-16): `python/pyRuntime.{h,cpp}` in `jitnodesPython`. Isolated `PyConfig` (home + Lib, DLLs, site-packages + extra search paths; no env vars, no site, no signal handlers, `sys.executable` = bundled python), registers the `jitnodes` embedded module itself (the standalone .pyd does not pull that file in), `newScope()`, `addSearchPath()`, `version()`; the creating thread keeps the GIL (thread model option D); one runtime per process (a second throws); bad home throws with a reason. The embedded test now uses it (no `PYTHONHOME`, home passed as argument) and checks isolation (PYTHONPATH ignored, no system paths, no site), stdlib, NumPy, the built-in module, an added search path, the second-runtime and bad-home errors: 39 checks, ASAN clean. Not done: a script console with stdout capture and cancel, calling from worker threads.
- Still to do (not started): wire the python target into curvy's own CMake (curvy builds jitnodes through `cmake/jitnodes.cmake`, not jitnodes' CMakeLists), undo transactions (`with doc.transaction()`), the curvy module (`@scriptable` on `SketchDocument` etc.), and the later phases of `py_scripting_plan.md`.
- Not yet carried to `D:\code\newui`: `tools/scriptgen`, `cmake/ScriptGen.cmake`, `tools/reflectgen_lib`, `tools/reflectgen/reflectgen.py` and README, `cmake/ReflectGen.cmake`.
- `3rdparty/pybind11` is now the full 3.1.0 tree (checked 2026-11-15): `add_subdirectory`, `pybind11_add_module` and `pybind11::embed` all build against it. CMake needs these settings before `add_subdirectory(3rdparty/pybind11)` or it picks the system Python (3.13.1) instead of the bundled one: `set(PYBIND11_FINDPYTHON ON)`, `set(Python_ROOT_DIR <3rdparty/python>)`, `set(Python_FIND_STRATEGY LOCATION)`, `set(Python_FIND_REGISTRY NEVER)`, `set(Python_FIND_VIRTUALENV ONLY)`. Verified: it then reports 3.13.18 with `python313.lib` from `3rdparty/python/libs`. An embedded executable also needs `python313.dll` next to it (or the whole `python/` folder, with `PyConfig.home` set, or it prints "Could not find platform independent libraries").
- The parser was moved as one `parser.py` rather than split into annotations/model/collect modules; the split can follow when scriptgen needs the pieces separately.

## Open

- Steps 1 to 3 are done (see Progress). Proposed next, not yet started: (1) read-only NumPy views of point, primitive and detail attributes (zero-copy, non-writeable, holding the `shared_ptr<const Geometry>`; needs `numpy` installed into `3rdparty/python`, only at run time); (2) type stubs for the handle classes; (3) `PythonRuntime` (isolated `PyConfig`); (4) optionally bind `SubnetNode`/`AssetNode`. Then wire the Python target into curvy's CMake and add the curvy module and a console. Deferred until after curvy: mutable NumPy geometry and a `PythonNode` (writable views dangle on resize, and it needs the thread model, trust/Safe Mode and spare-parameter serialization decided), undo transactions (need curvy's master undo stack), and a thread model beyond one thread per network at a time (options: keep the GIL during cook, a lock in `Network`, marshal to the main thread, or run scripts on the main thread; recommended for curvy first: main thread with cancel).
