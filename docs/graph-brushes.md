# Graph brushes (jitnodes assets)

A graph brush is a jitnodes asset (`<type>.asset.json`, see jitnodes `docs/assets.md`) that turns a stroke path into paint. Curvy reads them at
run time, so a brush is added or changed by dropping a file in a folder. Background: `D:\code\jitnodes\docs\brushStrokes.md` (how the example
graphs work) and `skeletal-strokes.md` (the vector art brushes these sit beside).

Status: the three shipped brushes, the loader, and a first drawing path exist (see "How a stroke is drawn"). Dots are plain circles drawn into a cached
image (no soft edges yet); chunking and live preview are not built.

## How a stroke is drawn

- `BrushKind::Graph` (`brush.h`): a brush whose `graphType` is the asset's node type. `addBrushPresets()` adds one per loaded asset (category
  "Graph", named after the asset: `brush_oil` is "Oil"), so the picker lists them next to the other presets. On a document that already has brushes,
  use the Presets button in the Brushes box.
- The stroke weight is the stroke's width in document units, as for a nib (`BrushDef::startingWeight`: 16 or the document default, whichever is larger).
- `cookGraphBrush(type, path, weight, widths)` (`graphbrush.cpp`): each subpath of the stroke goes in as the guide, moved to start at the origin and
  divided by `k = weight / referenceWidth`, with the pen pressure profile as `w` (1 for a mouse). The asset is cooked in a throwaway network (a
  `geometry` node feeding one instance, `guideLength` set to the scaled length). Dots and polygons come back multiplied by `k`, relative to where the
  stroke starts (`GraphBrushCook::origin`). Dots with alpha 0 are dropped. The result is cached by the shape of the path (points relative to its
  start), weight, pressure and asset revision (128 entries), so repainting or moving a stroke does not cook again.
- `cookGraphBrushLater` is what the canvas calls: a cached cook comes back at once; otherwise the stroke is split into chunks of about 24 guide
  units, up to six worker threads cook them (newest request first) and the ready handler set in `main()` repaints the canvases on the UI thread.
  Until then the stroke shows as a hairline. Image export wraps painting in `GraphBrushBlockingScope` so it waits instead. An asset that promotes
  `guideStart` is told how far along the stroke its chunk begins (oil, ink and leaves use it for dryness and colour drift); chunk ends share a
  point, and the only seam is that the smoothing and stamp spacing restart there. Assets are not registered while cooks run.
- `paintGraphBrush` draws a cook from a cached image (`GraphBrushResult::raster`): the shapes are drawn once into a bitmap at the next 1.25 step of
  scale above the view's, and later paints just blit it (a stroke has tens of thousands of dots: filling them on every repaint took about 65 ms for
  13,000 dots in Debug against 0.05 ms for the blit). Zooming in makes a finer image; the image is capped at 4096 pixels a side.
- `paintBrushStroke` (`sketchobject.cpp`) calls it. If the asset is not loaded the stroke shows as a hairline. A stroke at less than full opacity is
  composited once as a whole, like the art brushes.
- Expand and PDF export turn the result into outlines (`graphBrushParts`): polygons as they are, each dot as a circle.
- File format: the brush is written as a nib in `BRSH` plus a `BGRF` chunk (brush id, asset type), so an older reader still opens the file and sees a
  nib. The asset itself is not stored in the file: opening a file on a machine without the asset draws the hairline.

## Status while a stroke cooks

`graphBrushProgress()` gives chunks done of chunks queued (since the queue was last empty) and how many strokes are waiting. The ready handler is
called when a stroke is queued and after every chunk, so the UI can follow it. While `busy()`: the canvas shows the arrow-with-spinner cursor
(`"busy"`, system `AppStarting`, so it is clear the editor still works) and the hint bar's right slot reads "Rendering brush 3/8" in place of the
pointer position (`WorkspaceModel::setBrushProgress`, `WorkspaceController::brushProgressChanged`).

`GraphBrushProgress::fraction` is chunks done plus how far each running chunk has got. A running chunk reports through jitnodes' progress
callback (`Network::setProgressCallback`: nodes done of nodes planned, and `gasField` and `particles` report every simulation step), so a
simulation brush moves smoothly instead of jumping from 0 to done; the hint bar shows it as "Brush 45% - Esc stops".

Esc while it is busy calls `cancelGraphBrushCooks()`: queued chunks are dropped and a running one stops at its next node or simulation step
(`CookCancelled`; nothing partial is kept or cached). Those strokes stay hairlines until they are edited, because asking again for the same
stroke would only restart what the user stopped. Quitting stops running cooks the same way, so a long simulation does not delay the exit.

Stale cooks are dropped on their own. Every paint that needs a stroke asks `cookGraphBrushLater` again, and a stroke being cooked remembers when it
was last asked for. While cooks run, the cook threads tick about every 150 ms (from the jitnodes progress callback): a stroke nobody has asked for in
3 s (deleted, scrolled off screen, or redrawn with another weight, which is a new stroke to the cache) has its queued chunks removed and its running
ones stopped, and the same tick tells the UI so the percent moves during a long chunk and the canvas repaints (which asks again for what is still on
screen). Unlike a stop with Esc, a dropped stroke is cooked again if it is asked for later (undo, scrolling back). A stroke in a view that is not
repainted during the cook (a thumbnail painted elsewhere) is dropped too and shows a hairline until that view paints again.
This needs the jitnodes progress hook, which the `3rdparty/jitnodes` submodule gets once the jitnodes change is committed and the submodule
is moved to it.

There is a limit of 400,000 shapes per stroke (`kMaxShapes`): a brush must keep dots per guide unit
low enough to reach the end of a long stroke (flow hair had run into it).

## Where they live

- Source: `Resources/brushes/*.asset.json`.
- Run time: `Resources/brushes/` next to `curvy.exe`, copied with the rest of `Resources` after each build, so edits to the source files reach
  the exe on the next build, and files can also be edited in place beside the exe.
- `loadBrushAssets(folder, &problems)` (`src/graphbrush.cpp`, `include/curvy/graphbrush.h`) registers every `*.asset.json` of the folder (sorted
  by file name) and returns those that are brushes. A file that does not load is skipped and reported; `main()` prints it. The asset type name is
  the file name up to the first dot, so name files `brush_<name>.asset.json` (type names are process-global and must not clash).

## The convention

An asset is a brush when it promotes both `referenceWidth` and `guideLength`.

| Part | Meaning |
|---|---|
| Input 0 (`subnetInput`, index 0) | The guide: the stroke path as an open polyline, already scaled into graph space, with the pen pressure (1 for a mouse) in point attribute `w`. |
| `referenceWidth` | The stroke width, in graph units, that the dot radius, spacing and bristle counts were tuned for. It feeds the `ribbon` width. The host scales the guide by `k = brushWeight / referenceWidth` before the cook and the output by `1/k` after, so a brush keeps its proportions at any weight. |
| `guideLength` | The length of the scaled guide, set by the host on every cook. The graph turns the fraction `u` into a distance (`along = u * guideLength`) so nothing crawls while a stroke grows. |
| `guideStart` (optional) | Where this chunk of the stroke begins, in guide units (`along = guideStart + u * guideLength`). |
| `settleTime` (optional) | Marks a simulation brush (`brush_gas`). The host sets the network time to this many seconds before the cook, so the simulation has run that long after the stroke is done, and cooks each subpath as one chunk (a simulation cannot be cut into pieces). The result is the same every time. |
| `guideMinX`, `guideMinY`, `guideMaxX`, `guideMaxY` (optional) | The box of the scaled guide, set by the host, for a brush that needs room around the stroke. `brush_gas` places its grid with them (`spread` units of room around the stroke, `cell` the cell size, at most 256 cells along the longer side). |
| `strokeLength` (optional) | The whole stroke's length in guide units. The shipped brushes divide `along` by `max(1, strokeLength / load)`, so a stroke longer than the paint load stretches the load over the whole stroke and runs dry only at its end instead of halfway. |
| Other promoted floats | The brush settings (bristles, dry length, spacing, ...). Only floats can be promoted, and they are read through expressions (`parent.<name>`) on a wrangle's `a` to `d` parameters, not inside wrangle code. |
| Output (the `display` node) | Dots with `radius`, `Cd`, `alpha`, `softness` (soft edges are drawn by the host), or closed polygons with `Cd` and `alpha` (drawn as vector fills). There is no `render` node inside. |

The stroke's opacity is not part of the graph: the host composites the whole stroke at the object's opacity, as for the art brushes.

## Brushes a document brings along

A saved graph embeds the definition of every asset it uses, and reading the graph registers them with jitnodes. `GraphDocument::graphFromText` and
`readFromFile` then call `registerEmbeddedBrushes()`, which adds every registered asset type that is a brush but is not in the brush folder to a
side list. `findGraphBrush` finds those too (so the strokes cook and the settings rows work), while `graphBrushes()`, the folder's list that feeds
the picker presets, is unchanged. Limits: if the folder has the same type, the folder's info is used even when the document's embedded copy is a
newer version (jitnodes registers the newer definition, so it cooks, but the cached `referenceWidth` and settings list come from the folder's
file); the side list is kept for the life of the process.

## Editing a brush's settings

Every promoted float that the host does not set itself (not `referenceWidth`, `guideLength`, `guideStart`, `strokeLength`, the `guide*` box or
`settleTime`) is a setting. `loadBrushAssets` lists them in `GraphBrushInfo::settings` (name, a label made from the name, the asset's value).
Promoted values are always floats, so the panel has number fields only; a colour is three separate floats, and there is no range or unit
information yet (that would need optional metadata in the asset format).

- The library brush ("Oil") only names the asset. The settings belong to each stroke: `StrokeOptions::graphSettings` (only the values that differ from
  the asset), so two strokes with the same brush can differ, and a duplicate carries its stroke's values.
- The Brushes panel builds one label and field per setting when a graph brush is chosen (as many rows as the asset has; the first time an asset
  is shown, the rows are made and kept, hidden for other brushes), plus a "Reset settings" button. They edit the selected strokes that have that
  brush (one undo step) and are read-only while none is selected.
- **The brush node.** A stroke with a graph brush has an instance of the asset in the document's graph, wired to its `curvyStroke` node (a leaf, so
  the look chain is unchanged), with the stroke's settings as its parameters. `syncAppearanceNodes` creates it (an owned node, role `0x1000 + item
  index`, so undo keeps its id), replaces it when the stroke gets another asset, writes the item's settings to it, and removes it with the stroke
  or when the stroke goes back to a plain one. An edit of its parameters in the graph (the graph dock, `setGraphParam`, undo) is adopted by the
  stroke through the same "node wins if it changed since the last sync" rule as the other look nodes: the settings are `brush.<name>` numbers in
  `AppearanceParams`, the changed ones only. Painting still reads the stroke's settings and cooks in the background; the node in the graph is not
  cooked by the document (a plain graph cook would use the unscaled path and no chunking).
- Values go into `cookGraphBrush`/`cookGraphBrushLater` and into the cache key, so changing one recooks that stroke. A name the asset does not have is
  ignored.
- The binary format keeps them in the stroke's record (bit 5 of the cap byte, then count, names and values) and in the graph. SVG does not carry
  them yet: that waits for the move of the whole document into the graph on main, where the brush node holds them.

## Colour: `tintable`

An asset may promote `tintable` (a setting like the others, read by the host and by the asset through an expression).

- 1 or more: the asset outputs shading, with white as full colour and darker as shade, and the host multiplies it by the stroke's paint. On the
  canvas this is `paintGraphBrushTinted`: the stroke is drawn in an isolated layer, the paint (solid, gradient or pattern, as for any stroke) is
  multiplied over the box, and the shapes' own alpha is put back over it. A colour change repaints and needs no recook. Where a dot is
  translucent the paint shows through a little, because multiply fills the whole box before the alpha is restored.
- 0, or no `tintable`: the asset paints in its own colours and the stroke's colour is ignored. The asset can expose its own colour floats.
- Expand and PDF multiply a solid paint's colour and opacity into each shape; a gradient or pattern is not applied there yet.
- `brush_ink` (flat white shading) and `brush_oil` (grey shading from the bristle value) are tintable by default; the other brushes keep their colours.
  An asset does this in its last wrangle with `lerp(ownColour, shading, parent.tintable)`.

## The shipped brushes

| Asset | Look | Settings (promoted) |
|---|---|---|
| `brush_oil` | A flat brush with bristles that each carry their own colour and run dry one by one. Dots. | `referenceWidth` 1.7, `bristles` 34, `dryStart` 50, `dryRange` 30 |
| `brush_ink` | Sumi ink: flat black, crisp edges, bristles drop out in streaks as the ink runs dry. Dots. | `referenceWidth` 1.3, `bristles` 64, `dryLength` 70, `dryness` 1.5 |
| `brush_leaves` | A leaf polygon stamped along the path with jitter, colour drifting green to amber. Polygons. | `referenceWidth` 1.6, `stampSpacing` 0.064, `driftLength` 40 |

Lengths such as `dryStart` and `driftLength` are in graph units, which are `referenceWidth` units of the brush: oil's 50 is about 30 stroke widths of
paint. They are a paint load, not a fraction of the stroke, so a longer stroke runs drier. (The first versions were tuned for the 14-unit demo guide
and ran dry after about five widths, which left most of a real stroke empty.)

All three start with `resample` (even spacing) and `smooth` (Taubin) on the guide, as the jitnodes guide recommends. They are the jitnodes
examples (`examples/graphs/brush`) with the demo `line` and bend wrangle removed and the stroke-fraction effects changed to distances.

## Trying one outside curvy

```
jitnodesCli --asset Resources/brushes/brush_oil.asset.json test.json --png out.png
```

where `test.json` is a graph with a `line`, a wrangle that bends it and writes `w`, one node of the brush's type and a `render` node.

## Simulation brushes

`brush_gas` pours the stroke into a jitnodes `gasField` (`examples/graphs/brush/brushGasInk.json`, adapted for white paper: an ink ramp with alpha from
the density instead of additive light). It is cooked once for the whole stroke at `settleTime` 2.5 s; there is no live preview while drawing, the
stroke appears (as a hairline until then) a moment after pen up. `brush_light` (jitnodes `brushLightPaint.json`) scatters particles along the
stroke (`density` per guide unit, at most 6000), has them follow it for 1.6 s while their own gas stirs them, and dots each velocity streak, coloured
cyan, magenta, amber along the stroke (darker than the additive original so it reads on white). A 300 px stroke took about 25 s in Debug. Cost grows with the grid, which is capped at 256 cells on the longer side:
a 300 px stroke took about 6 s in a Debug build, so expect roughly a tenth of that in Release (not measured). Settings: `flow` (how hard the
stroke is blown along itself), `inkAmount`, `vorticity` (the curls), `spread`, `cell`. The picture is deterministic. Not built: a live-stepping
gas that follows the pen (jitnodes' own note 4).

## Flow hair and watercolour

| Asset | Look | Settings (promoted) |
|---|---|---|
| `brush_flowhair` | Seeds scattered along the stroke each start a trail through a curl-noise field (`flow`): a tight band that fans out into curling filaments, colour from teal to purple. Dots; ignores pen pressure; hairs are cooked per 24-unit chunk, so check long strokes for a visible join (not looked at yet). | `referenceWidth` 1.4, `density` 130 hairs per guide unit (at most 6000 per cook), `curl` 0.22 (field scale), `alpha` 0.3 |
| `brush_watercolor` | A wet wash: large soft transparent dots pool into the stroke, pigment gathers at the edge, the edge wobbles, a position hash grains it, colour drifts blue to violet and the wash fades over the last sixth. Dots; pressure widens it. | `referenceWidth` 2, `wash` 1 (opacity), `granulation` 1 (0 is smooth) |

Both are the jitnodes examples (`brushFlowHair.json`, `brushWatercolor.json`) with the demo guide removed and colours darkened for white paper. Watercolour
uses `strokeLength` for the colour drift and the fade, so they follow the whole stroke, not each chunk. Dots take their `softness` (0 hard, 1 fading from the
centre) into the cached image as a radial gradient that follows jitnodes' `renderSplats` falloff. Expand and PDF still write hard circles.

## Not built yet

Cancelling a chunk that is already cooking (queued jobs are dropped only when more than 256 are waiting); soft dots in Expand and PDF; the `curvyGraphBrush` graph node (a graph brush is a brush on a stroke today, not a node); simulation brushes (settle,
then bake at pen up); a live preview of the real brush while drawing (the proxy band is shown); embedding the asset in the document; colour tint floats
for single-colour brushes.

## Tail taper

A stroke without a width profile (a mouse stroke) has pressure 1 all along, so its tail ends bluntly. An asset that promotes `tailTaper` (a
fraction of the stroke, 0 is off, default 0) gets the end of the guide's pressure thinned by the host, smoothly down to a tenth, over that fraction.
It is an ordinary setting, so it shows in the panel and is saved per stroke; the asset only has to promote it, it need not read it. A stroke with a
width profile (pressure, Taper, Swell) is left alone. Oil, ink, leaves and watercolour promote it.
