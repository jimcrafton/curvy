# Native file format: `.curvy` (design note, 2026-11-06)

**Version 2 (2026-11-08, N3): the file has no `objects` section.** The writer emits `document` (page, guides, layers, artboards, swatches, graphicStyles), `resources`
(symbols, patterns, brushes, and `images` each with `name`, `key` = `ImageData::key`, and base64 `data`) and `graph`; the reader builds the objects from the graph with
`SketchDocument::objectsFromGraph`, finding pictures by `key`. Group opacity, blend and clip live on the group subnets, so there is no `groups` section either. Version 1
files (with `objects`) still open through the old reader path; `tests/data/skyDreamerV1.curvy` keeps one as a fixture. Looks are floats in the file (a saved opacity of
0.37 reads back as the float 0.37). There are no generator-backed live shapes any more (see `docs/node-only-plan.md`), so every object has a shape node that holds its name and flags. The sections
below describe version 1 where they mention `objects`. Plan: `docs/node-only-plan.md` (tracker DG-17 to DG-20).

Status (2026-11-07): **F1-F4, F3 slice 2 and F6 built; F5 (SVG export without the graph) waits for the import/export branch.** `Resources/samples/skyDreamer.curvy` is written by
`SampleArt.DISABLED_WriteSkyDreamer` next to the SVG (kept) and checked to open, keep its sparkle expressions and resave identically. The text below is the original design. Decided with the user on 2026-11-06: curvy gets its own native format, text, JSON5; SVG stays as an import and export option and is no longer the
way a document is saved.

## Why

Today a document is saved as SVG (`svgFromDocument`) with the node graph tucked into `<metadata><jitnodes:graph data="..."/></metadata>`, or as `.skb` (binary; version 4 added a `GRPH`
chunk holding the graph text). Problems with SVG as the native file:

- The graph is JSON already, but it sits inside an XML attribute: every quote is `&quot;`. In `Resources/samples/skyDreamer.svg` (697 KB, 183 objects) the graph is 441 KB on one
  line of 440,928 characters. It cannot be read, diffed or merged by a person.
- The picture (paths, styles) and the graph say the same thing twice; on load the importer builds plain paths and then matches them back to nodes (`syncPathNodes`,
  `bindLoadedNode`). That matching exists only because the file is not the graph.
- Anything that rewrites the SVG and drops `<metadata>` silently turns the document into dead paths.
- The SVG importer ignores `<style>` and classes by design, so a file only round-trips if curvy wrote it.

## The format

One UTF-8 text file, JSON5, extension `.curvy`. JSON5 because newui already parses it for `.newui` files, it allows unquoted keys, comments and trailing commas, and a person can
open it. Writers emit it deterministically (sorted keys where the model is a map, fixed order otherwise, two-space indent, **one node per line**) so a version-control diff of
two saves shows only what changed.

```json5
{
  format: "curvy",
  version: 1,
  document: {
    page: { width: 800, height: 600, ppi: 72, colorMode: "rgb", background: "#ffffff" },
    artboards: [ /* ... */ ], activeArtboard: 0,
    activeLayer: 1, activeTag: "tag1",
    guides: [ /* ... */ ],
    swatches: [ "#ff0000", /* ... */ ],
    graphicStyles: [ /* ... */ ],
    defaultStrokeWidth: 1.5,
    origin: [0.5, 0.5],
    time: 0, fps: 24,
  },
  resources: { symbols: [], patterns: [], brushes: [] },   // each entry a nested document, same shape as this one
  graph: {                                                  // jitnodes network as an object, not a string
    nodes: [
      { id: 1, type: "curvySubnet", name: "Sky_hair", /* floats, strings, arrays, inputs, nodes */ },
    ],
  },
  objects: [ /* per-object data the graph does not hold; see "Open: what lives outside the graph" */ ],
}
```

Rules:

- **The graph is stored as data, not as text in a string.** `jitnodes::saveNetwork` already produces JSON; the native writer needs it as a value (or splices its output in
  unescaped) and the reader needs `loadNetwork` to take a parsed value, or the raw slice of the file. This is the only jitnodes change in the plan.
- **No pictures.** Outlines, placement and paint come from cooking the graph, exactly as after an SVG load today. Nothing is stored twice.
- **Numbers keep the precision the model holds:** six significant digits for outline coordinates (what `pathnode.cpp` and the SVG and `.skb` files already keep), floats as
  floats, ids as integers.
- **Unknown keys are ignored and kept out of the way;** `version` rises only when a reader of an older version would misread the file. A reader refuses a file with a higher major
  version with a message rather than guessing.
- **Compression is optional and sniffed by content,** as `.svgz` is: a gzip stream is inflated first. An extension for it (`.curvyz`) is not needed to read, only to name.
- **Saving writes a temporary file beside the target and renames it.** `writeToFile` today truncates the destination first, so a crash or full disk mid-save destroys the
  previous version; a text format that people keep under version control deserves better.

## Open: what lives outside the graph

The graph is the storage model for an object's outline, placement and paint, for layers and groups (subnets), and for live shape parameters. `SketchDocument` still holds, outside
it: `objects_` (names, flags, text state, style indices, the frame angles of the selection), `pageBackground_`, the artboards, `guides_`, `swatches_`, `graphicStyles_`, `brushes_`,
`symbols_`, `patterns_`, `defaultStrokeWidth_`, the origin and the active layer, brush, symbol and pattern. `.skb` version 4 stores those in `PAGE`, `STRS`, `STYL`, `GRPS`, `GSTY`
and `OBJS` chunks, plus the object-to-shape-node ids in `GRPH`.

**Not yet verified:** exactly which per-object fields are not recoverable from the cooked graph. Step F1 below is that audit, and the `objects` table in the file is whatever it
finds, nothing more. The aim is that the table shrinks over time as the migration in `graph-migration-roadmap.md` moves more into nodes; the roadmap's note that
`syncPathNodes` / `bindLoadedNode` stay "until a file format that stores only the graph" becomes actionable once `.curvy` carries the object-to-node mapping.

### F1 result (2026-11-06, read from the headers, `sketchbin.cpp` chunk list and `dg03-node-truth.md`; not yet proven by a test)

`.skb` v4 writes these chunks: `PAGE GUID LAYR SWCH STRS STYL GRPS CLIP SYMB PATN BRSH IMGS ABRD GSTY GRPH OBJS`.

| Data | Where it lives now | In `.curvy` |
|---|---|---|
| Outline, placement matrix, paint warp | Nodes (`curvyPath` / `curvyShape`, `curvyTransform`) | `graph` only |
| Text words, font, size, style, warp; symbol and image references | String parameters of the shape node (mirrored, DG-03) | `graph`; the definitions they point to go in `resources` |
| Fills, strokes, effects (appearance) | Appearance nodes | `graph` |
| Layers and groups: order, visibility, lock, opacity, blend, clip | Subnet floats and strings (`layerId`, `layerOrder`, `groupId`, `visible`, `locked`, `opacity`, `blend`, `clip`) | `graph` (the `LAYR` / `GRPS` / `CLIP` chunks become redundant once the probe below passes) |
| Parametric shape numbers, expressions, keys, time, fps | Nodes and network | `graph` |
| Object name, parent link, `activeFill` / `activeStroke` / `activeEffect` (which row of the appearance is current), `frameAngle_` | **Object only** (the roadmap lists `frameAngle_` as left outside the graph on purpose) | `objects` table |
| Page size, ppi, colour mode, background; artboards; guides; swatches; graphic styles; default stroke width; origin; active layer, brush, symbol, pattern | **Document only** | `document` |
| Symbol, pattern and brush definitions; image pixels | **Document only** (`symbols_`, `patterns_`, `brushes_`, `ImageData`) | `resources` (images need a decision, see "Decisions still open") |

**Probe run (2026-11-06, `tests/graphprobe.cpp`, `GraphProbe.*`, on `Resources/samples/skyDreamer.svg`: 183 objects, 5 layers):**

- Loading the graph text alone into an empty document gives **0 objects** (of 183). There is no way yet to build objects from a graph, so the file needs either the object
  table or a new "objects from graph" builder in F3; the `.skb` v4 loader also relies on the file listing the objects and their shape node ids.
- The graph text itself round-trips exactly through `graphFromText` / `graphToText`.
- Every one of the 183 objects has its shape node, and an object's layer follows from the scope its node is in (183 of 183).
- **The object's name is not in its shape node** (0 of 183 shape nodes carry the name; all 183 objects are named). So the name is outside the graph today; cheapest fix is to
  write it to the shape node's name when the object is named, which removes it from the object table.
- The layer count after a graph-only load was 1 of 5: the layer table is rebuilt by the document's sync (`pullLayerNodes`), which `graphFromText` does not run, so this one
  says nothing about the graph; the F3 loader must run that sync and F3's tests must check it.
- **Not exercised** because the sample has none: text, symbols, images, parent links, a non-zero `frameAngle`, groups, guides, swatches, custom graphic styles, brushes. The
  probe prints zero for those; F3's round-trip tests must add a document with one of each before the table can be called final.
- 183 of 183 objects have more than one appearance row. **Third probe (2026-11-07, `GraphProbe.TheSamplesLookNodesHoldTheLooks`, `EveryKindOfLookSurvivesItsNodes`):** every look
  node is turned back into an item with `itemFrom` (the call the live sync uses to adopt a node's edits, from a default item of the node's kind) and compared field by field with
  the stored item. Sample: 427 rows (183 fills, 183 strokes, 61 effects), chain length and node type right for all, **0 rows lose a field**; 286 come back exactly and 141 differ only
  by a float step (the nodes hold floats, the items doubles). A document built to use every look feature (solid with cmyk and alpha, linear / radial with explicit axis, aspect
  and skew, point gradients, along-stroke, patterns, hidden rows, dashes, caps, joins, miter, variable width, brush, inside / outside, both arrowheads and scale, all seven
  effects with odd tint / softness / offset): 25 rows, 0 lost, 17 exact, 8 to a float step. So the appearance nodes hold every appearance field; what approach B changes is
  precision (an opacity of 0.37 comes back as the float 0.37), which makes the byte-identical `.skb` oracle fail for those rows, so B's check must compare to a float step.
  **Not probed:** the active fill / stroke / effect row, the object's own opacity and blend mode, and a look parameter driven by an expression (the probe reads base values).

**Second probe (2026-11-06, `GraphProbe.BareObjectsBoundToTheLoadedGraphRecoverWhatTheNodesHold`):** load the graph into an empty document, add one bare object per original
(type and geometry only), each bound to its node (`setGraphNode` / `setGraphRef`, as the `.skb` loader does), and compare. Of 183:

| Field | Recovered |
|---|---|
| Layer, geometry bounds | 183 (layer from the scope, geometry from the shape node) |
| Opacity, blend, visible, locked | 183, but the sample has only default values for them, so this proves nothing yet |
| Appearance rows (fills, strokes, effects) | **0**: a bound object keeps its default look. `syncAppearanceNodes` treats an object with no `appearanceSynced_` record as the one that is right and writes its look *over* the loaded nodes. |
| Active fill / stroke row | 122 (the rest differ; they are object state) |

So the appearance nodes are in the graph, but nothing adopts them on load. There are two ways to a file that does not store the looks twice:

- **A. Store the looks in the file** (the `.skb` rule: the object wins, the sync then rewrites the nodes). Safe, same semantics as today, independent of whether the appearance nodes
  hold everything (strokes with variable width, pattern and brush references, point gradients). Costs: the look is in the file twice, in the object table and in the graph.
- **B. Adopt the looks from the nodes on load** (a document method that fills `appearanceSynced_` from the nodes for bound objects). Smaller file, one truth. Costs: a change in
  `SketchDocument` internals, and it is only correct if the appearance nodes hold every appearance field, which is not yet proven.

**Decision: A first, B as a later shrink.** A is checked by an oracle: a document loaded from `.curvy` must write the same uncompressed `.skb` image as the original
(`binaryFromDocument(doc, false)`), which proves nothing was lost without comparing field by field. B then becomes "drop the looks from the file and keep that test green".

**How to prove it instead of trusting this table:** an executable probe. Build a document with one of everything, save the graph text only, load it into an empty
`SketchDocument` with `graphFromText`, and diff the cooked result and every getter against the original. Whatever differs is exactly the `objects` table and the
`document` section. The probe becomes the first F3 test, so the schema comes out of a failing test rather than a guess. Expect the first run to show gaps (object names and
the appearance's active rows are the likely ones); if a gap is cheap to move into a node, the table shrinks.

## Stages

| Stage | Work | Done when |
|---|---|---|
| F1 | **Audit and schema.** List every `SketchDocument` and `SketchObject` field that `.skb` v4 stores; mark each as recoverable from the graph or not. Fix the JSON5 schema from that. | This note gets a table of fields and their keys. |
| F2 | **jitnodes: value in, value out. Done 2026-11-06.** `jitnodes/graph/serializeValue.h`: `saveNetworkValue(builder, net, includeTime, compact)` returns the graph as a `json5::value` inside the caller's document, and `loadNetworkValue(net, value)` loads one (throws and leaves `net` alone on failure); `saveNetwork` / `loadNetwork` are now these plus the text step. The JSON5 reader is the `json5` library jitnodes and newui already share (curvy has its include path), so no new dependency. A writer must `setprecision(9)` on its stream, or coordinates round to six digits (found by the test). | `GraphValue.*` in `tests/graphprobe.cpp`: the sample's graph, stored inside another JSON5 document, comes back byte-for-byte equal to `graphToText()`; a bad value throws and leaves the network alone. Full ctest 2139 pass. |
| F3 (slice 1 done 2026-11-06) | **Writer and reader** (`curvyfile.h/.cpp`, beside `sketchsvg` and `sketchbin`): `curvyFromDocument`, `documentFromCurvy`. Never trusts the input: counts and ids are checked, a failed load leaves the document untouched (as `documentFromBinary` does). | Round-trip tests below pass, including every document `.skb` tests build. |
| F4 | **Wire it in.** `readFromFile` sniffs `.curvy` by its first bytes; `writeToFile` and the Save dialogs default to `.curvy`; SVG and `.skb` become "Export" and "Open" options; a document opened from SVG saves as `.curvy` (Save As) and the title says so. Atomic save. Recent-files and any "unsaved changes" logic follow the new extension. | Save, close, open in the running app round-trips; GUI test added. |
| F5 | **SVG export without the graph by default,** with an "include curvy graph" option that keeps `<metadata>` for tools that want a round trip. `.skb` stays readable; whether it keeps being writable is a decision below. | An exported SVG opens in a browser and in curvy unchanged from today. |
| F6 | **Sample and docs.** Regenerate `Resources/samples/` as `.curvy` (keep the SVG next to it), update `design.md`, `architecture.md`, the migration roadmap, and the tracker. | The sample opens, plays its timeline, and diffs cleanly after a resave. |

## F3 slice 1 (2026-11-06): what is built

`include/curvy/curvyfile.h`, `src/curvyfile.cpp` (in CMake's app and model source lists), tests `tests/curvyfile.cpp` (`CurvyFile.*`, 5 tests). `curvyFromDocument`,
`documentFromCurvy(text, doc, &error)`, `isCurvyDocument`, `curvyFileSupports`. Covered: page, guides, layers, artboards, swatches, graphic styles, groups and clips, the graph,
and path objects with their looks (fills, strokes with dashes, widths, brush ids and arrows, effects, solid / linear / radial / point / pattern paints). Not yet: text, symbols,
patterns, brushes, images (the writer leaves them out and `curvyFileSupports` says so; the reader refuses an object of those types).

- **Layout.** A small model (`Json`) written by an emitter of its own: numbers as the shortest decimal that reads back exactly (floats as the shortest float, so `0.1`), keys
  unquoted, short containers and arrays of numbers on one line, everything else one member per line. `skyDreamer`: 848,800 bytes (the SVG is 701,513), 23,100 lines, longest line
  1,133 characters, no `&quot;`. Enums are still plain numbers (blend mode, cap, join, effect, object type); names for them are a later readability pass.
- **Proof.** A document loaded from `.curvy` writes the same uncompressed `.skb` image and the same graph text as one that went through `.skb`; a saved file reloads and saves
  byte-identically; the sparkle expressions still move things after the trip; 10 kinds of bad file are refused with a message and leave the document untouched.
- **Two things found on the way.** (1) The reader must run inside a `SketchDocument::Batch`: without it every `add` syncs the graph while only some objects exist, and the sync
  deletes the nodes of the objects not added yet (shapes became paths, transforms doubled; fixed by the batch, as `documentFromBinary` does). (2) Outlines of objects whose
  shape node is not in the root graph are written exactly, as `{cmds, xy}`; six-digit path text made them differ from their nodes.
- **`GraphDocument::graphReplaced()`** is new: a loader that replaced the network itself (`jitnodes::loadNetworkValue(document.network(), ...)`) calls it to refresh what
  depends on the graph.

## F3 slice 2 (2026-11-07): what is added

Text, symbol instances, pictures and the libraries. An object row gains `text: { words, font, size, matrix, style?, warp? }`, `symbol: <id>` or `image: <index>`. A `resources`
section holds `symbols` and `patterns` (`{ id, name, box, content }`), `brushes` (`{ id, name, kind, params, art? }`) and `images` (`{ name, data }`, base64 of the original file bytes).
`content` and `art` are whole `.curvy` documents (same shape as the file), so the reader is recursive (depth limit 8) and each is built in a document of its own before the
main one is touched. Proof: `CurvyFile.TextSymbolsPatternsBrushesAndPicturesComeBackAsTheSkbRoundTripDoes` (same `.skb` image as through `.skb`, resave identical),
`BadLibrariesAreRefused`. Not covered by a test: text on a path, and the fallbacks (a symbol or picture that cannot be found becomes a plain path, as in `.skb`).
`curvyFileSupports` is now always true.

## Tests

- Round trip for every kind of object the `.skb` and SVG tests cover (paths, parametric shapes, text, groups, layers and sublayers, effects, gradients and patterns, brushes,
  symbols, artboards, guides, expressions, time-driven parameters): save, load, compare the cooked geometry and the graph text; save again and the two files are byte-identical.
- `skyDreamer`: save as `.curvy`, load, play the timeline, the sparkles still pulse in every layer (the DG-14 case).
- Old files: SVG with metadata, SVG without it, `.skb` v1 to v4 all still open.
- Hostile input: truncated file, wrong `format`, higher `version`, duplicate or dangling node ids, huge counts, deep nesting; none crash, all leave the document untouched.
- Atomic save: a failed write (read-only target, full disk simulated) leaves the previous file intact.
- Size and speed: record the `.curvy` size and load time for `skyDreamer` and for the 1000 shape document next to the SVG figures; no budget test until there are numbers.
- GUI: save, close, open; Save As SVG; opening an SVG then saving offers `.curvy`.

## Autosave and crash recovery (2026-11-08, N4a)

The autosave timer writes `<stem>.curvy` in `%APPDATA%\curvy\autosave` through `writeRecoveryCopy` (curvyfile.cpp), using the same writer as Save but without making the copy the
document's file or clearing its modified flag (`SketchDocument::saveCopyTo`). If the document cannot be written as `.curvy` the copy goes out as `<stem>.svg` instead, and a
copy of the other format with the same stem is removed so a recovery is offered once. `recoveryCopies` lists `.curvy` and the `.svg` copies older builds left, newest first;
the startup offer and `recoverFrom` read either by content. Test hook: the environment variable `CURVY_AUTOSAVE_SECONDS` sets both the timer tick and the minimum gap
between copies in seconds (normally 30 s and the `autosaveMinutes` setting); `tests/gui/test_curvy_native_file.py` uses it to crash the app and check the recovery.

## Decisions still open

1. **Does `.skb` stay writable?** Recommend read-only: its job (very many shapes, compact) is covered by `.curvy` plus gzip, and two native formats double the round-trip tests.
2. **Embedded images** (when images are added): inline base64 in the text file, or a zip container `.curvy` with the JSON5 inside and the images beside it? Not needed until
   images exist; the version number allows adding it.
3. **Should the graph part be separable?** A `.curvy` could reference a library graph in another file. Not planned; noted because the text form makes it possible later.
4. **JSON5 reader location** (F2): newui's reader if jitnodes may depend on it, otherwise a small reader in jitnodes.

## Risks

- The per-object table is where fidelity can be lost; the audit (F1) and the `.skb`-equivalence tests are there for that. A field forgotten in F1 shows up as a failing round
  trip, not a silent loss, because the tests compare against what `.skb` keeps.
- Load time of a text parse for very large documents (2000+ objects) against `.skb`'s binary read. Measure in F3 before deciding on item 1; gzip costs little and the reader
  already sniffs it.
- jitnodes changes (F2) must be carried to `D:\code\jitnodes`, like the earlier ones.
