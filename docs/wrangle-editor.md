# The wrangle code editor

The Node panel shows a **Code** section for a `wrangle` node, so the node's program is written and checked where the node's other
parameters are. The language itself is in `3rdparty/jitnodes/docs/wrangle.md`; this page is about the editor.

## Using it

- Select a wrangle node. The Node panel gets a `Code` row (`Code`, and `Run over: Points | Primitives`). `Code` opens the editor and widens
  the right dock to 380 px while it is open; closing it puts the dock back at the width it had.
- The editor is the same view the Python Script Editor uses (`CodeEditorView`): line numbers, colours, folds, undo, Ctrl+F find and
  Ctrl+H replace, Tab and Shift+Tab. The language is `WrangleLanguage`: `#` comments, the bars of the declaration list, the 13 functions
  (a call to an unknown one gets a squiggle), `a b c d`, `sampleIndex` and declared names, numbers and operators.
- A moment after typing stops, the first thing the parser rejects is marked in the text and named under the editor (`Line 3, col 9: ...`).
- **Commit** (or Ctrl+Enter) stores the text in the node's `code` as one undo step. **Revert** puts the node's text back. Text you have
  typed but not committed stays when the panel refreshes (another edit, a time change) and is replaced only when another node is shown.
- The cook's error (the node cannot compile or run) shows in the line above the buttons; it clears when the code cooks again.
- `Points` / `Primitives` is the node's `class` parameter (one undo step).

## Declared parameters

A list at the top of the code declares parameters the node gets as rows in the panel:

```
| gain = 1.5(0, 10), steps(1,), ratio(,1), plain |
```

Default (optional, 0), then optional `(min, max)` with either side empty (the comma is always written). The list must come before the
first assignment. The names become the node's spare floats, so they are saved, undone, copied and pasted with it, and can carry
expressions and keys. Editing the code keeps the value, expression and keys of a parameter that stays and drops the ones that go.
The limits clamp values typed or dragged in the panel (the cook does not enforce them); a row with both limits also has a slider.
Declared parameters are listed first, ahead of `a`, `b`, `c` and `d`.

## Examples

`Resources/samples/wrangle/*.wrangle` (`wave`, `pulse`, `remap`, `threshold`): open one in a text editor and paste it into the Code
section, or set it from Python. A test cooks each (`WrangleCodeTest.everySampleWrangleParsesDeclaresParametersAndCooks`).

```python
node = doc.add_node("wrangle", "bend")
doc.set_string(node, "code", open(r"...\wave.wrangle").read())
```

`curvy.check_wrangle(code)` returns `None` or `(line, column, message)` and `curvy.wrangle_functions()` lists the functions
(`docs/py-curvy-module.md`).

## How it is built

| Piece | File |
|---|---|
| The editor view (text, line numbers, find, problem line) | `include/curvy/codeeditorview.h`, `src/codeeditorview.cpp` |
| Language interface, Python and wrangle languages | `codelanguage.h`, `pythonhighlight.h`, `wranglehighlight.h` |
| Parse check with line and column | `wranglecheck.h` (`checkWrangle`, `wrangleFunctions`) |
| The section's layout (from the mockups in `docs/mockups/nodePanelCode*.html`) | `Resources/nodePanelCode.newui` |
| The panel code | `NodePanel::buildCodeSection`, `commitCode`, `updateCode`, `sliderChanged` in `src/nodepanel.cpp` |
| Widening the dock | `NodePanel::onCodeToggled`, wired in `WorkspaceController` |

`Resources/nodePanelCode.newui` is generated: change `docs/mockups/nodePanelCodeSection.html` and convert it again with the
`html-mockup-to-newui` skill rather than editing it by hand.

## Tests

- `tests/test_nodepanel.cpp` (`WrangleCodeTest`): the row only for wrangles, open and close, Commit / Revert and the undo step, unsaved text
  surviving a refresh, declared parameters (rows, limits, sliders, order, undo and redo, copy and paste), the cook error, the class button,
  every sample.
- `tests/test_wranglehighlight.cpp`, `tests/test_wranglecheck.cpp`: the language support.
- `tests/gui/test_curvy_wrangle.py`: the section with the real mouse and keyboard (run by hand; passes 4/4).

## Not done

- A way to add a node from the editor (a wrangle can only be made from Python).
- The Settings dialog still labels the colour styles with Python wording.
