# Plan: wrangle code editor in the Node panel

Status: planned 2026-11-16. Decisions by the user: `#` starts a comment (jitnodes change, edited in `3rdparty/jitnodes`); the editor is a
section of `NodePanel` shown for `wrangle` nodes, opened by a toggle "Code" button that grows the panel; a "Commit" button pushes the
editor text to the node; the shared editor is extracted from `ScriptEditorPanel`; parameters declared in the code (see
`3rdparty/jitnodes/docs/wrangleParamsPlan.md`) show as rows in the panel. Syntax (changed 2026-11-16 at the user's request, replacing
`param`): a list at the top, `| gain = 1.5(0, 10), steps(1,), plain |`: optional default (0), optional `(min, max)` with either side
empty. Limits clamp typed and dragged values in the panel (the existing `min:` / `max:` tag behaviour) and a slider needs both.

## Parts and order

1. **jitnodes language** (`3rdparty/jitnodes`, carry to `D:\code\jitnodes`): `#` comments to end of line in `exprParser.cpp`; `param`
   declarations (wrangleParamsPlan phase 1 and 2: parser out-parameter, spare floats synced from `code`, `Node::removeSpareFloat`,
   `floatRange`). Library tests; `docs/wrangle.md` updated (comments, declared parameters, grammar).
2. **`CodeEditorView`** (`include/curvy/codeeditorview.h`) - DONE 2026-11-16: the text control, line numbers, find/replace, problem line,
   highlight and syntax timers, undo, extracted from `ScriptEditorPanel`, parametrised by a `CodeLanguage` interface
   (`codelanguage.h`: style sheet, style ranges, new line indent, folds; base class is plain text; `PythonLanguage` implements it).
   The checker stays a callback on the view (`onCheckSyntax`). `ScriptEditorPanel` keeps files, scratch, Run and wraps the view;
   curvyTests stayed at 550.
3. **Wrangle language support** - DONE 2026-11-16: `wranglehighlight.h/.cpp` (`wrangleStyleRanges`, a hand-written tokenizer that uses the
   Python style names so the user's colours apply; `WrangleLanguage : CodeLanguage`) colours `#` comments, the bars of the declaration list (keyword), the 13 functions when called (an unknown one gets a squiggle), `a b c d`, `sampleIndex` and declared names
   (constants), dotted names, numbers, strings, operators; the squiggle marks characters and tokens the parser rejects. `wranglecheck.h/.cpp`:
   `checkWrangle(code)` runs `parseWrangle` and maps the byte offset to a 1-based line and character column; `wrangleFunctions()` lists
   the functions with their arity (for autocomplete later). Tests: test_wranglehighlight.cpp, test_wranglecheck.cpp (curvyTests 572).
4. **NodePanel**: a "Code" toggle button and the editor section (`Resources/nodePanel.newui` through the `html-mockup-to-newui`
   skill), a Commit button, `commitString`, the class selector, cook error in the problem line, rows for spare floats with an optional
   slider from `floatRange`.
   DONE 2026-11-16: mockups `docs/mockups/nodePanelCode.html` and `nodePanelCodeSection.html`, `Resources/nodePanelCode.newui` loaded by
   `NodePanel::buildCodeSection`; the panel widens to 380 while the section is open (`onCodeToggled`, `WorkspaceController`); tests in
   `tests/test_nodepanel.cpp` (`WrangleCodeTest`, curvyTests 581). Not tried live.
5. **Python**: `curvy.check_wrangle`, `doc.set_wrangle` if the existing setter does not already cover it; wrapper tests and docs.
6. **Examples**: `Resources/samples/wrangle/*.wrangle` (they can now carry comments), `docs/wrangle-editor.md`, a test that cooks each.
7. Tracker rows, GUI test in `tests/gui/` (ask before running).
