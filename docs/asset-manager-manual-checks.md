# Asset Manager: manual checks

Things the GUI driver cannot reach (native dialogs) or nobody has tried by hand yet. Build: `out\build\x64-release\curvy.exe`, close it before rebuilding.
Open the Assets panel (right dock). Library folders are the app's `Assets/` folder plus any you load. Each check lists the steps and the expected result.

## 0. Setup used by several checks
1. New document (A4). The graph editor cannot make a subnet yet (no Add node menu, no "group into subnet"), so a script does it: Scripts button at the top,
   then the sample `subnetForAssets`. It adds a circle node `ring`, a subnet `box` (a `subnetInput` and a `transform` inside it) wired from the circle, and selects
   `box`. Check in the graph view that `box` is drawn as a subnet (its own colour; double-click enters it and shows the two nodes, Backspace goes back).
   Run it again in a fresh document whenever a check says "make a subnet".
2. With `box` selected, press `+` in the Assets panel. The Asset Properties dialog opens filled from the subnet (Contents tab: 2 nodes).
3. Basic tab: name `TestAsset`, a category. Parameters tab: add one float parameter, `size`. Save. Expected: the subnet is replaced by an instance of `TestAsset`, and `TestAsset` is in the list, version 1. One Ctrl+Z restores the subnet.

## 1. Edit-contents flow
1. Select the `TestAsset` instance. In the panel press `Edit...` (the instance is unlocked and the dialog opens on it).
2. Close or cancel the dialog if it opened, then enter the unlocked subnet (double-click it) and change a value: select `move` and set `tx` in the Node panel. (Nodes
   cannot be added from the graph view yet; changing a value is enough to make version 2 different.)
3. Select the subnet, press `+`, name it exactly `TestAsset` again, Save.
4. Expected: version becomes 2, other instances (add one with `Add` first) show `Outdated 1` and the banner "1 instance is older than v2, Update".
5. Press `Update`. Expected: the older instance now has the new contents, the banner goes away. The status line says it cleared undo (Update is not undoable).
6. Check Edit... on a plain graph with nothing selected: should do nothing harmful and show a message in the status line.

## 2. Match node
1. Make an instance of `TestAsset`, press `Unlock`. Change its contents in the graph.
2. Select the unlocked subnet. Select `TestAsset` in the list, press `Edit...` and go to the Contents tab.
3. Press `Match node`. Expected: the dialog's contents and parameters are replaced by those of the selected subnet, the footer note says what a save will do, Revert undoes it.
4. Nothing selected, or a non-subnet selected: expected an error message, definition unchanged.
5. Save. Expected: version +1, instances outdated, Update brings them up to date.

## 3. Native file dialogs
All titles below are the window titles of the native dialogs.
1. `Save to library` (panel): title "Save asset to the library". Pick a folder, an existing file name (overwrite prompt?), and cancel once. Expected: a `.asset.json` written, entry loses the `Embedded` badge; cancel changes nothing.
2. Panel folder menu: `Load asset file...` (title "Load asset") with a valid file, a broken file (expected: error on the status line, nothing registered), and cancel.
3. `Load assets from folder...` (title "Load assets from a folder"): a folder with several assets, an empty folder, one containing a bad file (the good ones should still load).
4. Asset Properties dialog, Save with a new asset (title "Save asset"): confirm the path is remembered and the file is written before the registry changes (a failed write must leave the library unchanged: try a read-only folder).
5. Files tab, `Add...`: pick a text file and a binary file. Expected: listed with name and size; Remove works; Save then reload shows them again.
6. Trust: load an asset with a Python module from outside the `Assets/` folder. Expected: a prompt once, remembered by hash (check `trustedAssets.txt`); edit the file and load again, expected a prompt again.

## 4. Ctrl+C / X / V in the graph editor
1. Run `subnetForAssets` and `pythonNodeBasics` (Scripts button) in one document: that gives `ring` -> `box` (connected) and a second `ring` (its name gets a number) -> `stretch`. In the graph editor select
   `ring`, `box` and one node of the other pair (marquee or Shift-click). Ctrl+C, Ctrl+V. Expected: copies appear offset, connection between the two copies is kept, links to nodes outside the selection are dropped, the pasted nodes are selected.
2. Ctrl+X: nodes disappear, Ctrl+V puts them back. Ctrl+Z after paste removes only the pasted ones, one step.
3. Paste into another document (and another subnet level via the breadcrumb). Expected: works; names stay unique.
4. Paste with an empty clipboard, or after copying text elsewhere: nothing happens, no error.
5. Select an asset instance, copy, paste. Expected: the pasted instance fires `onCreated('pasted')` (check with a script that prints).
6. Things the editor refuses (`SketchDocument::refuseGraphCopy`): try copying the document's root-level required nodes; expected: a message, no change.
7. Feel: shortcuts do not fire while typing in a parameter field or the Python editor.

## 5. A real onVersionUpdated script
Needs the asset in the trusted `Assets/` folder (or answer the trust prompt first; the migration script is skipped while untrusted and the status line says so).
1. Make `TestAsset` with parameter `size` (version 1), put an instance in a document, set `size` to 10, save the document to `old.curvy`.
2. Edit the asset (section 1 or the dialog): rename nothing, but change the meaning, e.g. add a parameter `sizeCm`. On the Python tab add the event:
   ```python
   fresh.set_float("sizeCm", old.get_float("size") * 2.54)
   ```
   An event is a script body (statements, no `def`); `old`, `fresh`, `inputs`, `from_version`, `to_version` are plain names (the unit tests also reach
   the versions as `kwargs['to_version']`). Parameters are read and written with `get_float` / `set_float`, as in `tests/test_assetpanel.cpp`. Save (version 2).
3. Press `Update` in the panel. Expected: the instance shows `sizeCm` = 25.4 and no error on the status line.
4. Open `old.curvy` (still saved with version 1). Expected: the same migration happens on load (it runs twice on load, so the script must have no side effects) and the status line shows no failure.
5. Failure cases, each should keep the default result and show a message, never crash: a script that raises, `while True: pass` (5 second limit), editing another node, reaching for `doc` or `network` (not available), a bad `inputs` reorder.
6. Untrusted: put the asset outside `Assets/`, open `old.curvy`. Expected: default migration, a status note, and the trust prompt afterwards; after allowing it, Update runs the script.

## Report back
Anything that crashes, leaves the library half-registered, or leaves undo in a strange state is the interesting kind of failure; note the exact steps.
