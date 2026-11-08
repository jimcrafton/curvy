# Generating a `.newui` document from an HTML mockup

> The `html-mockup-to-newui` skill automates most of this (`scripts/html2newui.py`) and
> `newui-view-json5/scripts/validateNewui.py` checks the result. This file is the reference for the mapping those
> tools implement and for hand fix-ups. Corrected 2026-10-16 against the live headers: colours may be plain
> strings (see below), more widgets are listed, and the layout-estimation notes are new.

This is a sub-topic of the `newui-view-json5` skill (`../SKILL.md`) - read that first for the
core format (document shape, `"type"` tagging, scalar/nested-object encoding, the omission rule,
collections) before using this. Everything here just adds an HTML/CSS-specific mapping on top.

Translating an HTML/CSS mockup (a rough wireframe, a design tool's HTML export, a static prototype
page) into a `.newui` View tree is a **one-time, approximate conversion**, not a live rendering of
HTML - there's no CSS box-model/flow engine here, only `FlexLayout`'s single-axis flex algorithm
(and `GridLayout`, with the persistence caveat below). Pixel-perfect fidelity isn't the goal;
producing a real, loadable View tree with the right structure, text, colors, and approximate
layout is.

**Process:**

1. Walk the HTML DOM tree (or its component/element tree, if the mockup is generated rather than
   hand-written HTML) top-down. The outermost container becomes the document root (`Frame` with a
   `rootView`, or a bare `RootView`/`SubView` if you only need a fragment - see the core skill's
   "Document shape").
2. For each element, pick a View class from the mapping table below, using its own **verified
   current** reflected property names (per the core skill's own repeated caution: check the real
   generated `.cpp`, don't assume from an older file or from this table going stale).
3. For a `display:flex` container, give it a `layout: {type:"FlexLayout", ...}` (see the CSS→
   FlexLayout mapping below) instead of hand-computing each child's `bounds` - this is what makes
   the tree relayout correctly if it's ever resized, not just hold whatever pixels you picked once.
   For anything CSS positions with plain block/inline flow (no `display:flex`/`grid`) or `position:
   absolute`/`fixed`, omit `layout` entirely and set each child's own `bounds` explicitly instead -
   there's no block/inline flow equivalent to compute those from automatically.
4. Convert colors/fonts/text per the notes below.
5. Follow the core skill's omission rule: only write a property whose value actually differs from
   a fresh instance's own default - **except `visible`, which you must always write explicitly as
   `true` on every plain `SubView`/`RootView` container node.** `View::visible_` defaults to
   `false` at the base class - confirmed live (`HANDOFF.md` Part 96/97): a bare `SubView` used
   purely as a layout container (no `Control`-derived override) stays invisible, and so does its
   entire subtree, unless you say otherwise. `Label`/`Button`/`TextField`/`Toggle`/every other
   `Control`-derived widget already calls `setVisible(true)` in its own constructor, so omitting
   `visible` is genuinely safe *there* - it's specifically the plain structural containers (every
   `SubView` a mockup conversion introduces purely to hold a `FlexLayout` row/column) that need it
   spelled out. `enabled`/`opacity`/etc. on an actual `Control` are still fine to omit as before.
6. Give every node a `name` matching a stable identifier from the mockup (an `id`, a semantic
   role) if the consuming app will need to `View::findView()` it later (buttons, inputs) - purely
   decorative nodes (a wrapping `<div>` with no behavior) don't need one.

**Element → View class mapping** (verify each against the real generated `.cpp` before trusting it
for a specific property list - this table names classes/shapes, not exhaustive property lists):

| HTML | newui class | Notes |
|---|---|---|
| `<body>`/outermost container | `Frame` (`rootView` → `RootView`) or bare `RootView` | See the core skill's "Document shape" for the two document shapes |
| `<div>`, `<section>`, `<header>`, `<footer>`, `<nav>`, `<form>`, generic wrapper | `SubView` | Plain layout container - `style: {type:"ViewStyle", ...}` |
| `<p>`, `<span>`, `<label>`, `<h1>`-`<h6>`, any element whose only content is text | `Label` | `text`, `style: {type:"LabelStyle", text, wordWrap, textColor, font}` - set `wordWrap: true` unless the CSS says `white-space: nowrap` |
| `<button>`, `<input type="button/submit">` | `Button` | `text`, `enabled`; style is `ThemedButtonStyle` (native OS chrome) - see the styling caveat below |
| `<input type="checkbox">` | `Toggle` | `checked`; `radioStyle: false` |
| `<input type="radio">` | `Toggle` | `checked`; `radioStyle: true` (this codebase has no independent radio-*group* concept - see `Toggle`'s own class comment) |
| `<input type="text">`, `<textarea>` | `TextField` | `text`, `font`, `textColor` |
| `<img>` | `Image` | `imagePath` (the `src` - must already be a real, resolvable file path; a mockup's own `src="..."` almost never is, so this usually needs manual follow-up), `imageFillMode` |
| A scrollable container (`overflow: auto`/`scroll`) | `ScrollView` | `contentSize`; `vBar`/`hBar` are read-only (owned children, not settable) |
| A slider (`<input type="range">`) | `Slider` | `value`, `integer`, `step`, `horizontal`, `showTicks`. The range (`min`/`max`) cannot be saved and stays 0..100, so rescale `value` into 0..100 |
| `<progress>` | `Progress` | `value` 0.0-1.0 (`value / max`), `horizontal` |
| `<fieldset><legend>` | `GroupBox` | `text` is the legend; children go in `childViews` with explicit `bounds` (leave about 20px for the caption) |
| `<select>` | `DropDownList` | `selectedIndex`; the options live in a `model` that a file cannot describe, so populate it in code |
| `<textarea>` | `TextControl` | `text`, `font`, `textColor`, `wordWrap` (multi-line; `TextField` is single-line) |
| `<ul>/<ol>/<li>` | vertical `SubView` of `Label`s | prefix the text with a bullet; there is no list-marker concept |
| `<table>` | vertical `SubView` of horizontal row `SubView`s | give each cell `layoutParams: {type:"FlexLayoutParams", weight: 1}` for equal columns |
| `<hr>` | `SubView` 1px high | `backgroundFill` colour |
| `<canvas>`, `<svg>`, `<video>`, `<iframe>` | placeholder `SubView` | draw it in code; name it so the app can `findView` it |
| tabs | `TabControl` (+ `TabPage` children with `title`) | `selectedIndex`, `alignment` (`Top`/`Bottom`/`Left`/`Right`) |
| a resizable split | `Splitter` | `orientation`, `splitPosition`, `dividerThickness`, `minPaneSize`, `fixedPane` |

**CSS → `FlexLayout` mapping** (`FlexLayout`'s real registered properties - see the core skill's
"Nested reflected objects" section for the exact shape):

| CSS | `FlexLayout` property |
|---|---|
| `flex-direction: row` / `column` | `orientation: "Horizontal"` / `"Vertical"` |
| `justify-content: flex-start` | `mainAxisAlignment: "Start"` |
| `justify-content: center` | `mainAxisAlignment: "Center"` |
| `justify-content: flex-end` | `mainAxisAlignment: "End"` |
| `justify-content: space-between` | `mainAxisAlignment: "SpaceBetween"` |
| `justify-content: space-around` | `mainAxisAlignment: "SpaceAround"` |
| `justify-content: space-evenly` | `mainAxisAlignment: "SpaceEvenly"` |
| `align-items: flex-start`/`center`/`flex-end`/`stretch` | `crossAxisAlignment: "Start"`/`"Center"`/`"End"`/`"Stretch"` |
| `gap` | `spacing` |
| `flex-wrap: wrap` | `wrap: true` (+ `lineSpacing` for row/column gap between wrapped lines) |
| `flex-grow: n` / `flex: n` (on a child) | the child's `layoutParams: {type:"FlexLayoutParams", weight: n}` |
| `align-self` (on a child) | the child's `layoutParams.crossAxisAlignment` |
| a flex child's `width`/`height` | the child's `desiredSize: {type:"Size", ...}` plus `desiredSizeOverride: true`; without the override the control measures itself |
| `padding` | `padding` - **one uniform float**, not 4-sided; asymmetric CSS padding (different per side) has no direct equivalent - approximate with one value, or drop `layout`'s padding and pad via each child's own `bounds` instead |

A CSS Grid layout (`display: grid`) has a real `GridLayout`/`GridLayoutParams` pair in this
codebase, but **its row/column track definitions can't currently round-trip through a `.newui`
file** - `GridLayout::rows()`/`columns()` are get-only reflected properties (the real tracks are
only ever built via imperative `addRow()`/`addColumn()`/`addFixedRow()`/... method calls, never a
reflected collection), so a saved document has no way to reconstruct them on load. For a grid-like
mockup, approximate with **nested `FlexLayout`s** instead (a `Vertical` `FlexLayout` of rows, each
row itself a `Horizontal`-`FlexLayout` `SubView`) - fully round-trippable, and visually close
enough for a mockup where the grid is regular (uniform row/column sizes). `GridLayoutParams`
(`row`/`column`/`rowSpan`/`columnSpan`/alignment, on a *child*) are plain fields and do persist
fine - it's specifically the parent `GridLayout`'s own track structure that doesn't.

**Colors, fonts, text:**

- A `Color` is a string-valued class (`// @reflect stringvalue`, `color.h`): write it as `"#3498db"` (`#RGB`,
  `#RGBA`, `#RRGGBB` or `#RRGGBBAA`; the alpha byte is last), a CSS colour name (`"cornflowerblue"`, the 147 CSS
  Color 4 names plus `"transparent"`), or a `UIColorRole` name (`"WindowText"`). The reader still accepts the
  older nested form `{type:"Color", r, g, b, a}` with 0.0-1.0 floats (newui's own `examples/*.newui` use it), but
  new files should use strings. Convert `rgb()`/`hsl()` to hex first. A mockup's colours are usually fixed brand or
  design colours, so hex is right; use a role name only when the design means "the ordinary themed colour".
- `background-color`/`color` on a container maps to that View's `style.backgroundFill`
  (`{type:"Fill", kind:"Color", color:{...}}`) - usually best left unset except on the true root
  (`RootView` already initializes its own `backgroundFill` to a sensible, theme-tracking default -
  `UIColorRole::WindowBackground`, `HANDOFF.md` Part 96); a mid-tree container should almost always
  stay transparent (`PaintKind::None`, the default) so it doesn't opaquely blot out whatever its
  parent already painted, matching normal CSS stacking-context expectations only loosely, not
  exactly.
- Text color (a `<p>`'s `color`) maps to `LabelStyle.textColor`/`TextField.textColor` - same
  float-RGBA conversion.
- `font-family`/`font-size`/`font-weight`/`font-style`/`text-decoration` map to a `Font` object:
  `name` (nearest installed system font name - a CSS web font has no direct equivalent, pick the
  closest system one, e.g. "Segoe UI"), `size` (CSS px ≈ point size 1:1 is a reasonable mockup
  approximation, not a precise DPI-correct conversion), `bold` (`font-weight >= 600` or literal
  `bold`), `italic` (`font-style: italic`), `underlined` (`text-decoration: underline`),
  `strikeThrough` (`text-decoration: line-through`).
- `border`/`border-width`/`border-color`/`border-radius` map to `ViewStyle.borderWidth`/
  `borderFill`/`rectRadius` - **one uniform radius**, not CSS's independent per-corner radii;
  approximate with one value if the mockup uses different corners.
- `box-shadow` has **no `ViewStyle` equivalent at all** - `DropShadow`/`Glow` only exist on
  `gfx::ShapeStyle` (used by vector `Shape`s - `RoundRect`/`Text`/etc., a completely different
  styling system from the `View`/`ViewStyle` tree this whole skill is about) - there's nothing to
  map a plain container's `box-shadow` onto; drop it.
- A `Button`'s/`Toggle`'s chrome (`ThemedButtonStyle`/`ThemedCheckBoxStyle`) is drawn by the real
  OS theme (UxTheme), not Blend2D - custom CSS colors/border-radius/shadows on a mockup's `<button>`
  can't be reproduced at all; only its **text** and **enabled/disabled** state carry over
  meaningfully.
- CSS has no equivalent to this format's own document-level concepts either, and shouldn't be
  invented: no responsive breakpoints/media queries (a `.newui` document is one fixed pixel size),
  no CSS variables/custom properties (resolve them to their final computed value before writing).

**Gradients:** `linear-gradient(...)` maps to `backgroundFill: {type:"Fill", kind:"Gradient", gradient: {type:"Gradient",
kind:"Linear", linearStart:{type:"Point",x,y}, linearEnd:{type:"Point",x,y}, stops:[{type:"GradientStop", offset, color}]}}`
with the points in the view's own coordinates (compute them from the CSS angle and the final size). Radial and conic
gradients exist (`kind: "Radial"`, `"Conic"`) but `html2newui.py` only converts linear ones.

**Layout estimation:** `.newui` needs `bounds` on every node even under a `FlexLayout`, because the file is the
persisted result of a layout pass. Estimate them (text width ~ 0.56 x font size per character, line height 1.45,
button minimum 75 x 26) and let `FlexLayout` correct them on the first layout; set `desiredSize` + `desiredSizeOverride`
only where the CSS fixes a size.

**Worked example** - a small `<div style="display:flex; flex-direction:column; gap:8px;
padding:12px; background:#f2f2f2"><p style="color:#333">Enable notifications</p><input
type="checkbox" checked></div>`:

```json5
{
  meta: { author: "", date: "2026-09-14", copyright: "", version: "0.1.0.900" },
  type: "SubView",
  bounds: { type: "Rect", pos: { type: "Point", x: 0, y: 0 }, size: { type: "Size", width: 220, height: 60 } },
  visible: true,
  name: "notifyRow",
  layout: {
    type: "FlexLayout",
    orientation: "Vertical",
    spacing: 8,
    padding: 12
  },
  style: {
    type: "ViewStyle",
    backgroundFill: {
      type: "Fill",
      kind: "Color",
      color: "#f2f2f2"
    }
  },
  childViews: [
    {
      type: "Label",
      text: "Enable notifications",
      style: {
        type: "LabelStyle",
        text: "Enable notifications",
        textColor: "#333333"
      },
      bounds: { type: "Rect", pos: { type: "Point", x: 0, y: 0 }, size: { type: "Size", width: 196, height: 20 } },
      childViews: []
    },
    {
      type: "Toggle",
      checked: true,
      bounds: { type: "Rect", pos: { type: "Point", x: 0, y: 28 }, size: { type: "Size", width: 20, height: 20 } },
      childViews: []
    }
  ]
}
```

Note how much is omitted per the omission rule: `visible: true` is the one thing written despite
being "just a default" for a `Control` - it's *not* the default for a plain `SubView` (see step 5
above), so it has to be explicit here. `enabled` on the `Toggle` is genuinely omittable (`Control`
already defaults it true). No `mainAxisAlignment`/`crossAxisAlignment`/`wrap`/`lineSpacing`/`alignContent` on the
`FlexLayout` (all fine at their own defaults for this simple case), no `borderWidth`/`opacity`/
`compositingOp`/`font` on either `style` block, no `desiredSize`/`clientBounds`/`highlighted`/
`cursor`/`origin` anywhere - every real writer-produced file includes these (write always emits
every registered property, per the core skill's "Nested reflected objects" section), but a
hand-generated one only needs to state what actually matters for this mockup.

**Where to look for the real widget/layout shapes:**

- `include/newui/layout.h` - `FlexLayout`/`GridLayout`/`AnchorLayout`/`CardLayout` and their
  `*LayoutParams`.
- `include/newui/controls.h` - `Button`/`Label`/`Toggle`/`TextField`/`Image`/`ScrollView`/`Slider`
  and the rest of the concrete widgets the mapping table above names.
