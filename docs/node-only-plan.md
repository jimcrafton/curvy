# Plan: a document that is only its graph (2026-11-07)

Goal: finish the migration to the node graph as the single store, and finish the `.curvy` format with it. The test of "done" is that a `.curvy` file has no `objects`
list, opening it builds every object from its nodes, and nothing in the editor writes object state that the nodes do not also hold.

## Where things stand

* Nodes already hold: outline, placement, paint warp, text and its style and warp, symbol and image references, appearance rows (fills, strokes, effects), layers and
  groups, shape parameters, expressions, keys, time. Probed 2026-11-07: the look nodes lose no appearance field (only float precision), see `native-format.md`.
* `.curvy` today (approach A) stores the graph **and** an `objects` list (type, name, flags, looks, active rows, text data, node id, sometimes an outline), so the
  file says everything twice and the loader uses the list, then lets `syncPathNodes` / `bindLoadedNode` reconcile.
* Graph-only load builds 0 of 183 objects; there is no builder from nodes.
* Left on purpose until now (`graph-migration-roadmap.md`): `syncPathNodes`, `bindLoadedNode`, `GeometryState`, the revision/bound state, `frameAngle_`.

## What "100%" means, in two tiers

1. **Storage (this plan, N0 to N5):** the file and the undo snapshots hold the graph and nothing about objects. `SketchObject` is rebuilt from nodes on load and kept
   in step with them in memory, as it is today.
2. **No mirror (N6, optional):** `SketchObject` stops being a second copy at all and reads through to its nodes. Large, and it buys little once tier 1 holds, so it is
   a decision after N5, not part of the commitment.

Document-level settings (page, artboards, guides, swatches, graphic styles, default stroke width, origin, active layer, and the symbol / pattern / brush libraries)
stay in the file's `document` and `resources` sections. They are not geometry; Houdini and Illustrator keep scene settings beside the graph too. **Decided
2026-11-07: they never become nodes.** Also decided: the float precision of looks (nodes hold floats) is accepted.

## Stages

Each stage is shippable alone, ends with `ctest` green and the three perf tests re-measured (`AnEditCostsLittleWhenManyObjectsAreGrouped`,
`AThousandsObjectDocumentStaysResponsive`, `TheFrameOfALargeGraphIsCheapToMeasureWhileScrubbing`), and updates the tracker.

### N0. Audit by test: what is not recoverable from a node alone

Extend `tests/graphprobe.cpp`. For the sample **and** a document with one of everything (`fillLibraries` in `tests/curvyfile.cpp`: text on and off a path, symbols,
a pattern fill, brushes, a picture, groups, clips, a parent link, hidden / locked objects, opacity and blend, a non-zero frame angle), build a bare object from each
shape node and diff every `SketchObject` getter against the original. Whatever differs is the work list for N1. Expected, from the audit so far:

| Object state | Where it is now | Expected fix |
|---|---|---|
| name | object only (0 of 183 shape nodes carry it) | shape node name |
| type (path, rect, ellipse, line, text, symbol, image ...) | object only | derive from the node type and parameters; add a `kind` string where a node cannot say |
| visible, locked, opacity, blend | object, partly mirrored | confirm they are node parameters; add if not (the sample only has defaults, so test with non-defaults) |
| parent link | wired as the transform node's optional input | derive; verify across groups (known limit, roadmap A3) |
| active fill / stroke / effect row | object only (122 of 183 match by luck) | three integer parameters on the shape node |
| `frameAngle_` | object only, "left outside on purpose" | a float parameter on the transform node that does not drive geometry |
| text outline | object, saved because fonts differ per machine | rebuilt from words, font and size at load |
| text warp frame `warpFrame_` | object only | derive from the rebuilt layout, or store with the text parameters |
| layer / group request fields (`layer_`, `groups_`, `placedGroups_`) | object, kept for unplaced objects | derive from the scope; keep for objects not yet in a document |
| shapes whose node is not in the root graph | file stores exact outlines | read the outline from the node in its scope (six-digit curve text is accepted precision) |
| looks | object, mirrored into look nodes | already complete; see float-step note below |

Done when: the probe prints the exact list, the table above is corrected from it, and nothing is left unexplained.

**N0 result (2026-11-07, `GraphProbe.WhatABareObjectBuiltFromItsShapeNodeStillLacks`).** A bare empty path object bound to each shape node of a copy of the graph, versus
the original. The sample has 183 objects, the one-of-everything document 19 (names, opacity / blend, hidden, locked, a rotated rect, a group, a clip, a parent link,
an extra layer, a symbol, text with a style, text on a path, a picture).

| Field | Sample (183) | Everything (19) | Verdict |
|---|---|---|---|
| name | 183 differ | 6 (all named ones) | object only: N1 |
| type | 48 | 17 | object only (a bare object is always a path): N1 type tag |
| visible, locked, opacity, blendMode | 0 (all defaults) | 1 each (the one non-default of each) | object only: confirmed, N1 |
| activeRows | 0 (by luck) | 1 | object only: N1 |
| frameAngle | 0 | 1 | object only: N1 |
| parent | 0 | 1 | object only: the transform input is not read back on load, N1 |
| layer | 0 | 0 | the "3 wrong layers" first printed were a probe error (objects were paired by position, but `add` reorders); paired by node, layer is derived correctly from the scope |
| appearance row count | 61 | 0 | the sync overwrites the nodes' rows with the bare object's default look (the known "adopt" gap, N2 step 3) |
| text words / matrix / style / warp / warp frame | n/a | 1 to 2 each | object only: N1 text parameters |
| symbol, image | n/a | 2, 1 | reference not rebuilt: N1 |
| outline, segments | all | all | not a finding: nothing pulls the node's curve into an empty object on `add`; the builder in N2 has to |
| groups | 0 | 0 | recoverable from the scope |

**A real finding from chasing the layer mismatch:** `add` of a bare object bound to an existing node does not always keep the binding. In the sample 1 of 183 (`Paper`,
node 1 of scope `1`) comes out bound to a brand-new node in another scope (`4#371`); in the one-of-everything document the 4 members of the two groups (scopes `1/34`
and `1/43`) each get a fresh node in the layer scope (`1#99` ...), leaving the group scope's node orphaned. A bare object has no groups, so `add` places it by its own
(empty) group chain instead of by the scope its node is in. N2 must set `groups_` / `placedGroups_` / `layer_` / `placedLayer_` from the scope before the object is
added (the plan's "derive from the scope" row), and the `Paper` case is **fixed (2026-11-07)**. Cause: `setLayers` left `layersEdited_` true (only `changed()` clears it), and while it is true `layerOf` ignores
an object's scope, so the first object added afterwards went to the active layer and its nodes were copied there. (`placedBaseLayer_`, the first guess, was not it:
changing `baseMoved` made no difference.) Fix: `setLayers` now finishes through `changed()` (`documentlayers.cpp`), which writes the table to the graph and clears the
flag. Full suite green (405 pass, 1 skip). What remains is the group members: a bare object has no groups, and the sync removes a group subnet no object carries, so a
graph-only builder (N2) must set `groups_` / `placedGroups_` (and `layer_` / `placedLayer_`) from each object's scope before it is added, inside a `Batch`. The real
loaders already do this (`GraphProbe.GroupsAndClipsSurviveTheNativeFileAndTheSkbRoundTrip` passes).

### N1. Move the missing state into nodes

One field at a time, each with the same three pieces the existing mirrors have: written by the sync (`syncPathScope`), adopted by `pullPathNodes` when a graph edit or
undo changes it, and a test that edits it from the node view. Order: name, flags and opacity / blend if needed, active rows, frame angle, type tag, text frame. Undo
needs nothing extra (the graph snapshot already carries node parameters), but `GeometryState` and its per-object payload must be checked for fields that only it holds.
Perf: each new parameter is one more compare in the per-object sync pass; measure at 2000 objects.

Progress: name (`label`), visible / locked / opacity / blend, active fill / stroke / effect rows, `frameAngle` and `type` are node parameters, stored by
`storeObjectState` behind the per-object `nodeStamp` and adopted by `applyObjectState` (type is record-only: an object never changes type; N2 reads it with
`objectTypeFromName`). The text frame needed no new parameters: the text node already holds words, font, size, matrix, style and warp, and the warp frame is derived
from the layout (`GraphProbe.ATextObjectIsRebuiltFromNothingButItsNodesParameters`). A symbol node holds the symbol id (`SketchDocument::findSymbol` resolves it); an image node holds `imageKey`, a content hash set once when the picture is decoded
(`ImageData::key`), because the picture's name is not unique. N3 must store the pictures keyed by it.

The parent link needed no new parameter: it is the transform node's input 1, and the sync already reads it back for an object that is loaded and not yet bound
(`CurvyFile.TextSymbolsPatternsBrushes...` now fails if a link is lost through the native file; the link must stay inside one scope). The N0 probe still prints
`parent 1` only because it adds bare objects one at a time, and a sync with fewer objects than nodes drops the placement nodes that carry the link; add inside a
`Batch` with the graph and layers installed in the same batch loses the node binding instead (the group/layer derivation above). N2's builder has to set parent
from the transform input itself, after every object of the scope exists, and must not rely on the sync to do it. N1 is complete.

### N2. Build objects from the graph

A `SketchDocument` method (`objectsFromGraph`, runs inside a `Batch`) that, after the graph and layer table are installed:

1. walks every scope in paint order (`pullPaintOrder` already reads the merge order) and creates one object per shape node through `SketchObject::Prototype`,
2. reads outline from the node's curve under its placement, text from the text parameters (rebuilding the outline), symbol / image by reference,
3. reads the look chain with `itemFrom` and **fills `appearanceSynced_` from the nodes**, so `syncAppearanceNodes` treats the object and its node as already in step instead of
   overwriting the loaded nodes with defaults. This is approach B proper,
4. binds the object to its node (`setGraphNode`, `setGraphRef`, bound state), derives layer, groups and parent from the scope and the transform input.

It must be linear (no per-object sync): the 27 s big-file load is the warning. Test: graph-only load of the sample and of the one-of-everything document gives a
document that equals the original structurally (every getter, floats compared to a float step) and renders the same pixels.

**N2 progress (2026-11-08): `SketchDocument::objectsFromGraph(pictures)` is built** (`documentlive.cpp`). It walks the scopes depth first by each scope's `paint` merge
(shapes no merge lists follow, by id), and for each shape node that has a placement node builds the object from the node alone: type from the `type` tag (or the node
type for text, symbol, image), outline from the drawn curve placed by the world matrix, text through `applyTextNodeParams`, symbol through `findSymbol`, picture through
the caller's `pictures(imageKey)` lookup, looks through `itemFrom` over a default item per look node, name and flags through `applyObjectState`, paint matrix and warp
from the placement node, and layer / groups from the scope (group ids are read from the subnets, `nextGroupId_` is raised past them). Parents are set last from
transform input 1, within a scope. An object with a missing symbol or picture falls back to a plain path. The document must be empty with its layers, page size and
libraries installed first. Nothing new is needed in the sync: the built objects agree with their nodes, so `bindLoadedNode` keeps them bound.
`GraphProbe.ObjectsBuiltFromTheGraphAloneMatchTheOriginals` (sample and one-of-everything) checks type, name, flags, opacity / blend, layer, group count, parent,
active rows, frame angle, every look item's parameters, outline box, text, symbol and picture against the original, that a first sync leaves every object bound,
and that both render within a few bytes of each other. Left for N3: the reader calling it, pictures keyed in the file, and the file no longer writing `objects`.
**N2 gaps covered (2026-11-08):**

* **Live shapes removed (2026-11-08).** The generator-backed "live shape" (an object whose node is a jitnodes generator such as `circle`) was the first Phase 2
  experiment, superseded by `curvyShape` on 2026-11-04; only the "New Circle" menu command still made one, and a generator node cannot declare the object state
  (name, flags, opacity, blend) that shape nodes hold. `addLiveShape`, `setLiveShapeParam`, the command and its menu entry are gone, and `objectsFromGraph` builds
  objects from shape nodes only (a generator wired in by hand is not an object: `GraphProbe.AGeneratorWiredInByHandIsNotBuiltAsAnObject`). Star, polygon, round rect and
  arc are unaffected: they are `curvyShape` nodes (`liveshape.cpp` / `recogniseLiveShape` is their handle and panel code, and keeps its name).
* A shape with no placement node is built with an identity world and keeps its look chain; the sync that ends the batch adds the placement node
  (`...HasNoPlacementNodeKeepsItsLook`). Shape nodes no `paint` merge lists are swept up by id even without a placement node.
* A keyed look number: `paramsOf(node)` reads the stored value (`baseFloat`), so the built object takes the base value and a sync leaves the keys alone
  (`AKeyedLookNumberKeepsItsBaseValue...`). This needed no code.
* A parent link that does not name another object's placement node is reported: `objectsFromGraph(pictures, &notes)` appends one line per unresolved link and leaves the
  object without a parent. Links never cross scopes in a file the app wrote (grouping drops them), so this only fires on a hand-edited file. `worldTransformOf` now
  follows input 1 only through transform nodes (and stops after 1000 hops), so such a wire can no longer throw or loop (`AParentLinkThatCannotBeResolvedIsReported`).

### N3. `.curvy` version 2 without `objects`

* Writer stops writing `objects`, the `symbol` / `image` / `text` per-object data and the outlines; the `resources` section keeps the library definitions and image
  bytes. File shrinks by the looks and the object table.
* Reader: version 2 builds objects with N2. Version 1 files still open through the existing object-table path (the sample and any file you saved in the last
  days); once nothing depends on it, that reader path is deleted.
* The oracle changes: byte-identical `.skb` no longer holds for looks stored as doubles, so the check becomes the structural + pixel comparison from N2, plus
  "resave is byte-identical" (a saved file reloads and saves the same text), which does not depend on floats.
* Regenerate `Resources/samples/skyDreamer.curvy` (generator exists); update `native-format.md` (the `objects` section is gone) and the hostile-input tests (a node
  with a missing type, a look node with no shape, a text node with a missing font, a symbol reference nobody defines).

**N3 done (2026-11-08):** `kVersion` is 2. The writer drops `objects` and `groups` and writes images with their `key`; the reader (`loadFile`, `src/curvyfile.cpp`) takes
version 1 through the object-table path and version 2 through `objectsFromGraph` after the libraries, layers and graph are installed. `SketchDocument::writeToFile` calls
`syncPathNodes()` before writing a `.curvy`, because a document just opened from SVG had not stored every object's name and flags in its nodes (170 of 183 names were lost
without it; `SaveAndOpenGoThroughTheFileByContentNotName` checks it). The oracle is now `expectSameDocument` (fields, then render within tolerance) plus resave identity;
`AVersionOneFileWithAnObjectTableStillOpens` reads `tests/data/skyDreamerV1.curvy`. `skyDreamer.curvy` regenerated: 557 KB (was 954 KB). Not deleted yet: the version 1 reader
path (a file written in the last days may still be version 1).

### N4. Retire the reconcile code

Prerequisites, in this order:

1. **Autosave and crash recovery write `.curvy`**, not SVG-with-graph (they are the other caller of `bindLoadedNode`).
2. **F5**: SVG export carries no graph by default (the user's import/export branch must merge first; do not touch PDF / AI / EPS / SVG code before that).
3. Old SVG-with-graph files and `.skb` before version 4: decide to stop matching their outlines to embedded nodes (import as plain paths, build nodes fresh) or keep
   one converter. Importers then only create plain objects and let the sync create their nodes, which is the "adopt into graph" path and stays.

Then remove `bindLoadedNode` and the `everBound` / bound-state bookkeeping that exists for loaded-vs-edited, simplify `syncPathNodes` to "create nodes for objects that
have none, remove nodes of deleted objects", and re-check `GeometryState` (still the small undo payload; stays if its measurement still justifies it, 135 ms at 2000
objects for a full snapshot).

**N4 plan (2026-11-08).** What the reconcile code is and who feeds it: `bindLoadedNode` (documentlive.cpp:170) runs for an object that has a node reference but was
never written to it (`known && !everBound`), and keeps the loaded node when node and object agree within `kFileTolerance`; the same `!everBound` test also lets a loaded
parent link (`placeId` input 1) set the object's parent (documentlive.cpp:1907). The only producers of such objects are the SVG reader (`data-graph-node` /
`data-graph-scope`, sketchsvg.cpp:1681, 2126, plus the `<jitnodes:graph>` metadata) and the `.skb` reader. `.curvy` v2 never produces them (objects are built from the graph
and are bound already). `setBoundToNodes(false)` on a move into a group (documentlive.cpp:1206) is the third use of `everBound`.

Steps, each one landing with the tests green:

* **N4a (no import/export code touched, can start now): autosave and recovery write `.curvy`** (DG-19). `autosaveTick` writes `<stem>.curvy` through
  `SketchDocument::writeToFile`, `offerRecovery` looks for `.curvy`, `recoverFrom` already opens through `openInto`. Fall-back: `curvyFileSupports` refuses a document with
  features the format cannot hold; if it refuses, report it in the status bar and fall back to the SVG recovery copy instead of silently skipping the autosave. Keep reading
  old `.svg` recovery files for one release (a crash with the old build leaves one). Tests: autosave tick writes a loadable `.curvy` (`expectSameDocument`), recovery offer finds it,
  an old `.svg` copy is still offered. GUI: extend `tests/gui/test_curvy_native_file.py` once the 30 s tick can be forced (a settings value or a test hook).
* **N4b (waits for the import/export branch to merge to main): decide the old files.** Recommended: SVG and `.skb` import stop honouring embedded graph references
  (`data-graph-node`, `<jitnodes:graph>`, `.skb` node ids) and import plain objects, so the sync creates fresh nodes; SVG export stops writing the graph (F5). Cost: an SVG
  saved by an older build loses its stored node edits (typed transform numbers, keyed looks) on open; the shape and look are unchanged. Alternative: keep a one-off converter
  (open old SVG-with-graph, resave as `.curvy`) and then drop it. Left open on purpose (2026-11-08): the user's pending import/export branch may settle it, so revisit after it merges. It also touches `sketchsvg.cpp`, which stays off limits until that branch lands.
* **N4c (after N4b): remove the code.** Delete `bindLoadedNode`, `kFileTolerance`, `sameMatrix` / `sameCurveShape` if nothing else uses them, `known` in the sync's `Bound`
  entries, the loaded-parent branch, `everBound_` / `boundToNodes` pieces that only existed for loaded-vs-edited (`bound_` itself stays: it is the cheap "node is current"
  flag the sync uses). Simplify `syncPathNodes` to create-missing / remove-deleted / write-stale. Keep the v1 `.curvy` reader decision separate (delete when no v1 file matters).
* **N4d: re-measure.** Run the 2000-object perf tests and the edit-at-1000-shapes number (DG-12: ~1.2 ms) before and after; `GeometryState` stays if its 135 ms full-snapshot
  comparison still holds.

**Status (2026-11-08): N4a, N4c and N4d DONE; N4b skipped for now (by decision).** N4c did not need N4b: the readers are untouched (`sketchsvg.cpp`, `.skb`), and
removing the reconcile code has the same effect on an SVG or `.skb` read as N4b's recommendation, without editing them. A loaded object is now simply written to its node
by the first sync, so a node edit stored in an old SVG-with-graph (typed transform numbers, keyed looks) is overwritten by the object's values; the shape and look are
unchanged. Removed: `bindLoadedNode`, `Bound::known`, `SketchObject::everBound` / `everBound_`, the `world` matrix lookup the check needed. Kept: `sameMatrix` and
`kFileTolerance` (other callers: placement write-back, parametric recognition, pull of a cooked outline), `bound_`, `setBoundToNodes`. The loaded-parent branch stayed in
a narrower form: an object that is not bound yet and has no parent takes the parent its transform node's input 1 links to, because the `.skb` writer is still live and
stores the parent only as that link (releasing a parent leaves the object bound, so it is not re-adopted). Test change: `PathNode.SurviveSaveAndLoad` compared the loaded
graph text to the original; the load now writes object state (name) to the nodes, so it checks node count and that a second trip is a fixed point.
N4d measurements (same machine, release): edit at 1000 shapes 1.40 ms before, 1.28 to 1.46 ms after over three runs (100 shapes 0.21 ms both); 5000-shape SVG
read+parse 801 ms before, 793 to 799 ms after; the other perf tests are unchanged. `GeometryState` is untouched. No measurable change, so nothing to revisit.
Resume N4b (SVG and `.skb` readers stop honouring graph references, F5 SVG export without the graph) after the import/export branch merges; with N4c done it is now
only a reader/writer clean-up.

Risks: the `.skb` writer is still a live format (decide in N5 whether it stays writable); symbol and brush definitions compare their content through `svgFromDocument` (documentsymbols.cpp, documentbrushes.cpp), so
`svgFromDocument` itself cannot go away with N4, only its graph metadata; check before N4c whether anything else reads that metadata.

### N5. Close-out

GUI check of Save / Open / Save As / recent files / autosave recovery with the newui GUI driver (ask before input runs); carry the jitnodes edits
(`serializeValue.h`, `serialize.cpp`) to `D:\code\jitnodes` (ask first, the include trees differ); decide whether `.skb` stays writable; update `design.md`,
`architecture.md`, `graph-migration-roadmap.md` (the "left on purpose" table loses its last row), refresh the tracker Notes snapshot.

### N6. Optional: objects as pure views

Make `SketchObject` hold only an id and a node reference, with every getter reading the node, and cache the cooked outline. Removes the mirror and the sync passes
entirely. Costs: every read site (hundreds) goes through the graph, so the per-edit scans the roadmap warns about become the main risk, and the 2000-object perf tests
need a cache that is itself a second copy. Decide after N5 from measurements; likely not worth it.

## Order and size

N0 (small) -> N1 (medium, one field at a time) -> N2 (largest) -> N3 (medium) -> N5 GUI check and jitnodes carry-over can run any time after N3 -> N4 (waits on
the import/export branch and on autosave) -> N6 only if wanted. N0 to N3 do not touch import / export code and can start now.

## Risks

* **Float precision of looks.** Nodes hold floats, items doubles. A file round trip changes an opacity of 0.37 to the float 0.37. Accept it, say so in `native-format.md`,
  and make the item type use float, or keep doubles and round on write (a later cleanup).
* **Look parameters driven by expressions or keys**: the object stores the base value; the cooked value must never be written back as the base. Add a test with a keyed
  stroke width before N2.
* **Load speed.** The builder must be one pass; a per-object `add` that syncs is quadratic (`Batch` is essential, as found in slice 1).
* **Text.** Rebuilding outlines needs the font; on a machine without it the object must still load with a fallback and keep the font name.
* **Cross-scope links and live shapes in the root** (roadmap A3, A4) are known limits; the builder must not lose a parent link it cannot represent without saying so.
* **Perf**: every stage re-measures the three perf tests.
