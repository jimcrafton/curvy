---
name: html-mockup-to-newui
description: Convert an HTML/CSS UI mockup (wireframe, design-tool export, hand-written prototype page) into a newui ".newui" View/Frame document, run it through a validator, and fix up what could not be converted. Use whenever asked to turn an HTML mockup, screenshot-derived HTML, or UI sketch into the newui framework's UI format, to start the jitnodes editor UI from a mockup, or to check an existing .newui file against newui's registered classes.
---

# HTML mockup to `.newui`

jitnodes builds its editor UI on [newui](https://github.com/jimcrafton/newui) (checkout at `D:\code\newui`, override
with `NEWUI_DIR`). Screens are designed as HTML mockups, converted once to `.newui` files, then loaded by the editor
with `newui::Bundle::loadFrame()` (or `loadView()` for a panel) and wired up by `View::findView("name")`.

The conversion is a **one-time, approximate translation**: newui has no CSS engine, only `FlexLayout`, absolute
`bounds` and a fixed set of widgets. The goal is a loadable tree with the right structure, text, colours and rough
layout, not pixel fidelity. Read `../newui-view-json5/SKILL.md` for the file format and
`../newui-view-json5/references/html-mockup-conversion.md` for the full element/CSS mapping tables.

## Workflow

1. **Get HTML that the converter can read.** One self-contained page: inline `style=` and/or a `<style>` block with
   simple selectors (`tag`, `.class`, `#id`, `[attr]`, descendant `a b`, child `a > b`, `:root` custom properties
   and `var()`). Give interactive elements an `id` - it becomes the view `name` the editor code looks up. Set the
   page size on `body` (`width`/`height`) or pass `--width/--height`. Use `display:flex` for rows and columns.
2. **Convert:**
   ```
   python .claude/skills/html-mockup-to-newui/scripts/html2newui.py mockup.html -o editor/ui/panel.newui [--name panel] [--fragment]
   ```
   `--name` is the Frame name and the file name `Bundle::loadFrame` looks for (`Resources/<name>.newui`).
   `--fragment` writes a bare `SubView` document (for a panel that is loaded into an existing tree) instead of a
   `Frame`. `--base-font-size` (default 12) sets the size of unstyled text; newui controls default to 12, not 16.
3. **Read the warnings** on stderr. Each one is something the format cannot represent (see the table below). Decide
   per warning whether the mockup should change or code has to cover it.
4. **Validate:**
   ```
   python .claude/skills/newui-view-json5/scripts/validateNewui.py editor/ui/panel.newui
   ```
   It checks every `"type"`, property name, enum value and colour against newui's generated reflection data, and
   catches the classic bug of a `SubView` without `visible: true`. Fix errors; warnings about unknown properties are
   typos or stale names (the reader silently skips them).
5. **Look at it for real** when newui is built: `Bundle::loadFrame` in a small exe (see newui's
   `examples/loaddialog1.cpp`) and compare against the mockup. Adjust the HTML and reconvert rather than hand-editing
   the `.newui`, unless the fix is something only the file can say (`desiredSize`, `layoutParams`).
6. **Treat the `.newui` as generated** once hand-edited: keep the HTML next to it (`editor/mockups/`) and note in the
   commit/PR when the `.newui` diverges.

Do not commit or push; the user makes all commits.

## What converts and what does not

| HTML / CSS | Result |
|---|---|
| `body`, outermost container | `Frame` + `RootView` (or a bare `SubView` with `--fragment`) |
| `div`, `section`, `header`, `nav`, `form`, ... | `SubView` (`visible: true` always written) |
| `display:flex` (row/column, `justify-content`, `align-items`, `gap`, `flex-wrap`, `padding`) | `FlexLayout`; `flex-grow` becomes the child's `FlexLayoutParams.weight` |
| plain block/inline flow, `position:absolute` | no layout; explicit `bounds` computed by a block-flow estimate |
| `display:grid` with `grid-template-columns` | nested flex rows (tracks cannot be saved); `fr` becomes weights |
| `p`, `span`, `h1`-`h6`, `a`, `label`, `li` with only text | `Label` (`wordWrap`, `textAlignment` always written because newui centres by default, font, colour; `a` is a `hotLink`) |
| `button`, `input type=button/submit` | `Button` (`text`, `enabled`; custom colours/radius cannot be reproduced) |
| `input type=checkbox/radio` | `Toggle` (`checked`, `radioStyle`); `<label>` wrapping it becomes a Toggle + Label row |
| `input type=text/...`, `textarea` | `TextField` (`text`, `placeholder`, `font`, `textColor`) / `TextControl` |
| `input type=range` | `Slider` (`value`; the range stays 0..100 and is rescaled, with a warning) |
| `progress`, `select`, `fieldset`+`legend` | `Progress`, `DropDownList` (options dropped, with a warning), `GroupBox` |
| `table` | vertical `SubView` of row `SubView`s with equal weights |
| `img` | `Image` (`imagePath` needs a real path afterwards) |
| `canvas`, `svg`, `video`, `iframe` | placeholder `SubView` to draw in code (use it for the node-graph canvas) |
| `overflow:auto/scroll` | `ScrollView` |
| `background` colour, `linear-gradient`, `border`, `border-radius`, `opacity` | `ViewStyle` fill / `borderWidth`+`borderFill` / `rectRadius` (one radius) / `opacity` |
| `color`, `font-*`, `text-decoration`, `text-align` | `Font` object, `textColor`, `textAlignment` |
| `text-align`, `vertical-align` (`left`/`center`/`right`, `top`/`middle`/`bottom`) | `textAlignment` / `verticalTextAlignment` on Label, Button, ToolbarButton and TextField; `glyphAlignment` / `verticalGlyphAlignment` on a checkbox or radio Toggle. Written only when set explicitly and different from the control's default (Label always carries `textAlignment`); use `data-newui-type` for a ToolbarButton |

Dropped with a warning: `box-shadow`, `transform`, background images, media queries and other at-rules, margins
(use `gap`/`padding`), asymmetric padding (averaged), per-corner radii (first value), radial/conic gradients,
`calc()`, unknown colour names, web fonts (replaced by `Segoe UI` unless the family is a common Windows font),
`<select>` options, pseudo-class styles (`:hover` etc. are skipped silently).

Colours are written as strings (`"#2b2d31"`, `"cornflowerblue"`); `rgb()`/`hsl()` are converted to hex. newui's
reader accepts those and the older `{type:"Color", r, g, b, a}` form.

## Designing a mockup so it converts well

- Build rows and columns with `display:flex` and `gap`; avoid margins and floats.
- Fixed sizes only where they matter (`width` on a label column, `height` on a toolbar); everything else should size
  itself. Fixed sizes become `desiredSize` with `desiredSizeOverride: true`.
- Use `flex-grow:1` for the element that should fill the remaining space (a canvas, a text area).
- One widget per element; mixed inline formatting inside a text element collapses into one `Label` with the style of
  the outer element.
- Give every element the app needs to read or change an `id`. Decorative elements do not need one.
- Pick colours and fonts that exist: hex or CSS names, and fonts such as Segoe UI, Consolas, Arial.

## Files

- `scripts/html2newui.py` - the converter (stdlib only; reads `NEWUI_DIR`'s `color_constants.h` for colour names).
- `examples/parameterPanel.html` - a node parameter panel mockup that exercises most of the mapping; convert it with
  `--name nodeParameters` to see the output. It is a layout sketch, not a committed editor design.
- `../newui-view-json5/scripts/validateNewui.py`, `newuiSchema.py` - validator and schema lookup.

## Extending the converter

- Add elements as `do_<tag>` methods on `Converter` (return a `V` or a list of them) and map the new class in
  `emit`. Look up the real property names first: `newuiSchema.py --class TabControl`.
- Layout estimation lives in `intrinsic`/`place`; it only has to be close, because `FlexLayout` recomputes on load.
- Run the converter on `examples/parameterPanel.html` and the validator on the result after every change; the
  validator must stay at 0 errors for converter output.
