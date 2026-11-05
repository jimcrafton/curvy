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
| Output (the `display` node) | Dots with `radius`, `Cd`, `alpha`, `softness` (rasterised by the host with `renderSplats`), or closed polygons with `Cd` and `alpha` (drawn as vector fills). There is no `render` node inside. |

The stroke's opacity is not part of the graph: the host composites the whole stroke at the object's opacity, as for the art brushes.

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
gas that follows the pen (jitnodes' own note 4), flow hair and watercolour (the other examples).

## Not built yet

Cancelling a chunk that is already cooking (queued jobs are dropped only when more than 256 are waiting); rasterising dots with soft edges into a
cache per zoom (`renderSplats`); the `curvyGraphBrush` graph node (a graph brush is a brush on a stroke today, not a node); simulation brushes (settle,
then bake at pen up); a live preview of the real brush while drawing (the proxy band is shown); embedding the asset in the document; colour tint floats
for single-colour brushes; a tail taper for strokes without pen pressure.
