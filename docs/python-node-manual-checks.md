# Python node: manual checks

What the unit tests cannot show (dialogs, the Node panel, Esc, a frozen window) and nothing has tried by hand yet. Build: `out\build\x64-release\curvy.exe`;
close it before rebuilding. The five sample scripts below are in the **Scripts** button (top bar, under "samples"); each one adds a layer with a circle
(so the document has something on the page), a circle node `ring`, a python node, and selects the python node.

Where things live (`%LOCALAPPDATA%\curvy\`):

| File | What |
|---|---|
| `settings.json5` | the settings; the node time limit is `scriptEditor: { pythonNodeSeconds: 10 }` (0 = no limit), also in Settings > Script editor > "Stop Python nodes after" |
| `trustedPythonNodes.txt` | one 16-character hash per line: the code texts you allowed. Delete it (curvy closed) to be asked again |
| `scripts\` | your own scripts, listed above the samples |

The Node panel is in the right dock. A python node shows a **Code** toggle row (like a wrangle); open it for the editor, **Commit**, **Revert**, and the line
under the editor that shows the cook error or a syntax problem. The **Data** button in the panel shows the node's points as a table.

## 0. Before you start
1. Close curvy, delete `trustedPythonNodes.txt` if it exists (so the first prompt shows), start curvy, File > New (A4).
2. Run `pythonNodeBasics` from the Scripts button. Expected: `stretch` is selected and the Node panel shows it with parameters `a` 1.5, `b` 0.75 and the Code row
   (the script's own message, `added ring -> stretch`, is in the Python console panel if that is open). Ctrl+Z removes the layer and both nodes in one step; Ctrl+Y brings them back.

## 1. The trust prompt (code a script wrote)
Code a script put into a node is not code you typed, so the first cook asks.
1. With `stretch` selected (after step 0.2) a question appears: "1 Python node in this document has code that has not been allowed yet. The code can do anything a
   program can do. Only allow it when you trust where it came from. Allow it to run?" Expected: it appears once, not once per repaint.
2. Answer **No**. Expected: the status line says "Python nodes with code that is not allowed stay disabled"; open the Code row: the line under the editor reads
   "Python nodes are not allowed here: the application has not trusted this code"; the Data table shows no rows (or the same error). Selecting another node and back
   does **not** ask again (declined for this session).
3. An **Allow** button sits at the right end of that error line (only for this error; not for a `ValueError` or on a wrangle). Press it. Expected: no question, the
   error line clears, the button goes away, the canvas and Data recook, and `trustedPythonNodes.txt` gains a line. It allows the node's own code, not what is in the editor
   if you have unsaved edits. Typing a change and pressing Commit allows the new text too (you typed it).
4. Restart curvy, delete `trustedPythonNodes.txt` again, run `pythonNodeBasics` again, answer **Yes**. Expected: the canvas and the node recook at once, no error, and
   `trustedPythonNodes.txt` now has one line.
5. Run `pythonNodeBasics` once more in another new document. Expected: no question (the same text is remembered by its hash). Then edit the code in the editor by one
   character and Commit: no question either (typed). Then in the Python console panel run
   `n = doc.add_node("python", "other")`, `doc.set_string(n, "code", "geo.resize_points(2)")`, `doc.selection.set(n)`: a question appears again, because a script
   wrote a text that is not on the list.

## 2. Reading the result: basics
1. Run `pythonNodeBasics` (answer Yes). Click the `ring` node in the graph view and press Data: note the first few x and y values. Click `stretch`, press Data.
   Expected: every x is 1.5 times the ring's, every y is 0.75 times; the row count is the same.
2. Select `stretch`, change parameter `a` to 2 in the panel. Expected: Data recooks, x is now 2 times the ring's. One undo step per change.
3. In the editor change `x *= params["a"]` to `x *= params["a"] * 2`, Ctrl+Enter. Expected: it commits (same as Commit), Data shows x 4 times the ring's. Ctrl+Z puts the old
   code back in the node and the editor shows it.
4. Edit the editor text without committing, then change parameter `b` in the panel. Expected: the unsaved text is still in the editor (the panel refreshed but kept
   it, as for a wrangle). Now select `ring` and select `stretch` again: the editor shows the node's own text and the unsaved edit is gone (selecting another node
   discards it). Edit again and press Revert: the node's text comes back.
5. Type `x = (` and wait a moment. Expected: a squiggle and a line such as "Line 1, col ...: ..." under the editor. Commit it. Expected: the node fails and the line shows a
   `SyntaxError: ...` message. Revert.

## 3. A generator with primitives and strings
1. Run `pythonNodeShapes`, answer Yes. Select `star`, press Data. Expected: 10 rows, x and y around (150, 150), a `label` column that alternates `tip` and `dent`.
2. Change parameter `a` to 6. Expected: 6 rows. Change it to 1: the code uses `max(int(a), 3)`, so 3 rows.
3. In the code, turn the `del x, y` line into a comment and add `geo.resize_points(12)` as the last line; Commit. Expected: the node fails with a message that contains
   "still alive" (the arrays `x` and `y` were alive when the code resized the points, which would move their storage). Put `del x, y` back (without the comment): it
   cooks with 12 rows. This is the rule that keeps arrays from pointing at freed memory.
4. Replace that last line with `geo.add_primitive(0, 99)`. Expected: a clear error ("a primitive of 99 points from point 0 does not fit the 10 points"). Remove it.

## 4. Errors
1. Run `pythonNodeError`, answer Yes. Expected: the line under the editor shows `ValueError: this node always fails` (one line, no traceback); the Data table is empty.
2. Change the text to `x = 1` and Commit. Expected: the error clears and the node cooks.
3. Put back `raise ValueError("again")`; Commit; select another node; select `broken` again. Expected: the error is still shown (the failing cook is retried and fails the same way).

## 5. Time limit and Esc
The limit is 10 seconds by default. To make the checks quicker, set "Stop Python nodes after" to 3 in Settings > Script editor (or `scriptEditor: { pythonNodeSeconds: 3 }` in `settings.json5`).
1. Run `pythonNodeRunaway`, answer Yes. The script selects the node, so the loop starts: the window freezes. Expected: after about the limit it comes back, and the
   line under the editor reads "Python node stopped: it ran longer than 3 seconds (change the code, a parameter or an input to run it again)" (the first time without the
   bracket). Task Manager shows curvy.exe at a core of CPU while frozen and idle after.
2. Select another node and select `runaway` again. Expected: it fails at once with the "change the code ..." message and the window does not freeze again.
3. Change the code to `pass` and Commit. Expected: it runs (empty result, no error). Put the loop back and Commit: it freezes up to the limit again.
4. Esc: Commit the loop again, and press and hold Esc as soon as the window freezes (keep curvy the focused window). Expected: it comes back within a fraction of a
   second with "Python node stopped" (no "longer than"). Esc pressed while the window is not focused does nothing.
5. With the limit set to 0 (no limit) the loop is only stopped by Esc. Try it once, with a finger on Esc.
6. Raise the limit in Settings (the dialog applies it live), and re-select the stopped node in a fresh run of the script: it runs for the new limit.

## 6. Saving, opening and the trust of a document
1. Run `pythonNodeBasics`, answer Yes, and save as `pynode.curvy`. Close curvy, delete `trustedPythonNodes.txt`, start curvy, open `pynode.curvy`.
   Expected: the document opens (the `python` node type is known to the file loader), the canvas draws the circle layer; selecting `stretch` shows the question once
   (the file's code is not on the list any more). Answer No: the node shows the "not allowed" error and nothing else in the document is affected.
2. Open the same file again after answering Yes once: no question.

## 7. Things to look at while doing the above
- Does selecting a python node ever feel slow? Selecting it cooks it (like a wrangle), so its code runs on every selection and edit of the panel.
- After a stop (section 5) does the canvas keep drawing? A stopped node leaves the display unchanged; other nodes are unaffected.
- Does the Esc key still do its usual job (closing a menu) when no python node runs? It should; the check is only looked at while a node runs.
- Does the Python console panel still work after a stop? Run `print(1)`: it should print 1 (no stray exception is left pending).

## Report back
Anything that crashes, hangs for longer than the limit, asks the question twice for the same text, or leaves the editor showing text that is not what the node runs.

## Words used here
- **`.pyd`**: a Windows Python extension module, a DLL that Python can `import`. `jitnodes.pyd` is the standalone build of the jitnodes Python module for an ordinary
  Python install (`import jitnodes`). curvy does not use it: it links the same code into the application and embeds its own Python, which is why `python/tests`
  (the Python-level tests for the module) are not run by the curvy build.
- **GIL**: Python's interpreter lock; only one thread runs Python code at a time. Python nodes on separate threads therefore run one after another.
- **Watchdog**: the background thread that stops a python node that runs too long, is cancelled, or is interrupted with Esc.
