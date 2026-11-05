# Skeletal strokes (art brushes)

How curvy bends artwork along a stroke. Based on Hsu, Lee and Wiseman, "Skeletal Strokes" (UIST '93, `168642.168662.pdf`) and "Drawing and
Animation Using Skeletal Strokes" (SIGGRAPH '94, `192161.192186.pdf`). UI design: `ui-design.md` (Brushes tab, Skeletal strokes) and
`mockups/skeletal.html`.

## The idea

A brush is a picture drawn once around a straight reference line. Applying it to a path maps every point of the picture from
(distance along the line, distance across it) to (distance along the path, distance to the left of the path). The result is still vector
geometry, so it stays sharp at any zoom, exports to PDF and can be edited by changing the brush.

Everything is in `src/brush.cpp` and `include/curvy/brush.h`. Painting is `paintBrushStroke` in `src/sketchobject.cpp`.

## Brush kinds (`BrushKind`)

| Kind | What it does | Entry point |
|---|---|---|
| Calligraphic | A slanted oval nib is swept along the path (the hull of the nib at each segment end, filled non-zero, so the pieces union). Width is the stroke weight. | `nibOutline` |
| Scatter | Copies of the artwork stand along the path at `spacing`, with `scatter` (sideways), `sizeVariation` and `rotation` jitter from a hash of the copy index, so copies do not shimmer on redraw. | `placements` |
| Art | The artwork is bent along the path (a skeletal stroke). | `bendAlong` / `bendParts` |

For Art and Scatter a stroke weight of 1 draws the artwork at its own size, 2 at twice the size. Weight scales across and along together
(the artwork keeps its proportions); only Stretch and Guided then change the length to fit the path.

## The artwork

`BrushDef::art` is a `SymbolDef`: a box plus a small document. `parts()` turns each visible, non-symbol object into a `BrushPart` (outline,
closed flag, fill, stroke, stroke width) the first time it is asked for. The box is the unit that is stretched or repeated: its x axis is the
reference backbone (or y when the direction is top-to-bottom) and its height is the reference thickness.

Brushes are immutable once made (like symbols and patterns): editing one swaps in a new definition (`swapBrush`), so strokes can share a
definition and undo is a pointer swap. A brush can be linked to a symbol (`sourceSymbol`, `SketchDocument::linkBrushToSymbol`); redefining the
symbol then rebuilds the artwork of the brush (`brushesLinkedTo`), and so every stroke that uses it.

## Bending (`BrushDef::bendParts`)

1. **Measure the path.** `measurePath` flattens it into polylines with cumulative lengths (`MeasuredPath`: `points`, `at`, `total`).
2. **Apply the corner policy** (`BrushCorner`), per subpath:
   - *Bend*: use the path as is. The artwork tears on the outside of a corner and overlaps on the inside.
   - *Smooth* (default): `roundCorners` replaces every vertex that turns more than 15 degrees with a quadratic curve that starts and ends
     `cornerRadius`% of the artwork height (times the weight) away, at most half a segment. Turns smaller than that are the joins of a flattened
     curve and are left alone.
   - *Miter*: `place` slides each offset point along its segment by `across * tan(turn / 2)` (clamped by `miterLimit`), so the offset lines of two
     segments meet at the corner. The outside is lengthened and the inside shortened, which keeps the outline continuous.
   - *Break*: `splitAtCorners` cuts the path at every vertex that turns more than `breakAngle`, and each piece is fitted on its own.
3. **Fit along** (`BrushFit`). `along(u)` maps a position 0..1 of the artwork width to a distance on the path:
   - *Stretch*: `u * length`.
   - *Guided*: `headGuide` and `tailGuide` split the art into a rigid head, a stretching middle and a rigid tail. The head and tail take
     `fraction * artworkWidth * weight` of path each (an arrowhead stays an arrowhead) and the middle takes the rest. If the path is shorter than
     head plus tail, the two shrink together. This is the paper's anchoring, in its simplest (zeroth order) form.
   - *Repeat*: `round(length / (artworkWidth * weight))` whole copies, each stretched a little so they fill the path exactly.
4. **Map every point.** `map(x, y)` takes a point of the artwork, gets `offset` (across, up = left of travel) and `at` (along), applies
   `flipAlong`, `flipAcross` and the width profile, and calls `place(at, offset * weight * scale)`: the point on the path at that length, moved along the
   left normal. Top-to-bottom artwork swaps the roles of x and y so its top comes first.
5. **Resample edges.** Artwork outlines are flattened (`flattenPath`) and every edge is cut into pieces of at most 1/150 of the artwork's length
   (at most 300 per edge), so straight edges bend with the path. The result is polylines, not curves.

The output is a `BrushPart` per artwork part per copy per subpath, carrying the part's own fill and stroke (stroke widths scale with the
weight). `paintBrushParts` fills and strokes them. A brushed stroke that is not fully opaque is painted into an isolated layer sized by
`BrushDef::reach` (how far the brush can reach from the path) so that overlapping parts do not show through each other.

## Width profile and pen pressure

`StrokeOptions::widths` (a list of `WidthPoint`, position `t` 0..1 and a left and right multiplier) scales the artwork across the path. For
Art brushes it is applied per side, for the nib and scatter copies as the mean of both sides.

Pen pressure is stored in that field. `pressureProfile` turns the pressure samples of a freehand stroke into a profile: one point about every
4% of the length plus both ends, the pressure itself being the multiplier (so the finished stroke is as wide as the live proxy was). It returns
nothing when the pressure varies by less than 0.05 (a mouse), so mouse strokes store nothing. Hit testing, bounds (`reach`), Expand and PDF
export all take the profile.

## Drawing with the Paintbrush

`CanvasView` (`src/canvastools.cpp`): the Paintbrush records points and pressure, fits the line (`fitCurve`), creates a path object stroked
by the active brush (weight 1 for artwork, at least 2 for a nib) and stores the pressure profile. While drawing, `paintPencilPreview` shows a
proxy: an outlined band as wide as the brush's reach (widened by pressure) with the drawn line down the middle. The real stroke replaces
it on release.

## The brush library

- Brushes live in the document (`SketchDocument::brushes_`, `findBrush`, `activeBrush`). Arming the Paintbrush on a document with no brushes
  adds the presets (`src/documentbrushes.cpp`).
- `BrushList` (`src/brushlist.cpp`) is the picker: Favorites, Most Recent (`recentBrushes()`, not saved) and one folding group per
  category (`BrushDef::category`, defaulting to the kind name), with a sample stroke as each thumbnail.
- `BrushPanel` (`src/brushpanel.cpp`) shows the controls for the kind of the active brush (`fitRows` hides the rest). The Brushes box in
  the property dock appears for the Paintbrush, a brushed selection, or exactly two selected objects.
- The settings are numbers in a fixed order (`params()` / `setParams()`, which clamps every value into range) and are saved in the `BFIT`
  chunk of `.skb` files; `BSRC` holds the source symbol and `BCAT` the category and favourite flag. The picker-only fields are not part of
  `params()` or `sameLook()`.

## Deform along path (a graph node)

`curvyDeformAlongPath` (`src/pathnode.cpp`, `DeformAlongPathNode`) runs `bendParts` on any geometry, not only on a stored brush. Inputs: the
artwork (0) and the spine (1). Parameters: `fit`, `headGuide`, `tailGuide`, `corner`, `cornerRadius`, `miterLimit`, `breakAngle`, `direction`,
`flipAlong`, `flipAcross`, `width`, and `frame0..frame5`. The box of the artwork is its bounding box. The node ignores the pressure profile.

`SketchDocument::addDeformAlongPath(art, spine)` inserts it between an object's shape and its placement. The object becomes "driven": its
own transform comes after the bend. The spine arrives in page space, so `frame0..5` hold the inverse of the object's current placement, and
the result lands on the spine where the object sits. `deformSelectionAlongPath()` picks the spine as the open path of two selected objects.
The whole edit is one undo step (`brush.deform` command, the button in the Brushes box).

## Differences from the papers

Implemented: reference backbone and thickness, bending with a fixed local frame, corner joins, anchored head and tail (zeroth order), reverse
direction (`flipAlong`), width control.

Not implemented: higher-order anchoring (anchor origins that are anchored to others), shear angle and twist, recursive strokes (strokes made
of applications of themselves, as IFS codes), higher-order strokes (strokes made of strokes), sampled images through the parametric
coordinates, and the pseudo-3D and animation uses.

## Tests and checks

`tests/test_sketch_model.cpp`: `curvyTests --gtest_filter='SketchBrushes*:DeformAlongPath*:DockContext*'`. Headless: `curvy --check-tools`,
`--check-workspace`. The dock layout and the colours of the Brushes box have not been looked at on screen.

## Known gaps

- Break mode restarts the width profile for each piece.
- Pressure for the Pencil tool; link to non-symbol shapes; a grid showing how the art is stretched; the picker as a popup.
- The deform node has no explicit release command (undo, or delete the node).
- The legacy `Resources/curvy.newui` panel was not updated for the dynamic rows; the dock is the real UI.
