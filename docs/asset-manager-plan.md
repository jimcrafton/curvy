# Asset Manager: plan

A UI and scripting layer for authoring jitnodes assets (a subnet saved as a node type, see `3rdparty/jitnodes/docs/assets.md`), modelled on
Houdini's digital assets (HDAs). Status: **plan and mockups only, nothing implemented.**

Mockups (open in a browser): `docs/mockups/assetManager.html` (right-dock panel) and `docs/mockups/assetProperties.html` (the Asset Properties
dialog). Both convert with `html2newui.py` (checked: only cosmetic padding warnings and two placeholder `SubView`s for the contents preview and
the Python editor).

## 1. Feasibility

Possible; nothing blocks it.

| Need | State |
|---|---|
| Save, register, version, update, unlock, migration hooks | Done in jitnodes (`graph/serialize.h`, `graph/network.h`), tested. |
| A library UI | A basic one exists in the jitnodes editor (`editor/editorModel.h`: `assetInfo`, `createAsset`, `loadAssetsFrom`, `reloadAsset`, `unlockAsset`). Not in curvy. |
| Python access | None. scriptgen marks only `Network`, `Node`, `Geometry`, `History`, `Keyframe`; the asset calls are free functions, so they need hand-written bindings. |
| Definition metadata | None: no label, category, icon, help, author, parameter range or group. The file is JSON5 with `format` and `version: 1`, so optional keys can be added without breaking old files. |
| Parameter editing | `addSpareFloat` only. No remove, rename or metadata API. Floats only. |
| Existing users | Graph brushes are assets (`docs/graph-brushes.md`: an asset is a brush when it promotes `referenceWidth` and `guideLength`). |

Limits that stay: promoted values are read through `parent.x` in expressions (not in wrangle code); instances cannot be entered (unlock first);
updating instances clears undo; registration is process-global; floats only in version 1.

## 2. What we take from Houdini

Houdini: `Node.createDigitalAsset` (subnet to asset), the Type Properties dialog (Basic, Parameters, Scripts, Help, Extra Files), the Operator Type
Manager (libraries, embedded vs file), `hou.hda` (`installFile`, `definitionsInFile`), `HDADefinition` (`save`, `updateFromNode`,
`setParmTemplateGroup`), and a per-asset Python module (`hdaModule`) stored as a named section.

| Houdini | Here |
|---|---|
| Create from subnet | existing `createAsset` / `saveAsset` |
| Type Properties | Asset Properties dialog (`assetProperties.html`) |
| Operator Type Manager | Asset Manager panel (`assetManager.html`) |
| `HDADefinition` | `AssetDefinition` value type, editable from Python |
| `hdaModule` and event handlers | `scripts` in the definition: `module` as `curvy.assets.<name>`, plus event scripts (section 5a) |
| Allow Editing of Contents | Unlock (exists) |
| Match Current Definition | `AssetDefinition::applyTo` / "Match node" button |
| Event-callback scripts, interactive handles | not in scope |

## 3. Phases

| Phase | Work | Where |
|---|---|---|
| C | HTML mockups (**done**) | `docs/mockups/` |
| A | Core: `AssetDefinition`, `AssetLibrary`, format extension, spare-parameter API | `3rdparty/jitnodes` (branch `python`, carry to `D:\code\jitnodes`) |
| B | Python: `jitnodes.assets`, curvy wrapper, tests, `docs/py-curvy-module.md` update | jitnodes `python/`, `src/scriptmodule.cpp` |
| D | UI: `AssetManagerModel`, `AssetManagerPanel`, `AssetPropertiesDialog` (convert the mockups, then wire) | `include/curvy/`, `src/`, `Resources/` |
| E | Curvy integration: panels menu, node palette, graph-brush badge, create-from-selection | curvy |

Order: A, then B, then D, then E. A and B come before the UI so the UI can be a thin client of the same calls scripts use. Each phase is one
tracker row.

## 4. Core classes (phase A, jitnodes)

All plain value types, so Python can hold copies that cannot dangle.

### `AssetParam`
`name`, `label`, `defaultValue`, `min`, `max`, `step`, `group`, `tooltip`. A `type` field (default `float`) is reserved for later; only `float` is
accepted now.

### `AssetDefinition`
Fields: `name`, `label`, `category`, `description`, `author`, `icon`, `version`, `inputs`, `inputLabels`, `params`, `renames` (old name to new name),
`help`, `scripts` (event name to source), `extraFiles` (name to bytes), `graphText`, `source`.

| Method | Detail |
|---|---|
| `static fromNode(const Network&, NodeId subnet)` | Builds from a subnet: child graph via the existing writer, promoted names and defaults from the spare parameters. |
| `static fromText(const std::string&)` / `toText() const` | JSON5 round trip. Unknown keys are kept, so newer files survive an older reader. |
| `std::string validate() const` | Empty when fine. Checks name rules and a built-in clash, inputs 0 to 16, duplicate or empty parameter names, min <= default <= max, event scripts (syntax, then the per-event rules of section 5a), then builds one throwaway instance to check the graph. Must not touch the registry. |
| `void applyTo(Network&, NodeId subnet) const` | Writes contents and parameters back into an unlocked subnet (Match node, Unlock). |

New optional file keys: `label`, `category`, `description`, `author`, `icon`, `inputLabels`, `params` (array of objects; the old `promote` map stays
valid and is read as params with default only), `renames`, `help`, `scripts`, `files`. `version` stays 1 (the file format); `assetVersion`
stays the asset's own number.

### `AssetLibrary`
A thin facade over `NodeRegistry` (the registry keeps the text, version and revision; the library adds source, origin and notifications).

| Method | Detail |
|---|---|
| `list() const` | `AssetEntry`: name, label, category, version, source, `embedded`, instance counts per network. |
| `definition(name) const` | Parses the registry text into an `AssetDefinition`. |
| `installFile(path)`, `installDirectory(path)` | Wraps `registerAssetFromFile`; records `source`. |
| `save(AssetDefinition, path)` | Validate, write the file, then register; a failure leaves the registry as it was. Version is registered + 1. |
| `apply(AssetDefinition)` | Register in memory only (the dialog's Apply); `embedded` origin until saved. |
| `uninstall(name)` | New: `NodeRegistry` has no unregister. Refuses when instances exist unless told to unlock them. |
| `reload(name)` | Re-reads the source file. |
| `updateInstances(Network&, name)` | Wraps `updateAssetInstances`; returns the count; clears undo (the caller says so). |
| `onChanged(callback)` | List of callbacks so panels refresh. |

### Subnet parameter API
`Node::removeSpareFloat` exists; add `renameSpareFloat(old, new)` and `setSpareMeta(name, AssetParam)`. A rename must rewrite `parent.old` in the
child graph's expressions and record `renames` in the definition so instances saved with the old name migrate without a Python hook (the
existing hook mechanism stays for anything more complex). This is the riskiest piece, so it gets its own tests (expressions that mention
`parent.x` inside nested subnets, and a name that is a prefix of another).

## 5. Python (phase B)

New module surface, hand-written (the generator cannot do free functions):

- `jitnodes.assets.list()`, `get(name) -> AssetDef`, `install(path)`, `install_directory(path)`, `create(subnet_node, name, path=None) -> AssetDef`,
  `save(def, path=None)`, `uninstall(name)`, `update_instances(network, name=None) -> int`, `unlock(node)`.
- `AssetDef`: editable value type with the `AssetDefinition` fields plus `add_param(name, default, ...)`, `remove_param(name)`,
  `rename_param(old, new)`, `validate()`, `save(path=None)`. It is a copy; it holds no engine references.
- `scripts`: a map in the definition, `{ module, onCreated, onLoaded, ... }` of source text (replaces the single `pythonModule` field). `module` is
  imported lazily as `curvy.assets.<name>`; event scripts are functions called with `kwargs` (see section 5a). They run on the existing
  `ScriptConsole` and time-limit machinery. Assets from outside the app's own folders ask before any of it runs (security).
- Curvy side: `doc.assets` is a wrapper holding a `DocumentGuard` and checking it on every call (`docs/py-curvy-module.md`). Operations that
  clear undo are reported to the script and are not part of the script's single undo step. New pybind classes go through `guardClass`.
- Same task as the code (CLAUDE.md): tests in `tests/test_scriptmodule.cpp`, `docs/py-curvy-module.md`, and `// @reflect ignore=true` on any new
  header class `reflectgen` cannot register.

## 5a. When asset Python runs (events)

The Python page of the dialog (`assetProperties.html`) lists every event with a one-line "runs when" and its arguments, so nothing is implicit.

### What other programs do

| Program | What it has | What we take |
|---|---|---|
| Houdini ([HDA event handlers](https://www.sidefx.com/docs/houdini/hom/hou/HDAModule.html)) | Per-asset scripts `OnCreated`, `OnLoaded`, `OnUpdated`, `OnInputChanged`, `OnNameChanged`, `OnDeleted`, plus type-level `PreFirstCreate`, `PostLastDelete`, `OnInstall`; each gets a `kwargs` dict (`node` or `type`); a shared `PythonModule`. Forum threads show the pain points: `OnLoaded` not firing for assets created inside another asset, unclear order against `OnCreated`, and a file in `HOUDINI_PATH` silently overriding the embedded script ([example](https://www.sidefx.com/forum/topic/84362/)). | The event set and the `kwargs` style. State the order and the nested case in the docs and tests. No silent override. |
| Nuke ([callbacks](https://learn.foundry.com/nuke/developers/140/pythondevguide/callbacks.html)) | `onCreate` (any creation: load, paste, undo) vs `onUserCreate` (only a user adding it, right after defaults), `onDestroy`, `knobChanged`, `updateUI`; callbacks can be limited to one node class; render hooks `beforeRender`, `beforeFrameRender`, `afterFrameRender`, `afterRender`; `knobChanged` does not fire when the panel is closed. | Split interactive creation from every creation (`kwargs['interactive']`). Render and export hooks as a later phase. |
| Blender ([app handlers](https://docs.blender.org/api/current/bpy.app.handlers.html)) | `frame_change_pre` and `frame_change_post` (before or after the new frame is evaluated), `depsgraph_update_post`, `load_post`; handlers are dropped on file load unless marked `persistent`; during rendering the frame handlers run on a different thread from the viewport. | Say explicitly whether a frame event is before or after evaluation (ours: after). Never run script code on a worker thread. Make lifetime across document loads explicit (ours: scripts belong to the asset, not the session). |
| Maya ([scriptJob](https://help.autodesk.com/cloudhelp/2023/JPN/Maya-Tech-Docs/Commands/scriptJob.html)) | Named events (`SceneOpened`, `timeChanged`, `attributeChange`); idle events warned about for CPU use; time-change events are not forwarded at full rate during playback; scriptJobs do nothing in batch mode. | Throttle frame events during playback. Document what happens headless (ours: events still fire in `--run` scripts, but the UI-only ones do not). |
| Cavalry ([JavaScript layers and utility](https://cavalry.studio/docs/nodes/general/javascript-layers/)) | Per-frame logic lives in a node (JavaScript Utility, Shape, Deformer, Modifier) that is part of the cook, plus Pre/Post-render scripts. | Per-frame *computation* belongs in a node, not in an event hook (see "Animation" below). |

### Events (version 1 proposal)

All run on the main thread, outside a cook, with `kwargs` (`node` is the instance handle). Anything a script changes is part of the one undo step
that caused the event; an error is reported in the console and the status line, and never blocks loading a document.

| Event | Runs when | kwargs |
|---|---|---|
| `module` | once per session, first use of the asset | none |
| `onCreated` | an instance exists: Add, paste, undo of a delete, or opening a document (held back until the whole document is built, so other nodes can be looked at) | `node`, `reason` (`added`, `pasted`, `undo`, `loaded`) |
| `onVersionUpdated` | an older instance is rebuilt from a newer version of the asset, for changes `renames` cannot express (a new unit, say) | `old`, `fresh`, `inputs`, `from_version`, `to_version` (the C++ `AssetUpgrade`) |
| `onInputChanged` | an input was wired, rewired or disconnected | `node`, `index` |
| `onParamChanged` | a promoted parameter was edited by a user, script or undo; not for animated values while the timeline plays | `node`, `name`, `value` |
| `onFrameChanged` | the playhead moved, after the frame is shown; at most once per displayed frame, skipped while the previous call is still running | `node`, `frame`, `playing` |
| `onDeleted` | an instance is about to be deleted (not when the document closes) | `node` |
| `onInstalled` | the asset was registered in the library | `name`, `version` |

Order when a document opens: instances are migrated first (`onVersionUpdated`), then the document is built, then every `onCreated` runs with
`reason='loaded'`; nested assets run inside-out, so an inner instance's `onCreated` runs before its parent's.

`onVersionUpdated` differs from the others: it is the C++ migration hook, so it must be a pure function of its arguments (loading runs it twice,
once to validate). It can read `old`, write `fresh` and reorder `inputs`, and the runtime refuses any call that reaches the document.

### Keeping scripts from changing the asset's own result

An event that fires during animation must not change what the asset cooks (`onFrameChanged`: an edit to the instance, or to anything it reads
from, makes the result depend on how the user scrubbed, and the edit itself re-triggers a cook). Two layers, because Python is too dynamic to
prove anything statically:

1. **Runtime guard (the backstop, kept cheap).** The commit check catches the obvious cases, so the guard only has to stop what slips through,
   and it is built so that it costs nothing when it is not needed:
   - It exists only while an event script runs. The state is one small struct on the `DocumentGuard` (`docs/py-curvy-module.md`) set before
     the call and cleared after it; outside an event the cost is one pointer test per call. Nothing is added to the cook path, to reads, or to
     the engine (the same "no work per call on the cook path" rule as `3rdparty/jitnodes/docs/python.md`).
   - Only mutating calls are checked, never reads. The first test is O(1): the target is the instance, or its network is the instance's child
     network (inside it). That rejects the direct hits with a `PermissionError` naming the line and the reason.
   - "Upstream of the instance" (a node the asset reads from) is a graph walk, so it is done lazily: only when an event makes its first mutating
     call on some other node, and cached for the rest of that call and until the network's topology revision changes. An event that edits
     nothing, or only edits its own results' downstream, never pays for it. If measurement (below) shows it is still too much, version 1 ships
     with the O(1) rule only and the upstream rule is added later.
   - `onFrameChanged` is already limited to once per displayed frame and skipped while the previous call runs, so the guard cannot pile up.
   - Re-entrancy is refused with a depth counter (an event cannot start itself, `onParamChanged` setting its own parameter); nothing can edit
     during `onVersionUpdated`.
   - Not caught on purpose: anything that only ends up affecting the result indirectly through code the checks cannot see. The aim is to stop
     the obvious failures, not to prove isolation.
2. **Commit check (a gate).** When the user commits the scripts (Apply or Save, `AssetDefinition::validate`), every event script is parsed with
   `ast` and checked against the rules of its event; a problem rejects the commit and nothing is registered or written. The same editor pass
   runs while typing and shows the first problem on the problem line (`ScriptConsole::checkSyntax`, `SyntaxProblem`: line, column, message), so the
   user sees it before pressing Save. What it reports for `onFrameChanged`:
   - calls to mutating handle methods (`set_float`, `set_expression`, `set_string`, `add_spare_float`, `connect`, `disconnect`, key calls,
     `remove_node`) or an assignment on a name that came from `kwargs['node']`, including through a simple alias (`n = kwargs['node']`);
   - dynamic constructs that would hide such a call: `getattr` / `setattr` with a non-literal name, `exec`, `eval`, `__import__`, `compile`;
   - a call to a function of the asset's own `module` that itself contains any of the above (the call graph inside `module` is followed; a call
     to code outside the asset is allowed but left to the runtime guard).

   The check lives in the core `validate()`, not in the dialog, so it also covers a hand-edited `.asset.json`, `AssetDef.save()` from a script and
   a definition embedded in a document. The static check is conservative about what it rejects (only definite hits, so a false rejection is
   rare), and it is not a proof: edits through code outside the asset, or through something the analysis cannot follow, are still stopped by
   the runtime guard, which stays the actual enforcement.

A rule for one event is cheap to state, so the table for the dialog is: `onFrameChanged` cannot change the instance or its inputs side;
`onVersionUpdated` cannot touch the document; all other events can edit the instance and the document, in the one undo step that caused them.

### Animation

Curvy animates through keyframes and expressions, and a cook may run on several threads while the engine is not safe for edits from another
thread (`3rdparty/jitnodes/docs/python.md`, "Threads"). So:

- **Per-point and per-frame maths stay in the wrangle language and expressions.** They compile and run in parallel; Python in the cook path would
  hold the GIL, serialise the cook and add per-frame call cost.
- **`onFrameChanged` is for side effects, not for the asset's result:** drive another node, update a custom overlay, log, sync to something outside.
  It sees the frame after display, so a script that changes this asset's own parameters from it makes the animation depend on how the user
  scrubbed (playing, skipping, stepping). The dialog says so, and `kwargs['playing']` is available later if needed.
- **Later (not v1): a Python node**, a node type whose cook calls `cook(ctx)` for each frame (as Cavalry's JavaScript utility or Houdini's Python SOP
  do). It is marked single-threaded, results are cached per frame like any node, and it has the script time limit. Reasonable for rigs and drivers
  (one control mapped to several targets, as the "Driver" in `ui-design.md`) that are too awkward for expressions.
- **Later: export hooks** `onBeforeExport`, `onBeforeFrame(frame)`, `onAfterFrame(frame)`, `onAfterExport` for animation export (frame sequences,
  EXR), the Nuke and Cavalry pre/post render hooks. A script here can prepare a state per frame or write extra files.
- **Later: button parameters** with a callback (Houdini "Callback Script", Nuke `knobChanged` on a button). Needs a non-float parameter type.

### Safety

Maya and Blender both treat auto-running scene code as a risk (Blender asks before auto-running scripts in a file it opens). Here, assets from the
app's own `Assets/` folder run; any other asset, including one that arrives embedded in a document, asks once before its first event or module runs, remembered by
file hash, and the dialog shows which events have code (the checkbox in the list). Events are off by default for new assets.

### Built in phase B

`jitnodes.assets` (`3rdparty/jitnodes/python/pyAssets.cpp`), the script check (`pyAssetCheck.cpp`: Python's own `ast`, run by the embedded
interpreter, installed as `AssetDefinition::setScriptValidator`, so `validate()` / `apply()` / `save()` refuse a definition with a bad script),
and the runtime guard (`pyGuard.cpp`: `EventRestriction`, `jitnodes.assets.restricted`). Differences from the sketch above: `onFrameChanged`
checks the call itself, so a call whose target the check cannot follow is still stopped at run time; what a node "reads from" is found from the
wiring only (a parameter expression that names another node is not seen); the upstream walk is lazy and cached by topology revision, so an event
that edits nothing pays only the pointer test. Not in phase B: `doc.assets` with undo routing, firing the events, the trust prompt (phase E).
The cost test of section 8 is not written yet.

### Built in phase D

- **Models (headless):** `AssetManagerModel` (`include/curvy/assetmanagermodel.h`) and `AssetPropertiesSession` (`assetpropertiessession.h`), see
  their headers; the document is reached through `AssetManagerHooks`, so tests run without a window.
- **Views:** `Resources/assetManager.newui` and `assetProperties.newui` are converted from the mockups (`html2newui`, validated). Changes from the
  approved mockups: the panel's header row has no title (the Panel's own title bar says "Assets"), and Category is a text field (a dropdown cannot
  create a category).
- **`AssetManagerPanel`** (`assetmanagerpanel.h`): a `Panel` in the right dock (id `assets`, collapsed until opened, because it is long). Rows are
  pooled views in a scroll view; the action buttons are placed in two rows of three by hand. Callbacks: new, edit, save to library, load file,
  load folder, message, menu.
- **Dialog:** `assetpropertiesdialogsession.h` (`assetpropsdialog::Session`) binds the controls to the session; `AssetPropertiesDialog::run` shows it
  modal. Pages are the cards of a `CardLayout` (tab buttons), the parameter list and event list are rows built in code, the Python page puts a
  `CodeEditorView` inside the mockup's placeholder and feeds its problem line from the commit check, Apply and Save are disabled while the
  working copy does not validate. The "runs when" sentences are in the header (`kEvents`), copied from the mockup.
- **Wiring** (`WorkspaceController`): `createAssetPanel`, `newAsset` (`+`: from the selected subnet, else blank), `editAsset`, and the graph edits
  behind the buttons: Add (one undo step, wired from the selected node, selected afterwards), Update (snapshot and `commitGraphEdit`, one step),
  Unlock (same), and replacing the subnet by an instance after Create (same). Library folders at start-up: `Resources/assets` next to the
  executable (ships `wobble.asset.json`) and `assets` next to the settings file (`userAssetsFolder()`, the default save location).
- **Behaviour worth knowing:** the panel follows the graph only while it is open (refreshes when opened); an instance that comes back from undo is
  built from the registered definition, so it is not "older" again.
- **Seen in the real window (GUI test, 2026-11-17):** the panel and all pages of the dialog lay out and work with real clicks and typing. Fixed on the way: the dialog's layout was fixed-size so its footer fell outside the window (now flexible, like the settings dialog); rows of the parameter and event lists need the explicit heights the mockup gives their containers; the code editor needed `setVisible(true)`; the "runs when" text is top-aligned.
- **Not done:** Edit... on the contents (adds an instance and unlocks it, the user then uses `+` with the same name to re-create the asset) works through callbacks but has not been tried by hand; the file dialogs (Save, Load, add file) are native windows the GUI driver cannot drive, so they are untested; the other GUI suites were not re-run after the new panel joined the dock.
- **Tests:** `tests/test_assetmanager.cpp` (9, models), `tests/test_assetpanel.cpp` (6 panel/controller, 5 dialog) and `tests/gui/test_curvy_assets.py` (8, real input; run by hand: `python D:\code\curvy	ests\gui	est_curvy_assets.py --exe <curvy.exe>`).

### Built in phase E (first part, 2026-11-17)

- **Running an event:** `ScriptConsole::runAssetEvent` (`scriptconsole.h`) runs the asset's event text with `kwargs` (`node`, plus string values such as `reason`),
  `node`, `network`, `doc`, `module`, `curvy` and `jitnodes` as globals, inside `jitnodes.assets.restricted(node, event)`. `module` is a
  `types.ModuleType` named `curvy.assets.<name>`, built once per session and asset version. Edits made through `doc` join the undo step that is open in
  the document; edits through the raw `network` handle are not undoable (the general rule for scripts).
- **Who decides:** `AssetEvents` (`assetevents.h`, headless): `fire` / `fireCreated`, the trust rule (an asset whose source file is under a trusted folder runs;
  anything else, embedded included, asks once through the trust prompt, a refusal lasts for the session) and the allowance, remembered by a hash of the
  scripts in `trustedAssets.txt` next to the settings file, so an edited script asks again. The controller trusts `Resources/assets` and the user's
  `assets` folder, and its prompt is a Yes/No message box.
- **Fired so far:** `onCreated` with reason `added`, from the panel's Add, before the step is merged, so a script's `doc` edits are part of "Add". An error shows
  in the status line.
- **`loaded` and `pasted`:** `AssetEvents::fireCreatedFor` (nodes of one scope, plus the contents of the subnets among them, inner first) and
  `fireCreatedInDocument`. `loaded` fires from `WorkspaceController::fireLoadedEvents` after File > Open succeeds and after a script's `doc.open` (waits
  for the script to end, since the interpreter is busy), as one undo step "Asset events" with errors in the status line. `pasted` fires from the graph
  editor's Ctrl+V (`GraphView::onPasted`, set by `WorkspaceController`), inside the paste's one undo step.
- **Graph copy/paste (added with it):** `GraphDocument::copyGraphNodes` / `pasteGraphNodes` (clipboard text is jitnodes' `jitnodes-nodes` JSON, so a copy
  also pastes into another document; wires between the copied nodes are kept, wires to other nodes are not; layout positions move by 30 so the
  copy does not sit on the original), `GraphView::copySelected` / `cutSelected` / `pasteClipboard` on Ctrl+C / X / V. Layers, groups and the document's own
  nodes are not copied (`SketchDocument::refuseGraphCopy`). Pasting an asset the document has not registered fails with a status-line message.
  Instances inside an asset instance are not visited (instances cannot be entered).
- **`undo` reason:** `WorkspaceController::stepHistory` lists the document's asset instances before an undo or redo and, afterwards, fires `onCreated('undo')`
  for each one that is new (`AssetEvents::instancesOf` / `fireCreatedForNew`); a redo of an Add counts too. A script that edits the document makes a new
  undo step (merged as "Asset events"), which ends the redo history; a script that edits nothing leaves redo alone.
- **The other events** (all in `WorkspaceController::wireAssetEvents` / `AssetEvents`; values reach the script typed: `index`, `frame`, `version` int, `value`
  float, `playing` bool):
  - `onDeleted(node)`: `GraphDocument::beforeRemove` / `afterRemove` wrap `removeGraphNode(s)`; the event runs with the nodes still in place (also for
    instances inside subnets that go with the delete), and the script's steps are merged with the delete into one step. Deleting from inside a running script
    does not fire it (the interpreter is busy); undoing an Add does not fire it either.
  - `onInputChanged(node, index)` and `onParamChanged(node, name, value)` work by comparison, not by hooks: `AssetEvents::remember` records the wiring and the
    stored values (`baseFloat`, so animation never counts) of the instances whose asset has such a script, `fireChanges` diffs and fires. `fireChanges` runs from
    `GraphDocument::afterEdit` (after `commitGraphEdit` pushed the step; the script's steps are merged into it) and after an undo or redo (`stepHistory`), so
    an undo that unwires an input fires the event. `remember` also runs when a document is shown or its `loaded` events are done. What an event script itself
    changes is not reported back to it. Edits made while a user script runs are reported by the next edit, not at once.
  - `onFrameChanged(node, frame, playing)`: `GraphDocument::onTimeMoved` (after the views were told), skipped while a script runs. There is no timeline UI or
    playback yet, so `playing` is always false and nothing sets the time but `GraphDocument::setTime`.
  - `onInstalled(name, version)` (`node` is None): a library listener queues `flushInstalledEvents` on the run loop; it announces every asset whose name or
    version was not seen yet (what was installed before the console existed is not announced). An asset authored in the dialog asks the trust prompt once
    when it has scripts, like any other asset outside the trusted folders.
- **Still to do:** `onVersionUpdated` (see below); `doc.assets` and its undo routing; the guard cost test.
- **`onVersionUpdated` is not built.** It is the core's migration hook (`NodeRegistry::setAssetMigration`), called from `updateAssetsIn` and from loading a
  graph, with `const Node& old` and `Node& fresh` that belong to no network, while the Python handles (`PyNode`) are a network plus an id, and the read-only
  guard mode refuses every edit in every network, `fresh` included. Needs: a way to hand Python those two nodes (a scratch network holding copies, or a
  loose-node handle), a guard mode that allows `fresh` only, and an answer to which thread a document load runs the hook on.
- **Tests:** `tests/test_assetevents.cpp` and the controller tests in `tests/test_assetpanel.cpp` (the `AssetPanelTest.addScripted` helper makes an asset with
  scripts and a console).

## 6. UI (phase D)

### Asset Manager panel (`assetManager.html`)
**Add** puts an instance of the selected asset in the graph being edited (wired from the selected node into input 0, selected, one undo step; the
jitnodes editor's "Add asset"). **+** creates a new asset and opens the Asset Properties dialog in one of two ways: with a subnet selected in the
graph the dialog is filled from it (`AssetDefinition::fromNode`; Save writes the file and replaces the subnet with an instance, the existing
`createAsset` flow); with nothing selected it opens on a blank definition (0 inputs, no parameters, empty graph; the button says Create, and the
contents are built afterwards by adding an instance and pressing Edit). The mockup shows the blank case at `assetProperties.html#new`.

Right-dock panel, 360 wide: header (title, New, library folders), search and category filter, the list grouped by category with badges
(`Brush`, `Embedded`, `Outdated n`) and the version, an "N instances are older than vX, Update" banner, details of the selected asset, and the
actions Add, Edit, Unlock, Reload, Save to library, Uninstall, with a status line.

### Asset Properties dialog (`assetProperties.html`)
Modal 560 wide, drawn in the style of `nodePanelCode.html` and `swatchesTab.html` (header bar, pill tabs, amber accent, pill buttons). One page per
pill tab: Basic, Parameters, Contents, Python, Help, Files; the app shows and hides the page views. Parameters is a drag-reorderable list (as the swatch tiles) with the selected row's editor beside it. Footer: Revert, Cancel, Apply, Save.
The footer note says what a save will do ("1 instance will be outdated").

### Headless models (testable, in `include/curvy/`)
Following `NewDocumentModel` / `...Session`: the GUI binds to a model, tests drive the model.

- `AssetManagerModel`: library entries (filtered by search and category), selection, `outdatedCount()`, and commands that map one to one to the
  Python calls (`addInstance`, `edit`, `unlock`, `reload`, `saveToLibrary`, `uninstall`, `updateInstances`). Each returns an error string, like
  `EditorModel`. A UI action and a script call run the same code.
- `AssetPropertiesSession`: owns a working `AssetDefinition` copy, `dirty()`, `apply()`, `save()`, `revert()`, parameter add/remove/rename/reorder,
  a validation message per field, and `summary()` for the footer note.

## 7. Details that need care

- **Save order.** Validate on a throwaway instance, write the file, then register. A failure leaves nothing half-done (registration is global and
  not undone otherwise).
- **Versions.** Every save is registered version + 1; older instances then show as outdated until Update. Update clears undo, so the UI says so
  before doing it.
- **Embedded vs library.** Graph files already embed the definitions they use. The manager shows the origin and "Save to library" promotes an
  embedded one to a file.
- **Graph brushes.** `referenceWidth` and `guideLength` are reserved: the dialog warns when both exist and the list shows the `Brush` badge.
  Removing or renaming either asks first.
- **Contents editing.** Instances cannot be entered, so Edit is Unlock then Save re-creates the asset from that subnet (the existing flow), with a
  "Match node" button as the explicit form.
- **Python trust.** A module is code. Assets from the app's own `Assets/` folder run; others prompt once per file, remembered by file hash.
- **Undo.** Create-from-selection and Save are single undo steps; updating instances is not undoable (core limit).

## 8. Tests

- Core (jitnodes tests): definition round trip including unknown keys; old `promote`-only file still loads; validate failures; rename rewriting
  expressions incl. nested subnets; `renames` migration of an old instance; save failure leaves the registry unchanged; uninstall.
- Python (`tests/test_scriptmodule.cpp`): the calls above, stale `AssetDef` after document close, undo behaviour.
- Guard and commit check: each rejected construct (direct call, alias, `getattr` with a computed name, a `module` helper that edits, a name that only
  looks like a node), the allowed downstream edit, re-entrancy, and a **cost test** in the style of the jitnodes scaling tests: an event that makes
  100 allowed edits in a graph of 1000 nodes, with and without the guard, with a time floor; and an event with no edits must show no measurable
  difference from the guard being off.
- Models (gtest, headless): filter and group, outdated counts, every command, dialog session dirty/apply/revert and parameter reordering.
- GUI (Python, `tests/gui/`): create from selection, edit and save, update instances. Run by hand after asking.

## 9. Open decisions

1. Core work in jitnodes (branch `python`) and carried to `D:\code\jitnodes`: confirm.
2. Float-only parameters in the first version, with range, label and group: confirm (strings, ints and colours would reach into the cook path).
3. Python (module and event scripts) inside the definition file (one file, trust prompt) or beside it as `.py` files (several files): the plan assumes inside.
6. (Decided) If measurement shows the upstream walk of the runtime guard is too slow, version 1 ships with the O(1) rule only (the instance and its
   inside) and the commit check; the upstream rule is added later.
5. (Decided) Version 1 events: `module`, `onCreated`, `onVersionUpdated`, `onInputChanged`, `onParamChanged`, `onFrameChanged`, `onDeleted`, `onInstalled`.
   `onLoaded` was merged into `onCreated` (`reason`).
4. Dock panel (as drawn) or floating window.
