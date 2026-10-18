# curvy design notes

## Status
- `src/` and `include/curvy/` are the sketch demo (newui-demos/demos/sketch) ported as-is: namespace `curvy`, flat file names kept
  (`sketchdocument.h`, ...), `Resources/curvy.newui` is the old sketch layout.
- `3rdparty/newui` and `3rdparty/jitnodes` are git submodules.

## Build notes
- The project is C++17 throughout (newui's `delegate.h` does not compile as C++20). jitnodes upstream is C++20 but only used
  `std::bit_cast`; it builds as C++17 once that is replaced (`jitnodes/core/bitCast.h`).
- jitnodes' CMakeLists is not used: Blend2D embeds asmjit under a private ABI namespace, so jitnodes gets its own asmjit static build.
- reflectgen needs `3rdparty/newui/tools/reflectgen/.venv` (`python -m venv .venv`, `pip install -r requirements.txt`).

## Phases
1. Port the sketch demo and build it (done).
2. Use a jitnodes `Network` directly from the document code; cook artwork objects from it.
3. Map the document model (paths, shapes, strokes) onto graph nodes; hide the graph in the default UI, expose it in advanced mode.
4. Skeletal strokes (see `Skel Strokes.docx`).
5. Replace the sketch UI with new mockups converted through the `html-mockup-to-newui` skill.

## Phase 2: GraphDocument
`GraphDocument : newui::Document` owns a `jitnodes::Network`; `SketchDocument` derives from `GraphDocument`. The graph layer knows
nothing of sketch types (paths, layers, artboards), so it can be unit tested alone.

- **One undo stack.** `UndoStack` (newui) is moved into `GraphDocument` and shared. jitnodes' own `History` is not used: it is a
  second linear stack and could not interleave with sketch edits. Graph edits instead use
  `snapshotNetwork(net, &previous)` before and after the edit and push `{label, restoreSnapshot(after), restoreSnapshot(before)}`.
  Snapshots share the text of unchanged nodes and restore touches only the nodes that differ, so cooked caches survive undo.
- **Edit API (all one undo step each):** `addGraphNode`, `removeGraphNode`, `connectGraphNodes`, `setGraphParam`, `renameGraphNode`.
  A drag edits live and commits once, like `commitGeometry`.
- **Cooking.** `GraphDocument::cook(NodeId)` wraps `Network::cook`; `changed()` marks affected nodes dirty and repaints.
- **File format.** The document stays SVG. `saveNetwork()` text is embedded in a dedicated element (namespaced, ignored by other
  SVG readers); `GraphDocument` exposes `graphToText()` / `graphFromText()` and the SVG reader/writer call them. A load goes
  through `Batch`, and a failed `loadNetwork` leaves the old graph intact.
- **Phase 2 slice.** A graph-driven "live shape": a `SketchObject` holds a node id; its outline is the cooked polyline of that node
  (circle / star with parameters), drawn on the canvas, undoable. Geometry from jitnodes is polyline-only, so a `bezierPath` node
  with in/out handle attributes is a later addition.
- **Risks.** Node ids must stay stable across undo (restoreSnapshot preserves them); the Network must not be edited while cooking.
