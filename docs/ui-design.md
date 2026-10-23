# Main screen: pen in one hand, keyboard in the other

Design notes for curvy's main window, with the mockup in `mockups/main.html` (open it in a browser; the buttons above the frame switch
between the states). Status: proposal, nothing built.

*Workflow storyboard:* `docs/mockups/flow.html` is a static, scrolling walk-through of the whole flow (empty window, New ring, document, creation, styling, transform, booleans and masks, text, guides, structure, artboards, animation, rig, export, close) with one drawn screen per step and links to the interactive benches.

## Premise

Primary hand: pen on a tablet or pen display. Secondary hand: left side of the keyboard (modifiers, letters, number row), never the mouse.
So the rule is: **the pen never travels to a toolbar, and the keyboard never needs the pointer.** Everything reachable by pen and chord,
and the canvas is almost the whole window.

## What the research says

- Procreate keeps chrome minimal, puts actions in a customizable radial QuickMenu, and uses gestures (two-finger tap = undo, pinch = zoom)
  so the pen hand never changes tool to navigate. ([gestures](https://help.procreate.com/procreate/handbook/5.1/interface-gestures/gestures),
  [Procreate Insight](https://procreate.com/insight/2022/gestures))
- Krita's pop-up palette opens at the cursor on right click, for brushes and colour; users report it makes a UI-less setup workable, and one
  contributor notes pie menus suit mouse+keyboard better than pen+keyboard, with a marking menu preferred for tools.
  ([Krita Artists](https://krita-artists.org/t/enhanced-popup-palette-and-more-for-a-complete-ui-less-keyboard-less-workflow/53408))
- Affinity Designer on iPad: the toolbar changes with the mode, a context strip shows controls for the selected tool, held fingers act as
  Ctrl/Alt modifiers for the pen, and a Quick Menu gives context-sensitive commands. Reviewers found on-screen controls easy to misfire.
  ([review](https://www.expertreviews.co.uk/technology/software-services/affinity-designer-for-ipad-review))

Not verified: how Illustrator's contextual task bar behaves; treat the "floating bar near the selection" idea as ours.

## Proposal

### Do we need a toolbar? Not by default.
Tools are chosen by single keys (V select, A direct, P pen, B brush, T text, ...) and by the pen-button menu. The current tool is shown in the
hint bar. Hold-to-switch (Space = hand, Z = zoom, Ctrl = select) so the pen hand never leaves the drawing. A toolbar exists as an option, off by
default (see "Optional docked toolbar and property boxes").

### The top row
A thin row (32 px) across the top holds the app logo and name at the far left, then the document name, then empty space. It sits inside the window, in the place of today's `menuBar` and `mainToolbar` views (which the Classic workspace keeps). It is deliberately empty: reserved
for later (switching views or workspaces, collaboration status, other features). It is the only addition to the permanent chrome besides the hint bar
and rulers.

### Four surfaces, nothing else always visible
1. **Canvas** fills the window. No menu bar, toolbar, tool palette or sidebar by default.
2. **Hint bar** (one line, bottom): current tool, what is under the pen, zoom, and the keys that do something *right now*. Holding Ctrl, Shift
   or Alt swaps it to what that modifier changes (a "which key" strip). This is how the hidden UI stays discoverable.
3. **Context HUD**, a small panel next to the selection, only when something is selected. It replaces the Transform and Live Shape panels:
   X/Y/W/H, fill and stroke, and the parameters of a live shape (radius, points). Tab pins it to the right edge; Esc dismisses it.
4. **Pen menu**: barrel button (arrives as right click) opens a radial menu at the pen tip. Its eight slots depend on what is under the tip:

   | Under the pen | Slots |
   |---|---|
   | empty canvas | Rectangle, Ellipse, Polygon, Star, Line, Freehand, Text, File (hub: shape options) |
   | object | arrange, group, fill, stroke, duplicate, delete, make live shape / edit graph, more... |
   | anchor or handle | corner/smooth, delete point, add point, join, cut, ... |
   | live shape | parameters (opens the HUD focused), convert to path, edit graph |

   Flick in a direction without waiting and the menu never draws (marking-menu behaviour), so frequent choices become muscle memory. "File"
   opens a second ring.

### Space is the context key
Tap Space (pressed and released with no pen drag, under ~250 ms) opens the same radial menu as the pen's barrel button, at the pen position.
Hold Space and drag pans, as in other editors. On blank canvas or a new document the menu is the **creation ring**: Rectangle `R`, Ellipse `E`,
Polygon `P`, Star `S`, Line `L`, Freehand `B`, Text `T`, File. Over the grey workspace (outside every artboard) it is the **document ring**: New artboard (at the pen), New document, Open, Save, Export, Place
image, Fit, Rulers and grid (Document Setup is behind the hub). With **no document open** the workspace is empty and the same press
gives the **New ring**: four common sizes (Letter, A4, Full HD, Square 1080, taken from `NewDocumentModel::presets()`), Open and Recent, with
"Create..." in the hub opening the full New Document dialog. With a document loaded the hub reads "Document..." and opens Document Setup. Over an object it is the object ring; over a point or handle, the point ring. The keys
are printed on the slots, so the ring teaches the shortcuts; once known, hold the key and drag to draw without opening anything. Space only
acts as the context key when no text field has focus, and key auto-repeat is ignored.

### The hub: name and "..."
The centre of every ring is a hub that names the ring (Create, Document, or the object under the pen, e.g. Live Circle) with a "..." under it.
Tapping the hub opens the full dialog for that context: shape options for Create, Document Setup for Document, the properties/appearance panel for an
object. So rings stay at eight direct commands and nothing needs a "More" slot; the rare and the detailed live one tap behind the hub.

### No command palette
Considered and dropped: nobody drawing wants to put the pen down and type. Rare commands (export, document setup, preferences, workspace) live in
the hub dialogs and the File slot, and Alt still opens the classic menu bar for anyone who wants it. The command table in
`sketchwindow.cpp` stays the single list of commands behind both.

### Rulers
Kept, on by default (Ctrl+R toggles): an 18 px strip on the top and left, with markers that follow the pen. They are the one exception to "no
permanent chrome" because they are how you read position and size, and they are pen-friendly: drag out of a ruler to pull a guide, drag a guide
back to remove it. The pen's coordinates are also in the hint bar. The canvas already has the ruler and guide code.

### Panels become overlays on demand
Layers, artboards, library, swatches, graph editor: shown by a key (L, ...) as an overlay on the left or right edge and hidden by the same key
or Esc. Hover at the screen edge does *not* open them (misfires with a pen hovering near the bezel); an edge flick with the pen can come later.
Tab hides everything for a clean canvas.

### Keyboard hand
- Letters pick tools; Ctrl+Z/Shift+Ctrl+Z, Ctrl+C/V, Delete unchanged.
- Hold-and-drag scrubbing: hold a key, drag the pen to change a value live (S = stroke width, O = opacity, R = rotate, [ ] brush size), release to
  commit as one undo step. The numeric scrubbers already in the Transform panel use the same mechanism.
- Number row: recent colours; Shift+number: layers. Hold Space and drag pans (tap opens the context menu).

### Property scrubbing (decided; mockup `docs/mockups/scrub2.html`)
Borrowed from ZBrush (hotkey on a slider, hold and sweep, Shift snaps, bracket keys step, each tool keeps its own values):
- **Hold a key and move the pointer** to change one property of what is under the pointer; release to keep it (one undo step). Shift snaps to preset
  values; `[` and `]` step.
- **Scrub axis (setting):** vertical by default (up increases), which suits pens, where sideways wrist travel is awkward; horizontal stays
  available as a user setting for mouse users. In the lab it is `LabCanvas::scrubAxis_`, with no UI to change it yet.
- **A fixed slider** appears where the key went down and stays put; only its value changes with the pointer's travel along the scrub axis from there.
  Nothing follows the pointer.
- **Context from the pointer:** the part under it is outlined and picks the keys: outline (W width, O opacity, H hue), solid fill (O, H, L lightness),
  gradient (A angle, H hue shift, O), between characters (K spacing, Y height, B baseline), on a glyph (Y, H, O). Letters are placeholders and
  remappable; the same letter keeps its meaning where it can.
- **One property at a time.** Two keys at once (second driven by vertical travel) was tried and rejected as hard to control.
- **Hints:** names and keys in full at first, then keycaps with a small icon only as the user learns them, with an Off setting. A hint retires after
  a few uses (see the lab's use counters). The status bar is a reference line only; the primary hint is at the pointer.
- **Pen display vs tablet:** on a pen display the hand covers the lower side, so hint cards and ring slots avoid it (handedness setting).
- A cursor drawn as a nib at the stroke width, with a + at its centre, was liked but is not the first thing to build.

### Colour, gradient and stroke editing (mockup `docs/mockups/appearance.html`)
Each editor opens at the pen from a held key (or from the object ring), works with the pen alone, and commits on release as one undo step. The
popups use the translucent card style (`#17181bf0`), which is liked and is the style for all floating cards.
- **Colour (hold C):** a hue ring with a saturation/brightness square inside. Preview splits old and new. Number row 1-8 picks recent colours,
  Space toggles fill/stroke while open, hold I samples from the canvas. The H/S/L scrub keys stay for nudging.
- **Gradient (hold G or via the fill):** edited on the shape. The gradient line and stops are drawn over it; a small card holds the type and the
  selected stop's hue and lightness. Tap a stop to select, drag it along the line, tap the line to add a stop, drag a stop off the line to remove it,
  drag a line end to move or rotate. Types: Linear, Radial, Angular, Points (only Linear and Points are mocked).
- **Gradient points:** a Points gradient is a set of colour points placed on the shape (tap the shape to add, drag to move, drag off the shape to
  remove). Stops, line ends and points are stored in the **shape's own coordinate space**, not the page's, so when the shape is moved, rotated, skewed
  or warped the gradient geometry is transformed with it. The mockup's Rotate/Skew/Stretch sliders demonstrate this. Consequence for the code: gradient
  geometry belongs to the shape's fill (and its transform), and handles are drawn by mapping local points through the shape's current transform.
- **Stroke (hold W):** one card with width (scrubbed), alignment, cap, corner, dash and width profile (Even, Taper, Swell, Pressure) as tap chips;
  Shift snaps width to 1 2 4 8 16 32. Profile editing by dragging points comes later.
- **Appearance hub:** the "..." under the object ring opens a docked list of all fills and strokes with opacity, for stacked fills and the rare cases.
  Swatches there open the same colour popup, so there is one colour editor.

### Pen path drawing and editing (mockup `docs/mockups/pen.html`, proposal)
A test bench on pointer events (hover badges, pressure and the eraser end work with a pen; right button stands in for the barrel button).
- **Pen (P):** click = corner, drag = smooth (the drag pulls the outgoing handle, the incoming one mirrors it). Hover the first point to close, the last
  point of an open path to continue. With paths present the same tool adds a point on a segment and deletes a point, so the tool rarely changes. A badge at
  the tip names what a touch will do (visible while the pen hovers), and a dashed rubber band previews the next segment.
- **Keyboard hand while placing:** Shift constrains to 45 degrees, Alt breaks the handle (cusp), Space moves the anchor instead of pulling the handle.
  Enter or Esc finishes, Backspace removes the last point.
- **Direct (A):** drag points, handles or segments (a dragged segment bends, the handles adapt); marquee on empty canvas; double-tap flips corner/smooth. Smooth
  keeps handles collinear, symmetric keeps them equal, Alt-dragging a handle makes a corner. Corner points draw as squares, smooth as circles. Dragging an end point of an open path onto its other end (a ring shows within 10 px) closes the path on release and merges the two points. Dragging an end point onto the end of another open path joins them the same way (ring and "join" badge; Alt while dragging suppresses closing and
  joining); the two `bezierPath` nodes become one, the junction keeps the handles that face into each path and averages pressure and width, and the first path's Stroke
  settings win. With the pen tool, clicking the end of another open path while drawing joins to it and ends the stroke. A `fitCurve`-driven path must be baked first.
- **Point ring** (barrel, or tap Space over a point): Smooth, Corner, Symmetric, Delete, Add mid, Cut, Close, Reverse. The pen's eraser end deletes the point it touches.
- **Segment ring** (barrel, or tap Space over a segment but not a point): Add point (at the pen), Midpoint, Delete segment (an open path splits in two, a closed one
  opens), Straighten (removes the handles on that segment), Add handles (gives a straight segment handles to pull), Cut here (adds a point and cuts), Select ends, and
  Divide x3. So the pen over a point gets the point ring and over a path between points gets the segment ring.
- **Freehand (B):** keeps the raw samples and fits curves (RDP then tangent-continuous beziers, corner angle decides breaks). Hold F and move vertically to
  re-fit live (up simplifies), shown in the fixed scrub card with the point count; one undo step. Editing by hand drops the samples.
  The stroke is a `fitCurve` node (input: raw samples; parameters: Smooth tolerance and Corner angle) feeding the `bezierPath`; the node's parameter box carries the two
  sliders and F scrubs the first. The HUD is a chain of node boxes (fitCurve, bezierPath, Fill, Stroke). While fitCurve feeds the path its
  anchors are an output and read-only; editing is blocked with a hint until "Bake to anchors", which drops the samples and the fitCurve node. The toolbar-less defaults for new strokes live in settings.
- **Pressure and tilt (pen only):** freehand records pressure and tilt per sample. `fitCurve` averages them into `pressure` and `tilt` point attributes on each
  anchor (jitnodes geometry already has named point attributes) and does not decide a width. The `Stroke` node maps pressure to width (Min/Max width, a Response
  curve shown as a small plot, Width smooth; header sliders only set its defaults). In the pen tool the pressure while a point is placed (the peak while dragging) is
  stored as that point's pressure. Hold W over a point and move vertically to set an explicit `width` override attribute; the HUD shows it and "Use pressure" clears it.
  Pressure and tilt survive Bake, so the response stays changeable afterwards, and downstream attribute nodes (taper, noise, remap) can modify them. A mouse gives
  an even width and no tilt. A **nib preview** at the pen tip shows size from pressure (dashed minimum while hovering), stretches and turns with the lean, and a tail
  shows the pen body's direction; recorded tilt shows as a small dashed nib on anchors. Tilt does not change the path: it is for the calligraphic and star brush
  angles of the Brushes tab.
- **Width model (decided):** width is data on the path, and the Stroke node turns it into geometry. The `bezierPath` stays a clean centerline whose anchors carry
  `pressure` and `tilt` attributes (and an optional `width` override); pressure never moves the path itself, so points stay editable and a `fitCurve` can still re-fit. The Stroke node (profile, caps, joins, dash,
  alignment) outputs the **outline** as a closed filled shape offset from the centerline by the width at each point, which is what the mockup draws. When all widths are
  even and there is no brush or dash it may hand the path to the renderer's native stroker as an optimization (Blend2D strokes at one width only). Reasons for outline
  geometry: SVG has no variable-width stroke so export needs it, and brushes, skeletal strokes, deform-along-path, booleans and inside/outside alignment all consume
  geometry. Baking the width into the path was rejected because it loses the centerline (no later width change, no re-fit, nothing for tilt-driven brushes to read).
  Costs to plan for: the offset folds over itself on tight curves with a large width, joins and caps, caching the outline, and hit-testing the stroke against the
  outline. The mockup's 16-sample offset has none of that. "Expand stroke" (outline to a plain filled `bezierPath`, one way) is a later command, not mocked.
- **Graph:** a path is one `bezierPath` node (anchors with relative in/out handles, a point-type tag, closed flag). Cut on an open path splits the node into two; the
  context HUD and a peek chip show the same node. Not mocked: pressure to width, curvature pen, pan/zoom, joining two open paths.

### Text and fonts (mockup `docs/mockups/text.html`, proposal)
- **Text is a node chain**, `Text` > `Glyphs` > `Fill`, not a separate object kind. `Text` holds the string, font, style or variable axes, size, tracking, leading,
  alignment and either a box (area text) or a path input (text on a path, the shared `bezierPath` with a start offset and a side). `Glyphs` outputs one primitive
  per glyph with outline geometry and attributes (advance, kerning, baseline shift, rotation, glyph index), so a hand kern is an attribute override that
  downstream nodes can modify, like the pen's pressure attribute.
- **Creating (T):** click = point text, drag = area text (wraps, red + marks overflow), click a curve = text on path, click text = edit. Select tool (V): drag moves,
  area text has box handles, path text has a handle that slides it along the curve, double-click edits.
- **Fonts (F):** tap F over text for the font card (search, filters Document / Recent / Favourites / Serif / Sans / Mono / Script / Display, each family drawn in
  itself, hover previews on the real text, click or Enter keeps, one undo step). Hold F and move vertically to step through the installed fonts with the text
  updating live. A missing font keeps its name, renders with a substitute and shows Replace in the HUD (same list as the Fonts tab of the document card).
- **Hold-key scrubs:** S size (Shift snaps to common sizes), K tracking, L leading, E weight and N width (variable fonts only; the sample uses Bahnschrift).
  With the pen between two letters K scrubs the kerning of that pair, marked by an amber tick, per the "between characters" context above. Typography guides
  (baseline, x-height, cap height, measured from the font) can be toggled and show while scrubbing size and leading.
- **Convert to outlines** is a bake like `fitCurve`: `Glyphs` becomes plain `bezierPath` nodes, the string and font leave the document, editing is blocked with a
  hint, undo restores the text.
- **Space ring for text** (tap Space or the barrel button over text; in `text.html`): Edit, Font (opens the font card), Style (cycles Regular / Bold / Italic, or the
  weight of a variable font), Align (cycles), To path / Detach, Convert to outlines, Fill (cycles swatches), Clear kerning; the hub "Text ..." opens the full character
  and paragraph panel. Over empty canvas or a curve with no text the creation ring's Text slot makes point text or text on that path. While a text field has focus Space
  types a space, so the barrel button is the only way to the ring during editing (the rule in "Space is the context key").
- **Kerning** (three routes, one attribute): Illustrator-style Alt+Left/Right at the caret in the text (20/1000 em, Ctrl+Alt times five; over a selected range the step is
  added after every selected letter, which is how tracking of a range is stored), hold K with the pen between two letters (the scrub above), and a **kerning view** toggle
  that puts a draggable tick on every gap of the selected text. The result is a per-glyph kerning attribute added on top of the font's own pair kerning (**Auto kern**:
  Metrics, or Off). Edits to the string shift the attribute with the text. As remembered from Illustrator (not re-checked): kerning applies at the insertion point,
  Metrics / Optical / a typed value are the choices in the Character panel, and the same arrow keys change tracking when text is selected. Optical kerning is not mocked.
- **Risk:** Blend2D loads fonts and returns glyph outlines, but its shaping is basic (kerning only, not verified), so ligatures, complex scripts and
  right-to-left text may need HarfBuzz. Not mocked: OpenType features, paragraph and character styles, threading, vertical text, per-character styling, glyph
  palette, text on closed shapes.

### Artboards and export (mockup `docs/mockups/artboards.html`, proposal)
- **Artboards are crops, artwork is shared.** All artwork lives on one canvas. An `Artboard` node (name, rect, background, bleed) decides what an export contains, so
  artwork can overhang an artboard and is cut off in the export. An object belongs to an artboard for moving purposes when its centre is inside it; a switch decides
  whether moving, duplicating or arranging an artboard takes that artwork with it (it moves the objects' transforms, there is no parent/child link).
- **Editing:** Artboard tool (O): drag the workspace to create, drag an artboard to move it (Alt-drag duplicates with its artwork), handles resize, edges snap to the other
  artboards with amber guides. In the Select tool an artboard is picked by clicking its empty area and moved by its label. The HUD (a node box) holds name, presets,
  X/Y/W/H, swap orientation, Fit to art, background (white / transparent / dark), bleed and the order, which is also the PDF page order. Hold W or H over an artboard and
  move vertically to scrub its size; Shift snaps to common sizes and the card names a matching preset.
- **Space rings:** over an artboard (always in the Artboard tool; in the Select tool an object under the pen gets the object ring first, so a full-size background
  rectangle is bypassed through the artboard's label or the Artboard tool): Export, Quick export, Fit to art, Duplicate, Rename, Orientation, Background, Delete. Over the
  grey workspace, the **document ring**, identical in slots and keys to `main.html` state 3b: New artboard A, New document N, Open O, Save S, Export E, Place image I, Fit 0, Rulers and grid; the hub is Document / Setup.... Arrange all is a button in the artboard side box, not a ring slot. The barrel button opens the same rings.
- **Export card** (Ctrl+E, the rings, or the HUD): what to export (everything, or the selection), a checklist of artboards and of objects marked **Export as asset**, and the
  outputs: each a format + scale + options (SVG responsive / outline text, PNG and WebP transparent, JPG and WebP quality, PDF with bleed) with presets (Web, App icon,
  Social, Print) and a naming template (`{doc} {artboard} {n} {scale}`). The resulting files are listed with thumbnail, pixel size and byte size; Save downloads one,
  Export N files all. Quick export saves the first output of the selected artboard or asset without opening the card.
- **The export settings are nodes.** An `Export` node (input: artboards and/or objects, parameters: output rows and name template, output: files, no geometry) belongs
  to the document, so reopening the file and pressing Quick export or Export all repeats it. It evaluates on demand, never while editing, through the canvas renderer
  (Blend2D) at the requested scale; SVG export needs the stroke-outline and text-to-path steps from the stroke and text notes, plus the embedded graph as an option.
- **No document open:** the workspace is empty ("Nothing open") and tap Space (or the barrel button) gives the **New ring**: Letter, A4, Full HD and Square 1080 (one
  tap creates the document), Open, Recent; the hub "Create..." opens the full New Document dialog (name, size presets or a custom size in px / in / mm, orientation,
  number of artboards, background, colour mode, preview). With a document open the document ring's New document slot opens the same ring, and its **New artboard** slot opens an **Artboard ring** (the same four
  sizes, Same size as the selected artboard, Custom) centred on the pen; the view zooms to fit. So the empty-workspace press is about documents when none is open and
  about artboards when one is (reference: `main.html` state 3c for the New ring). Hubs are tappable in every ring (Document ring: Setup...).
- **Mockup status:** it really builds a cropped SVG per file and rasterises through a canvas for PNG, JPG and WebP, so the sizes are real; PDF is listed but not produced.
  Not mocked: slices (an export region that is not an artboard), colour profiles, pan and zoom, a folder picker, the document card's artboard list, a progress view.

### Overlays: perspective guides and tracing paper (mockup `docs/mockups/overlays.html`, proposal)
- **Overlay = something that helps you draw and has the Reference role:** never exported or printed, with its own visibility, opacity and above/below order. Two kinds:
  **parametric guides** (perspective; isometric grids and symmetry axes would be the same kind) are small nodes with draggable handles that output guide geometry, and
  **reference layers** hold an imported image and reference shapes (ordinary paths with the Reference role, promotable to art with "To art"). The Export node skips the
  Reference role, so it needs no special case; the mockup's "preview export" shows the page with art only.
- **Perspective guide:** one, two or three vanishing points. The first two slide along the horizon (the horizon has its own handle), the third is free; rays fan out at a
  chosen step, 2-point can add verticals. In 1- and 2-point perspective verticals stay vertical (and in 1-point, horizontals), so "vertical" and "horizontal" are extra
  directions in the picker.
- **Constraining the pen:** each segment starts at the last point. Hold Shift to lock it to the nearest direction (an amber ray shows which), hold 1-4 to choose the direction
  (listed in the hint bar), or leave soft snap on (a few degrees, a setting). Without a perspective guide Shift is the usual 45 degrees.
- **Snapping:** reference shapes, art and the vanishing points are snap sources, each overlay with its own switch. On a constrained ray the point lands on the intersection of
  the ray with a reference or art segment, or on an anchor that lies on the ray; unconstrained it snaps to an anchor, then the nearest point on a segment, then an
  intersection. The badge names what it snapped to. The same query is what artboards use to snap to each other.
- **Tracing paper:** the image sits between the page and the art (or above, per overlay) at an adjustable opacity; unlocked it can be dragged, scaled, flipped and fitted.
  "Draw into: Reference" sends pen strokes to the reference layer (construction lines that snap but never export). A generated street corner whose edges converge on the
  default vanishing points is the sample, so turning the guide on shows the match.
- **Space ring (overlay ring):** 1-point, 2-point, 3-point, Image, Draw into, Snap, Hide overlays, Remove active. In the document ring the "Rulers · grid" slot would become
  Overlays. Hold O and move vertically to fade the active overlay; hold H to peek without overlays.
- **Costs and open points:** a large reference drawing needs a spatial index so snapping stays smooth; embedding an image bloats the SVG while linking breaks when the file
  moves (embed by default with a size warning); an image needs a two-point scale calibration for real units (a slider here). Perspective guides pair with the planned
  deformers (a perspective warp is a deformer node) and animation onion skin would be another reference overlay. Not mocked: isometric grids, symmetry, several perspective
  guides at once, curved strokes, outline integration.

### Animation, deformers, switches, drawn frames, layers and EXR output (mockup `docs/mockups/animation.html`, proposal)
Goal (user, 2026-10-20): animated output as **one EXR per frame**, with optional extra layers (AOVs) produced by nodes in the network, plus rig-and-pose work.
Research on Moho, Toon Boom Harmony and OpenToonz showed that 2D animation has two workflows that 3D tools lack: **drawn frames** (xsheet exposure, timing on 1s/2s/3s, onion skin, line boil) and **cut-out rigs** (pegs, parent-child, deformers, drawing substitution / switch layers, Moho's smart bones).
- **Keys:** float keys are eased per key (bezier presets plus a two-handle curve, Hold); **int/enum keys are stepped** (square keys), used by Switch and Drawings nodes. Auto-key: a keyed parameter always writes a key when edited, an unkeyed one only with auto-key on.
- **Deformers are nodes** (Squash, Bend, Wave) with a per-point weight source: uniform, along path (u), pinned ends, pressure.
- **Switch node:** integer picks one of several drawings (Face mouth and eyes). The Face is a child of the Ball and follows its transform and squash.
- **Drawings node + Timing:** a library of drawings, cycled with a Timing of 1s to 4s; the timeline shows an exposure strip (xsheet laid on its side) merged where a drawing is held; onion skin shows neighbouring drawings.
- **Poses** snapshot parameters including switches; apply them like an edit (keys per the auto-key rule). Poses are per type (`tail.bend`), applied to the selection or every object of that type.
- **Gestures:** tap T toggles the timeline, hold T + sideways scrubs time; hold S/B/W + vertical scrubs squash/bend/wave (writes a key at the playhead); K keys the selection, Shift+K removes; arrows step frames and jump keys; Enter plays; Space is the animation ring (Play, Key, Unkey, Onion, Timeline, Store pose, Rest pose, Output).
- **Scale:** the canvas draws everything visible; handles, trajectories, weights and onion skin belong to the selection. Timeline rows are **Selected** (keyed parameters of the selection) or **All** (tree of groups and objects with summary rows, collapsible, scrolling). Many identical things should be a scatter/instance node, not many objects.
- **Layers (After Effects style) as a view over the graph, no separate Layers card:** eye, solo and lock sit on the timeline rows (and the xsheet column heads); groups have eye and solo; in/out bars on object rows (drag ends to trim, middle to slide); the parent shows as ↳ after the name. A group is the precomp (a sub-graph with its own time offset in the real app). Visibility and solo affect the export.
- **Output node (EXR sequence):** beauty RGBA linear premultiplied always, AOV layers from nodes: motion (px/frame), id (stack index), and attribute layers (point attribute rasterized as data). The mockup writes a real half-float uncompressed multi-layer EXR for the current frame.
- **Code consequences:** jitnodes needs bezier easing and typed (int/enum/colour) keys; Blend2D is 8-bit so AOVs need a float rasterizer path (scanline fill with interpolated per-vertex attributes) while beauty stays on Blend2D; per-pixel motion vectors come from cooking t and t+1; range output cooks each frame (`setTime` dirties only time-dependent nodes) and writes on a worker.
- **Dopesheet** (the timeline drawer): the selection is a set of (parameter, key) pairs, so one set of operations serves float keys, switch keys and stepped keys. Click, shift-click and box-select (a box over an object/group summary row selects everything under it), All, @Frame, row and summary-tick selection. Drag to retime with **snap** (playhead, range ends, other keys; Alt bypasses); hold **G** to move and **E** to scale about the playhead (same hold-and-move family as the scrubs); Ctrl+C/V/D copy, paste (onto the selected objects of the same type) and duplicate; Ins/Rem ripple frames; Smooth/Linear/Step for the selection; [ ] work range that playback and Output honour. A **Curves** toggle shows the same selection as normalized curves with two-axis key dragging.
- **Full Dopesheet view (Tab, or the Full button / ring slot):** the timeline drawer grows to fill the window: tool row, a band with a live preview of the canvas and a readout of the selected object's values, then the sheet. Two orientations of the same selection model: **Dope** (horizontal, channel labels on the left) and **Xsheet** (a vertical spreadsheet: frames down, one column per channel; float cells show the value every frame with key cells highlighted, switch and drawing columns show choices with hold lines). Xsheet editing: click a frame number to move the playhead, drag across cells and type a number (drawings or switch choices; the previous value is restored after the range), double-click a cell to type a float value (creates a key), drag key cells down/up to retime, column-head eye/S toggles. The drawer is for quick edits while working on the canvas; Esc or Tab closes the full view.
- **Editable exposure (xsheet):** a Drawings node can be baked from its cycle to manual cells (`exp` per frame): click/drag cells and type 1-6, drag a run's right edge to extend a hold, Hold previous, Back to cycle. Ripple shifts the cells too.
- Not mocked: editing a drawing, tangent handles on curves, vertical xsheet, markers, lattice/path-deform nodes, morphing between shapes, sub-graph time remap, range output, EXR compression.

### Rigging: bones, binding, weights, IK and drivers (mockup `docs/mockups/rig.html`, proposal)
Research (Moho bones and Smart Bones, Toon Boom pegs/hierarchy/deformers, OpenToonz Plastic) shows two rig styles that coexist: **cut-out** (rigid parts bound to one pivot, parented) and **bone rigs** (smooth skinning, IK, constraints, smart controls).
- **Skeleton = transform nodes.** A bone has a head (pivot), length, parent, an attach fraction along the parent and an optional sideways offset. The hierarchy is ordinary graph parenting; each bone's rotation is a normal channel, so it shows up in the timeline/dopesheet and keys, copies and ripples like any parameter.
- **Bind node (skinning).** Points + bone transforms + per-point weights -> deformed points (linear blend skinning, a per-point wrangle, JIT-friendly). Weights are a point attribute (same system as the Bend/Wave weights). Rigid bind = weight 1 on one bone (the cut-out peg); smooth bind = painted weights over several bones. Auto weights from distance along a chain give a first pass.
- **Weight painting with the pen:** pen pressure is brush strength (mouse = half), hold B + vertical move resizes the brush, a heat map (blue 0, red 1) of the active bone on the vertices, modes Add / Subtract / Smooth, weights stay normalized, a stroke is one undo step.
- **IK = solver node:** per limb an IK amount (keyable, so FK/IK blend), a target and a bend direction. Dragging a wrist/ankle ring switches the limb to IK from where it is (no pop); To FK bakes the solved angles into the bone rotations. Analytic two-bone now; a chain solver (FABRIK) for tails and spines.
- **Driver (smart bone):** one control mapped to several targets: the Head turn dial adds rotation to head and neck and picks one of five face drawings through a stepped switch. Mappings can be disabled or scaled. Poses (animation bench) are the stored-snapshot cousin.
- **Editing the skeleton:** the Bone tool shows the art unposed; drag from a tip to add a child, or Edit tip to move a bone (children keep their orientation); every edit re-binds so the art stays where it was drawn. A new bone can be given a part with "Bind rigid to bone".
- **Rig as an asset:** a group with published controls (dial, IK rings, master move). Posing = keying those controls, so the dopesheet, poses, copy/paste and the EXR output work unchanged.
- **Code:** needs Bone, Skeleton (publishes world transforms), Bind, IK and Driver nodes; jitnodes is polyline-only, so skinning a bezier path deforms anchors and handles (needs bezierPath). Not mocked: mesh warp, constraints (aim, parent-to-world), bone dynamics, pose mirroring, several-dial smart bones.

### Selection and transform (mockup `docs/mockups/transform.html`, proposal)
Test bench with six shapes, a group and a HUD; headless-Chrome tested (48 interaction checks), not hand-tested.
- **Transform is a node.** Each object has one Transform node holding a single affine matrix (the HUD shows it decomposed as X/Y/W/H/Rot/Skew). Moving, scaling, rotating and skewing edit only that matrix, so a Rectangle stays a Rectangle. **Bake** pushes the matrix into the geometry and yields a plain path (`bezierPath` in the editor). Groups are subnets with their own Transform: dragging a group edits only its matrix, double-click enters it (the rest dims, Esc leaves), Ungroup multiplies the group matrix into each child.
- **Frame.** One object gets an oriented box that follows its rotation and skew, so scaling stays along its axes; several objects get the axis-aligned box of all of them. Every drag is a world-space delta matrix (`F * about(S, anchor) * F^-1`) multiplied onto each object's start matrix, so nothing accumulates and Esc cancels.
- **Handles.** Corner/edge handles scale (Shift keeps proportions, Alt scales from the centre, and a drag can flip through zero); just outside a corner rotates (Shift 15 degree steps; Alt rotates a copy); Ctrl-drag an edge handle skews. The **pivot** crosshair is the origin for rotate and the scrubs: Ctrl-drag it from the centre (plain drag once off-centre), it snaps to the nine reference points, double-click resets; the HUD has the same nine-point reference picker and its X/Y read that point. A plain drag from the centre must move the object, which is why the pivot needs Ctrl at first.
- **Selection.** Click, Shift-click toggles (a Shift-press on an already-selected object waits for release, so Shift can still mean axis lock while dragging), marquee on empty canvas: left-to-right selects what it fully contains, right-to-left what it touches (no modifier, pen friendly; the mock tests bounding boxes, the editor should test outlines). Ctrl+A, arrows nudge (Shift x10).
- **Move.** Shift locks an axis, Alt drags a copy. **Smart guides** snap edges and centres to other objects, the artboard and an optional 20 px grid (also while scaling an unrotated frame); amber lines show the match.
- **Transform again.** The last delta is kept; Ctrl+D duplicates and applies it once more (initially a small offset), so Alt-rotate a copy then Ctrl+D repeats builds a ring, move-copy then Ctrl+D a row.
- **Scrub keys.** Hold R (rotate), S (scale) or K (skew) and move vertically; fixed card, around the pivot, Shift snaps 15 degrees / 10% / 5 degrees, release is one undo step. Letters are placeholders (S and K are already used by other contexts).
- **Rings.** Over a selection: Group/Ungroup, Duplicate (again), Delete, Flip H, Flip V, Align (sub-ring: T/R/B/L/Middle/Centre/Space H/Space V), Arrange (sub-ring: front, forward, back, backward), and a contextual last slot (E): Enter group when one group is selected, Exit group when inside a group, else Bake (Bake stays in the HUD); hub names the selection. Over empty canvas: Select all, Invert, Deselect, Grid, Smart guides, Scale strokes, Exit group, Reset.
- **HUD.** The chain `shape -> Transform`, reference picker, X Y W H with a keep-proportions lock, Rot, Skew (single object only; Rot is a step for several), Flip, Reset, Bake, Duplicate again, plus Align (to selection or artboard; a single object aligns to the artboard), Arrange and Group buttons.
- **Strokes.** "Scale strokes" off keeps widths constant while geometry scales (here `vector-effect: non-scaling-stroke`; in the editor a Stroke node flag dividing by the matrix scale).
- **Code consequences:** a Transform node (affine, maybe animatable parts later) on every object, a decomposition helper for the HUD, a delta-matrix transform tool, world AABB / oriented-frame queries on geometry for marquee, snapping and align, and group nodes as subnets.
- **Not mocked:** pan and zoom, rotating a multi-selection frame, equal-spacing guides and distance labels, snapping to anchors, live-shape handles (corner radius), numeric expressions, per-object pivot memory.

### Booleans, clipping and masks (mockup `docs/mockups/booleans.html`, proposal)
Test bench with a live Boolean node, shape builder, clip and opacity masks, blend modes and per-operand editing; headless-Chrome tested (68 checks), not hand-tested. Geometry is flattened polygons clipped with the MIT `polygon-clipping` library (`docs/mockups/tools/`).
- **Boolean = node with ordered operands.** First input is the base; each later one has a mode (add, subtract, intersect, exclude), folded in order. Union = all add; Subtract = bottom shape minus all others ("Minus Front"); Reverse order makes the top the base ("Minus Back"). Operands keep their own geometry, transforms and appearance (a hidden operand is skipped); the result is a path with the node's own fill/stroke (copied from the base). Booleans nest, since a node's geometry is just a function of its inputs. Empty results stay selectable from the HUD list and say so.
- **Live editing.** Enter the node (double-click, E) to see operands as ghost outlines coloured by mode (blue add, red subtract, green intersect, purple exclude) and move/rotate/scale them with the result updating; the ghosts also show when the node is only selected. The HUD lists operands with mode select, eye, reorder and Enter/Separate/Expand/Reverse.
- **Separate / Expand.** Separate restores the operands as separate shapes (matrix multiplied back in). A mask shape gets its original fill back (the gradient belonged to its mask role); geometry edits stay. Expand bakes the result into a plain compound path (holes by even-odd) and drops the operands.
- **Shape builder (B) and Divide are destructive commands**, not nodes: they split up to six selected shapes into the regions of their overlap, then drag across regions to merge them into one shape, Alt-drag deletes them, Enter or Divide keeps every region; a region takes its appearance from the top-most shape covering it.
- **Clip.** Clip node = Content inputs + one Mask input, rendered with an SVG `clipPath` (no geometry computed, so strokes, gradients and nested nodes clip correctly). Entering shows the unclipped content faded and the mask outlined in orange. The top-most selected shape becomes the mask.
- **Opacity mask.** Same structure with a luminosity mask (SVG `mask`): white shows, black hides, greys fade; the mask shape defaults to a white-to-black gradient, Invert flips it (a filter in the mask). Entering lets you edit the mask shape's fill (solid or gradient with angle) in the HUD.
- **Blend and opacity** are per-node (normal, multiply, screen, overlay, darken, lighten, difference, soft light) in the HUD and a Blend sub-ring; the mask applies first. Nodes can also be hidden or locked (HUD list, ring).
- **Rings.** Several shapes: Union, Subtract, Intersect, Exclude, Divide, Clip, Opacity mask, Shape builder. One shape: Duplicate, Delete, Front, Back, Blend, Hide, Lock, Exit. A Boolean/mask node: Enter, Separate, Expand (Boolean only), Reverse (Boolean) or Invert (opacity mask), Duplicate, Delete, Blend, Hide. An operand inside a Boolean: Add, Subtract, Intersect, Exclude, Hide, Duplicate, Delete, Exit. Empty canvas: Select all, Invert, Deselect, Exit node, Operands on/off, Undo, Redo, Reset.
- **Code consequences:** a `boolean` node (ordered inputs with a mode each, geometry out), `clip` and `opacityMask` nodes (content plus mask input, render-time effects rather than geometry), per-node blend/opacity/visibility/lock, and a way to evaluate a node's world outline for hit testing and marquee. jitnodes is polyline-only today, so a first boolean runs on flattened polylines; the real thing needs exact bezier clipping (as Paper.js does) or flatten, clip and refit with `fitCurve`. Strokes and open paths cannot take part until outlined (Outline stroke command, not mocked). The float raster path planned for EXR is also what a luminosity mask needs.
- **Not mocked:** hand-made compound paths, Outline stroke, knockout groups and isolation, on-canvas gradient mask handles, boolean of text, anchor editing of an expanded result (pen bench), full transform handles (transform bench).

### Optional docked toolbar and property boxes
The default stays the plain canvas, but two switches (both **off by default**, in the settings ring and in the mockup's header) bring back a
conventional layout: a **toolbar** docked flush to the left edge, full height (tool icons with their key letters printed on them) and **property boxes** docked flush to the right
edge, full height and scrollable (Transform, Fill, Stroke, Opacity, the live shape's parameters, Effects; same style as the popups). Both are
always translucent over the artwork (panel colour bgSurface at 60% opacity), so the canvas keeps its full size. A blur behind them is not done: Blend2D has no built-in blur, and a software box blur of a cached copy of the canvas would need its cost checked while panning and zooming first. The property boxes' text size is adjustable (10-20 px in the mockup, default 11) and the panel width scales with it, for pen displays where the
default is too small to hit. They show the same values the popups edit, so both routes always agree. This serves mouse users and anyone who prefers visible controls without changing the pen workflow. The same switches apply to any later
toolbars and panels, not only these.

### Tool options (mockup `docs/mockups/tooloptions.html`, proposal)
Every tool may have **defaults for the next shape it makes** (Star points and inner radius, Rectangle corner radius and from-centre, Polygon sides, Freehand smoothing, Pen rubber band and auto add/delete, Text size, Eyedropper sample, Select marquee mode). They never change a shape already on the page: that is the HUD's job.
- **Toolbar on:** a single click picks the tool, a **double-click** opens its options in a small card beside the button (the Photoshop/Illustrator habit). A small amber **dot** at the corner of the button marks tools that have options (a Maya-style option box was tried here and judged too much for a 36 px button); a tool with none (Line, Zoom in the mockup) has no dot and does nothing on double-click.
- **Ring (toolbar off, the default):** slots that have options carry the same **option box** at their outer end (away from where a flick lands) and it is a **tap target**: tap the box and the card opens, no hold needed. The slower routes also work: **press and hold the slot** (about 0.4 s, a progress bar fills along the slot; the delay is a setting), the **barrel button on the slot**, or **Shift + the slot's key**. A plain tap or flick on the slot still picks the tool, so frequent use is unchanged. The box must be a proper pen target (about 22 px with its hit area). A "⋯" was tried first and liked as a marker but it is not a target; the option box replaced it. The hub was rejected for this: with a pen you cannot hover a slot and tap the hub at once.
- **The card** is the same translucent card as the other popups, with a live preview of the result (star points, corner radius, how many points a smoothed stroke keeps), the controls, Reset and a line saying it is for the next shape. Esc, a click elsewhere, the × or picking another tool closes it. Touch screens would use long-press for both routes.
- **Open points:** the toolbar prints Pen on P while the ring's P is Polygon; ring keys are only live while the ring is open, so there is no real clash, but the toolbar needs its own letter for Polygon (Y in the mockup). Where the defaults are stored (profile or document) is undecided.

### The graph is the document (mockups `nodes.html`, `nodes-experiments.html`, `nodes-girl.html`)
Nodes are how the document is stored and represented, not something shapes are converted into. Drawing a shape, adding a fill or grouping creates
nodes as the user works; the canvas, the node view and the property boxes are all **views of the same graph**, and the canvas shows the display
node. So the node UI is one more view, never a separate mode. The mockups explore where that view lives:
- **Peek chip** beside a selected shape (its chain, e.g. Star > Transform > Copy), **bottom drawer**, **floating window**, **hold-key lens** (hold N,
  the graph over the whole canvas), **cards on the canvas**, and **zoom into the shape** (entering a subnet). Likely combination: cards or the chip for
  small graphs, drawer or window for medium ones, the lens for a quick look, zoom-in for subnets.
- The property box is the parameter list of the selected node (each parameter with `fx` and `key`), the same boxes as everywhere else.
- **At a realistic size** (`nodes-girl.html`: 341 shapes, 5434 points, 56 fill colours, about 416 nodes): the flat graph is a hairball and has to be
  searched and selected rather than read. What keeps it workable: subnets made as the user groups (and from layers or groups in a loaded file);
  thumbnails on every node, dropping to thumbnails only (no text or ports) when zoomed out; selection linked both ways with the canvas; a filter that
  matches group names, colours and node types; Fit, a minimap and breadcrumbs. Graphs this size want the lens or a large window, not the drawer.
- Not yet explored: auto-layout, collapsing a chain into one node, and a palette node feeding many Fill nodes.

### Document structure views (mockup `docs/mockups/structure.html`)
Metadata, layers, groups, shapes and artboards are views of the same graph, shown the usual way so they are familiar. Same translucent look as the
property boxes.
- **Outline (L):** one tree of layers, groups and shapes, with a thumbnail, eye, lock and count per row. A layer or group is a subnet and a shape row is
  a node, so the tree is the node graph's grouping seen as a list. Click a row to select, click the artwork to find its row, hover previews it on the
  canvas, and a filter matches names, colours and groups. Needs virtual scrolling and the same thumbnails as the node view (341 shapes in the mockup).
- **Layer strip (pen):** a narrow strip of layer chips (colour, visibility) with a flyout of names. Hold L and move up/down to change the active layer
  (the hold-key scrub), or pick from a **layer ring** on the barrel button (layers plus new layer, hide others, lock, merge down). New shapes go to the
  active layer, so this is the only structural control needed while drawing. Other layers dim while one is active (a switch).
- **Document card:** opened from the "..." hub of the document ring. Name, description, author; page size, units, resolution, colour mode and
  background; the artboard list with thumbnails; counts (layers, groups, shapes, nodes, points, fonts, linked images, guides); export. Edits show in
  a small title chip at the top of the canvas. Metadata lives here so the canvas stays clear.
- **Swatches, Library and Fonts are tabs of the document card** (Document | Swatches | Library | Fonts), so resources live with the metadata and not in
  more panels:
  - **Swatches:** a grid of the colours in use (55 in the mockup), plus gradients. A swatch is a colour node that Fill nodes reference, so editing its hex
    recolours every shape using it, and "Select them" highlights those shapes on the canvas. Shows how many shapes in how many groups use it.
  - **Library:** Symbols, Patterns, Styles and Brushes. A symbol is a subnet and each placed copy is an instance node referencing it (Redefine
    updates all copies, Break link makes one ordinary again); a pattern is a repeating subnet with scale and angle as parameters; a graphic style is a saved chain
    of fill, stroke and effect nodes; brushes live here too (decided), not in a separate brush panel. Hovering a symbol highlights its instances on the
    canvas.
  - **Patterns tab:** saved patterns as live thumbnails (Sparkles, Flock, Dots, Waves in the mockup). Selecting one shows its chain (Tile > Repeat >
    Transform, a subnet), a live preview and its parameters: tile (any shapes, or a primitive), layout (grid or brick), size, spacing, angle, opacity.
    "Fill demo shape" applies it to a shape on the canvas; editing the pattern updates every shape using it, so patterns are linked, like symbols.
  - **Styles tab:** saved graphic styles as live thumbnails (Navy line, Soft shadow, Glow, Sticker, Dashed). Selecting one shows its chain (Fill >
    Stroke > Shadow) and the preview, with fill colour and opacity, stroke colour, width and dash, and shadow blur, drop and opacity. A shape using a
    style stays linked (edit the style and every shape follows; Break link makes the chain the shape's own).
  - **Brushes tab:** each brush is listed with a live stroke preview (Round, Calligraphic, Dry ink, Stars, Beads in the mockup). A brush is a subnet,
    a stamp repeated along a path with a pressure map (chain: Path > Stamp > Repeat along path > Pressure map), and a brushed stroke stays linked to
    it. Parameters: stamp type, size, spacing, opacity, angle and roundness (calligraphic), scatter and size jitter (dry, stars, dots), pen pressure to
    size (amount and a response curve, drawn as a small plot), tilt to angle (the stamp turns with the direction the pen leans; calligraphic and star
    stamps), taper (start length, end length, tip size), and colour. While the tab is open the artwork is a drawing pad, so a brush can be tried
    straight away with the pen (pressure and tilt from `WM_POINTER`, the mouse counts as half pressure and no tilt). The end taper needs the stroke
    length, so the stroke is drawn live and redrawn when the pen lifts.
  - **Skeletal strokes** are a brush type (Leaf, Arrow, Wave band in the mockup): artwork bent along the stroke's path (Hsu et al.'s "skeletal
    strokes", Illustrator's art brushes). The art can be fitted by stretching it over the whole path or repeating it along it, with parts such as an
    arrowhead kept undistorted; pressure and taper change its width. This widens the library a lot, because the art can be any shape or subnet,
    so a symbol can become a brush. It needs a **deform-along-path node** (maps geometry from (along, across) coordinates onto a path), which
    is also useful on its own, and guides marking which part of the art stretches and which stays rigid (the arrowhead). Worked out on a test bench,
    `docs/mockups/skeletal.html`:
    - **Choosing the art:** any shape in the document, or the current selection, from a list with thumbnails, plus a few built-in shapes (leaf, arrow,
      ribbon). A symbol or subnet becomes a brush. The art needs a direction (which axis runs along the path, left to right or top to bottom) and a
      flip; its length-to-width ratio sets how long it is at a given width. The brush stays linked to the source art, so editing the source updates every
      stroke using it.
    - **Guides:** two draggable guides split the art into a rigid head, a stretching middle and a rigid tail. The rigid parts keep their proportions at
      the stroke width (an arrowhead stays an arrowhead) and the middle takes the rest of the path; if the path is shorter than head plus tail they
      shrink together. The other fits are "stretch to length" (distorts the art) and "repeat" (whole copies along the path). A "show how the art is
      stretched" grid on the result makes the stretch visible.
    - **Corners:** four policies, compared side by side on the same path. Bend (raw) tears on the outside of a corner and overlaps on the inside, so it
      is not a sensible default. Smooth rounds the corner first (radius setting) and is the default for flowing strokes. Miter join keeps the outline
      continuous with a sharp corner and a limit, for geometric art such as ribbons and arrows. Break at corners fits each piece separately above a turn
      angle, for art with identity such as a bird. The policy is an editable brush setting; **Smooth is the default** (decided).
    - All of this is the parameter set of the deform-along-path node: path, art, fit mode, guides, corner policy and its radius, miter limit and break angle.
  - **Fonts:** the fonts used, a preview, how many text objects use each, and a status (embedded, or missing with Replace). Embed fonts on save, convert
    text to outlines (adding an outline node, undoable), and a list of installed fonts.
- **Artboards on the canvas:** each artboard shows its name and size above it and is selected by clicking it; the document card lists the same set.
- **The strip is the outline at its smallest density.** Both show the same layer tree and share selection and the active layer; the strip stops at
  layers and is driven by the hold-key scrub and the ring, and it expands into the full outline (L, or tapping its top), which in turn opens into the
  node graph (layers and groups are subnets). One tree model with a density setting serves all three.
- Open: whether a shape row shows its node chain inline; whether layers and groups get different icons (a place to draw vs a unit that moves).

### No Classic mode
An earlier idea kept a **Classic** workspace (menu bar, toolbar, palette, sidebar) behind a switch. It is dropped: two front ends double the work. The workspace is the
program; everything it does is a command id in `SketchCommandController`, so a menu bar or toolbar can still be added as one more view of the same commands. The old
`curvy.newui` layout stays only for the headless `--check` and `--check-tools` runs until their checks move over.

### Theme
Dark by default, light available (`WorkspaceController::setDarkTheme`). The colour tokens are in `include/curvy/theme.h` (from `docs/dark mode.docx`: no pure black or white,
text 4.5:1, icons and accents 3:1; a gtest checks the figures). They reach newui the way a host such as cpp_codetools' `ThemeChanged` does: `UIColorManager::setDarkModeOverride`
plus role overrides for what the files leave out, and `Resources/Themes/dark.theme` / `light.theme`, which `ThemeData` loads for the roles and control parts. Those two files
are generated by `python tools/makeThemes.py` from newui's Windows-derived ones (part sizes kept, colours remapped to the tokens). The workspace's own painting (hint bar,
rings, cards, pasteboard) reads `Theme::current()`. Not yet: a ring slot or setting to switch themes, and a canvas-background choice independent of the UI theme.

### Settings / Preferences dialog (needed soon)
Nothing has a home yet and the list is growing, so a settings dialog is the next piece of chrome to design (as an HTML mockup first, then converted). Settings the design already promises or the code
already hard-codes, to start from: theme (dark / light, `WorkspaceController::setDarkTheme`), canvas background independent of the UI theme, dock defaults (toolbar and property boxes on or off at
start), property text size (10-20, default 11), scrub axis (vertical default, horizontal option; `WorkspaceModel::ScrubAxis`), scrub sensitivity, Space tap threshold (300 ms in `WorkspaceController`), whether hints fade after three uses, pen options (eraser end, barrel button meaning), tool-option defaults (polygon sides and star points; kept per user, or in the document as `data-tools` when the Drawing page says so), snapping and guides defaults, recent files length. Reached from the Document ring's hub ("Setup...") and a View ring slot; stored per user, with the document-level ones kept in the file.

### Docks (implemented)
`DockController` builds the optional docks from the mockups `docs/mockups/toolDock.html` and `propertyDock.html` (converted to `Resources/toolDock.newui` and `propertyDock.newui`
with the html-mockup-to-newui skill; the top bar's two toggle buttons are in `workspace.html`). The toolbar is the old palette's order and icons in two columns; clicking a button picks the
tool and the buttons follow the tool when it changes elsewhere. The property dock hosts the existing Transform panel (one box so far; the other boxes follow the same way). Both docks
are off by default, flush to the edges under the rulers, full height, translucent (bgSurface at 60%, retaken from `Theme::current()` when the theme changes), and are switched by the
**Tools** and **Panels** buttons in the top bar or by the **View** ring (Toolbar T, Properties P, Rulers G, Fit page 0, Theme L), which replaces the Document ring's Fit and Rulers
slots. Blur behind the panels is not done (see above). Headless checks: `curvy --check-workspace`; real-input tests: `tests/gui/test_curvy_gui.py` (see the newui-gui-driver skill).

### Implemented so far (WorkspaceController)
`Resources/workspace.newui` is the window; `WorkspaceController` (MVC: `WorkspaceModel` holds the shell state and the hint bar text, the canvas, ring and labels follow it,
`SketchCommandController` does the work) gives: tap Space / barrel button / right press opens the New, Create, Object, Arrange or Document ring for what is under the pointer;
hold R E P S L and drag to draw; hold Space and drag to pan; hold W, O, H or B over an outline or fill and move vertically to scrub (`HoldScrub`, one undo step on letting go,
Esc cancels). The canvas offers `onPressFilter`, `onKeyFilter` and `onPaintTop` hooks so the workspace sees presses and keys first. Not yet: tool option cards, the object
ring's Fill / Stroke slots, touch, rotate/scale/skew scrub keys. Checked by `curvy --check-workspace` and the HoldScrub / WorkspaceModel gtests.

## Consequences for the code

- **newui has no pen support.** No `WM_POINTER`, pressure, tilt or barrel-button handling. Windows pen drivers map the barrel button to right click by
  default, so the context menu works, but pressure (width profiles, brushes) and eraser-end detection need a pointer-message path in newui.
  Separate task, and it affects the pen tool most.
- The radial menu, hint bar and HUD are new views painted in `Overlay`/`SubView`; only the HUD and hint bar are layout (convertible from the
  mockup). The radial menu is custom drawing.
- The radial menu slots can be built from the command ids in `kMenuBindings` in `sketchwindow.cpp`.
- Decided: the graph is the document, so **every path is a node** and the **object list (and the outline, layer strip, canvas hit testing) is a view
  over the graph**. `docs/design.md` Phase 2 still describes plain paths as `SketchObject`s beside graph-backed live shapes; that is the old plan.
  Path nodes need a `bezierPath` node type or an extension of the geometry model (jitnodes geometry is polyline-only today); that will be added.
- jitnodes' `GraphController` and `ParameterPanel` talk to its own `EditorModel` and `History`; put a small interface in front of them so they
  can run on `GraphDocument` and its shared undo stack.
- Context under the pen needs a `hitTest` that also reports anchors and handles (today `hitTest` returns the topmost object only).

## Decisions so far
- Start with an empty workspace and no document; Space (or the pen button) there opens the New ring.
- Toolbar (left) and property boxes (right) are optional translucent overlays, off by default; gradient geometry lives in shape-local coordinates.
- No command palette, no toolbar by default; rulers kept; a reserved top row; hubs with "..." open the full dialog for the context.

## Open questions
1. Does the user have a pen display or a separate tablet? (A pen display makes the HUD-near-selection natural; a separate tablet makes it
   better to dock the HUD to a fixed edge.)
2. Which barrel-button/eraser mappings does the target hardware expose (one or two buttons)?
3. Touch: should two-finger tap = undo and pinch = zoom be supported, or is pen+keyboard enough?

## First run

On the first start (`Settings::welcomeShown` false, no file given on the command line, no autosaved copy to recover) a card is centred over the canvas: a
stylised keyboard with the Space bar lit, and three lines on how Space opens the ring menu (tap), pans (hold and move) and starts a document. Any key, or a press anywhere (the canvas, the top bar, the hint bar), closes it (Esc just closes; Space closes it and carries on, so the first tap opens the ring). The flag is saved when the card is shown, so it appears once.
It is a newui `Overlay` (`WelcomeOverlay`) built from `newui::shapes`: the keyboard is rounded-rect shapes with blue outlines (roughly opposite the amber accent) and the lit Space bar has a small glow plus an amber focus ring.
