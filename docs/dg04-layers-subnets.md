# DG-04: layers and groups as subnets

Status: **option A (containment) chosen 2026-10-28.** The S0 spike showed the subnet undo cost (21 ms per edit, 22 ms undo for 2000 nodes); the cause was jitnodes
snapshotting a subnet as one text, fixed by hierarchical snapshots (DG-07: now 1.1 / 0.6 ms, numbers in design.md). S1 onward not started. Follows DG-03 (objects are views of their nodes).

## Goal

The document's structure (layers, sublayers, groups) is stored in the graph as jitnodes subnets, so the Document view of the graph dock, the
Layers panel and the cook all read one structure. Today that structure lives beside the graph: `SketchDocument::layers_` (a tree of `Layer`),
`SketchObject::layer_`, `SketchObject::groups_` (outermost-first group ids), `groupProps_`, plus the older `SketchObject::parent_` links between
Transform nodes.

## Where things stand

- Every object has a shape node, a `curvyTransform` node and Fill/Stroke/Effect nodes, all in the **one flat root network**. Node ids are what
  selection, hit testing and the graph tree use (`selectedNodes`, `objectOfNode`, `placedOutline`).
- Layers are `Layer{id, parent, name, visible, locked, color}`; stacking order is the order of objects in the document, groups are runs of adjacent
  objects. Visibility/lock is pushed into objects (`setLayerState`).
- `parent_` already gives a transform hierarchy: a child's Transform node takes its parent's as input 1 (matrices as details `w0..w5`).
- jitnodes `SubnetNode` owns a child `Network`; ids are per network; the subnet text holds its contents and snapshot restore reloads a subnet
  whose text changed; edits inside dirty the owner; depth is capped at 64.

## Decision: A, containment

A group (and a layer) is a real jitnodes subnet; its members' nodes (shape, Transform, Fill/Stroke/Effect chain) live in its child network. Faithful to the
mockups (enter a group, Esc to leave), gives `parent.x` expressions and groups as symbols/assets later, and was judged the endpoint even if B came first.
Option B (structure nodes in the flat root) is dropped.

### Addressing nodes: `NodeRef`

Ids are per network, so a node is addressed by `NodeRef{scope, id}`: `scope` is a document-wide small integer naming the path of subnet ids from the root
(0 = root); `GraphDocument` interns paths (`scopeOf(path)`, `networkOf(scope)`, `pathOf(scope)`), never reuses a scope number, and walks the path to find the
`Network`. Undo that recreates a subnet with the same id gets the same path, so refs stay valid. A `NodeRef` packs into 64 bits for map keys and
`std::hash`. Rejected: globally unique ids in jitnodes (asset instances build their children standalone before they are attached, so ids would collide), and
`Network*` in refs (a network object is replaced when an undo recreates its subnet).

## Design

1. **Group subnet.** `curvySubnet` (a `SubnetNode`) with role `group`: params name, visible, locked, opacity, blend, clip and the six transform numbers as its own
   (promoted) parameters, so a member reads its group's matrix as `parent.tx` etc. and `worldTransformOf` walks the scope chain. Contains its members' node chains.
   Replaces `groups_` ids and `groupProps_`.
2. **Layer subnet.** Same type with role `layer`: name, visible, locked, color; children are groups and objects. Layers nest by containment. Replaces `layers_`;
   the Layers panel reads the nodes through a `LayerTree` view with the row interface of `layertree.h`.
3. **Order.** Paint order of a container is the input order of a `merge` node inside it that the container displays; each member's last node feeds one slot.
   `SketchDocument::objects()` stays the flat paint order, rebuilt by walking the tree (cached, invalidated by `graphChanged`).
4. **Parent links.** `parent_` stays (a bone-like transform link, no grouping semantics); links across scopes are not allowed (a parent link needs both
   Transforms in one network), so `setParent` rejects or ungroups across scopes.
5. **Moving a chain between scopes** (group, ungroup, move to layer) is one document operation: copyNode/pasteNodes (ids change), remove from the source,
   rewire, update the object's `NodeRef`s. Undo uses per-network snapshots (hierarchical now), so it stays O(touched).
6. **Persistence.** Graph text round-trips in SVG metadata including subnets. Old files (no subnets) get them built from `data-layer`/`data-group` on load
   (no migration step, same rule as DG-03). `.skb` v5 stores the graph chunk with the subnets.
7. **Graph dock.** Document view reads the subnets (drop the synthetic rows); enter a group = the dock and canvas scope to that child network, Esc leaves;
   detail box edits name/visible/locked/opacity/blend/clip; drag a row to reparent.

## Stages (each builds and passes ctest, `--check`, `--check-tools`, `--check-workspace`)

- **S0 Spike** done; jitnodes hierarchical snapshots done (DG-07).
- **S1 `NodeRef` and scopes. DONE 2026-10-28.** `include/curvy/noderef.h`; `GraphDocument` scope table (`scopeOf`, `scopeOfSubnet`, `parentOfScope`, `findNetwork`, `networkOf`,
  `node(ref)`, `hasNode`) and `NodeRef` forms of every edit/cook method (new nodes take a scope; connect refuses two scopes); `SketchObject::graphScope()/graphRef()`;
  jitnodes `Network::hasNode`. The old `NodeId` forms stay as root-scope shortcuts (tests and root-only code); a caller moves to `NodeRef` when the stage that
  needs scopes reaches it (selection, graph dock rows, `transformNodeOf` and the sync loops in S2/S3), because each needs per-scope reasoning, not a rename.
  Test `GraphDocument.ScopesAddressNodesInsideSubnets` (edits, undo/redo and subnet creation undone/redone). 2080 ctest pass.
- **S2 Subnet types and scoped lookups. DONE 2026-10-28.** `curvySubnet` (`kSubnetNodeType`, a jitnodes `SubnetNode`) holds `role`, `visible`, `locked`, `opacity`, `blend`,
  `clip` and the eight transform-node numbers as promoted parameters (`parent.rotation` etc.); its cook carries the contents by that matrix. `SketchDocument` gained
  `transformRefOf`, `scopeMatrix(scope)` (the groups' matrices up the scope chain) and `worldOf(NodeRef)` (parent links, then the groups). The sync loops
  (`syncPathNodes`/`syncPathScope`, `syncAppearanceNodes`, `pullPathNodes`, `pullAppearanceNodes`, `refreshLiveShapes`, `timeChanged`, `drivenPlace`) read each object's
  `graphScope()`; their caches are keyed by scope (`placedCache_`) or `NodeRef` (`appearanceChains_`, `appearanceSynced_`, `drivenPlaces_`). A parent link across scopes is
  ignored by the sync (`parentOf`), and a placement is written relative to the object's group when it has no same-scope parent. Tag, move-points, `placedOutline` and
  `adoptChain` work in any scope. Still root-only until S3/S5: `transformNodeOf`, `hitTestNode`, `selectedNodes`, `setSelectedNodes` and the graph dock (they answer
  `invalidNodeId` / skip an object in a group). Tests `SketchDocumentScopes.*` (a hand-built group moves its member, undo/redo, appearance nodes beside the shape).
  2082 ctest pass; `--check`, `--check-tools`, `--check-workspace` pass. GUI suite not rerun.
- **S2.5 Membership accessors. DONE 2026-10-28.** `SketchDocument::groupsOf`, `outermostGroupOf`, `isGrouped`, `inGroup`, `layerOf`, `setGroupsOf`, `setLayerOf` (inline in
  `sketchdocument.h`, bodies still read `SketchObject::groups()/layer()`). Every production read and in-document write outside the serializers now goes through them
  (documentlayers/clip/expand/symbols/trace/transform, sketchdocument, graphdock, layerspanel, canvasview, pdfexport, the SVG load post-pass). Still on `SketchObject`
  directly, to move in S3/S6 because they have no document in scope: `writeObject` and the SVG readers (`sketchsvg.cpp`), `sketchbin.cpp` read/write, the copy helper in
  `documentexpand.cpp` (image result), `SketchObject::clone`, and `sketchchecks.cpp`/tests. S3 then changes only the accessor bodies plus those sites. 2082 ctest pass
  (the clipboard test is flaky in a full run, passes alone); `--check`, `--check-tools`, `--check-workspace` pass.
- **S3a Containment, derived from the group ids. DONE 2026-10-28.** `SketchObject::groups()` and `groupProps_` stay the carrier for now; `changed()` runs
  `syncGroupScopes` (before `syncPathNodes`) and `pruneGroupScopes` (after). The first finds or makes the `curvySubnet` (new `groupId` parameter) for each object's
  group chain and moves the object's whole chain there (shape, deformers, placement, look nodes copied with `copyNode`/`pasteNodes`, so expressions and keys survive;
  the old nodes are removed); an object with no groups in a group subnet goes back to the root; hand-built subnets (no `groupId`) and live shapes are left alone.
  The second removes group subnets no object lives in and writes opacity/blend/clip into the rest. Group, ungroup, move-to-layer and their undo needed no change:
  they set the chain, the sync moves the nodes. Files: SVG writes `data-graph-scope="4/7"` (subnet ids from the root) and `.skb` appends the paths after the GRPH
  node ids (older files lack them and load as before); both read the scope before `add()` so the sync moves nothing at load. A `.skb` object in a subnet is saved
  with its path (not held by its node), since that reader checks the root graph only. Tests `SketchDocumentScopes.Grouping*`, `GroupedObjectsKeep*`,
  `GroupsSurvive*`. ctest passes (the clipboard test fails in some full runs, passes alone); `--check`, `--check-tools`, `--check-workspace` pass. GUI suite not rerun.
  Limits: a parent link between objects in different groups is dropped by the sync (as S2 does); a live shape (boolean etc.) stays in the root whatever its group;
  wiring from outside the chain into it is cut when the chain moves.
- **S3b Flip the carrier. DONE 2026-10-29** (in a smaller form than first planned: `SketchObject::groups_` stays as the object's requested chain, the graph wins).
  Membership: `syncGroupScopes` merges per object. If `groups()` differs from `placedGroups()` (new field, the chain last placed) it was edited, so the chain moves into the
  subnets it names; otherwise the subnet the chain sits in wins and `groups()` adopts its group ids (`groupChainOfScope`), so a node moved in the graph or an undo of the graph
  changes membership. Group props: `groupProps`/`setGroupProps` read and write the subnet's `opacity`/`blend`/`clip` (`groupSubnetOf` finds it by `groupId`, cached in
  `groupSubnets_`); `groupProps_` now holds only the props of a group with no subnet yet (a new id, or a group whose subnet was pruned), applied when the subnet is made.
  All direct writers go through `putGroupProps`. Opacity is a float in the node, so it round-trips to float precision. `GraphDocument::scopeOf` now interns the enclosing
  scopes too (a nested scope read from a file had no parent entry and `parentOfScope` threw). Loaders still build subnets from the `<g>`/group ids through the sync.
  Not done: `groups_`/`groupProps_` are not deleted (clones, loaders and unplaced objects have no document to ask), and group/ungroup still move chains in the sync rather than
  at the call. Test `SketchDocumentScopes.GroupPropsLiveInTheSubnet`; 2087 ctest (clipboard test flaky) and the three checks pass; GUI suite not rerun.
- **S4 Layers by containment.** `layers_` replaced; Layers panel and order via the container merge. **Not started; trial 2026-10-29 reverted.** A first cut (layer
  subnets with a `layerId` parameter, objects moved into root > layer > sublayer > group by the same sync as S3a, graph-wins merge for the layer) built and ran, but
  every object, in the default one-layer document too, then lives in a subnet. Result: about 15 `PathNode.*` tests, the `SketchDocumentScopes.*` tests, the big-example
  load (27 s) and the thousand-object test (4 s) failed, because root-scope assumptions are everywhere (`network()` node ids in tests, root-only Flow view, `NodeId`
  shortcuts) and the sync's per-object scope walks cost too much. Before retrying: (1) move the remaining root-only code and the tests to `NodeRef` (2026-10-30: `transformNodeOf` removed, `tagNodeOf`/`addMovePoints` return `NodeRef`, the checks and the placement/tag/key/expression tests converted; still root-only: Flow view and `GraphRow::node`, the `NodeId` shortcuts, tests that count `doc.network()` nodes, the .skb loader's curve fix-up); (2) make the sync
  incremental (only objects whose layer/groups changed) and cache chain-of-scope; (3) decide whether the default document keeps its one layer as the root itself;
  (4) only then replace `layers_` and the flat paint order with merge nodes.
  **Prerequisites 2 and 3, 2026-10-30.** *(2) Measured, not rebuilt.* Test `SketchDocumentScopes.AnEditCostsLittleWhenManyObjectsAreGrouped` (1000 objects in 100 groups, 50
  adds): about 1.2 ms per edit, of which the group sync is 0.6 ms and the path/appearance sync 0.6 ms, i.e. a linear 1 to 1.5 us per object per `changed()` (about 15 ms at
  10 000 objects). The steady-state walk is therefore not what made the trial slow; the cost is in **moving** objects (about 0.25 ms per object: copy the chain, paste, remove,
  per object) and in loading a flat file whose objects are then all moved. So the sync is not made incremental yet; what S4 needs instead is (a) files saved after S4 keep the
  subnets, so a load moves nothing, (b) the migration of an old flat file batched per source scope, (c) a per-object dirty set only if the linear walk shows up in a real
  document. One cheap fix landed: `syncAppearanceNodes` no longer rescans every node of every scope for orphaned look nodes when a single object is added (only on a removal or
  a bulk change), about 28% of the path sync at 1000 objects.
  *(3) Decision: the root holds the first top-level layer's own objects ("base layer").* Every other layer, and every sublayer, is a subnet of its parent's scope; a layer's
  sublayers are subnets inside the scope of the layer, then its own objects, which matches "paints its sublayers and then its own objects". Why: a one-layer document stays flat, so
  today's code, tests and file size are unchanged and the common case pays nothing; adding a layer moves no existing object (only the new layer is a subnet); the alternative of
  making every layer a subnet puts every object of every document through the move path and breaks the root-scope assumptions all at once (the trial). Cost: deleting or
  reordering the first layer promotes another layer to root, one batched bulk move of two layers' objects; rare, and bounded by the objects of those layers. The paint order is
  the merge node in the root: sublayer subnets of the base layer, its own objects, then the next top-level layer's subnet, and so on.
  **S4a Layer containment, 2026-10-30.** Done in the S3a/S3b way: `layers_` and `SketchObject::layer()` stay the carrier, the graph follows and wins on a mismatch. A layer other than
  the base layer is a `curvySubnet` with a `layerId` parameter (role `layer`), nested for sublayers, made by the same `syncGroupScopes` (keys are layer ids with a high bit, then group
  ids, so a group sits inside its layer); `layerPathOf` gives the path without the base layer. `SketchObject::placedLayer()` plays the role of `placedGroups()` (layer edited since the
  last sync: move the chain; else adopt the layer the scope names via `layerOfScope`); `placedBaseLayer_` makes a change of base layer (delete or reorder the first layer) count as an
  edit for every object, so they are moved and not misread. `pruneGroupScopes` also removes layer subnets no object lives in. A one-layer document has no subnet (test). Tests
  `SketchDocumentScopes.TheBaseLayerStays*`, `SublayersAndGroupsNest*`, `DeletingTheBaseLayerPromotes*`, `LayersSurviveSvgAndBinaryRoundTrips`; 2092 ctest and the three checks pass;
  GUI suite not rerun. Not done: `layers_` is not replaced (name, visible, locked, color, expanded and the layer undo steps still live there; an empty layer has no subnet; the subnet
  node is named `layerN`, not the layer's name); paint order is still the flat `objects_` order, not a merge node; the Layers panel does not read the nodes.
  **S4b part 1, 2026-10-30.** The layer subnet shows its layer: `putLayerProps` (called from `pruneGroupScopes` for every kept layer subnet) writes `visible`, `locked`,
  `color` and `label` (the exact name) and renames the node after the layer (letters, digits and underscores; a clash gets a numbered name). `layers_` is still the carrier, so
  the node follows and edits to it do not flow back. Test `SketchDocumentScopes.ALayerSubnetShowsTheLayersNameAndFlags`; 2093 ctest and the checks pass; GUI suite 57/57.
  **S4b part 2 (option 1, graph follows), 2026-10-30.** `syncPaintOrder` (end of `changed()`, after `pruneGroupScopes`) keeps a `paint` merge node in every scope that paints
  something; its inputs are, back to front, the last node of each object's chain (from the `appearanceChains_` cache, else the placement or shape node) and, at the place of its
  first member, each layer or group subnet. `objects_` stays the carrier, so edits made on the merge node do not flow back yet (that is option 2). Only a changed order is
  rewritten (an object added on top just connects one more slot); a scope with nothing to paint loses its node. Costs one node per scope, so two old tests count one more node;
  the 5000-object edit test stays under its limit. Test `ThePaintMergeNodeListsTheStackingOrder`; 2094 ctest, the checks and GUI 57/57 pass.
  **S4b part 3, empty layers, 2026-10-30.** Every layer except the base layer keeps its subnet while it is empty: `syncGroupScopes` makes the missing ones (also one under the
  wrong parent after a reparent) and `pruneGroupScopes` removes a layer subnet only when its layer is gone or misplaced and nothing lives in it. The old test that expected the
  empty layer's subnet to go now expects it to stay; new test `AnEmptyLayerHasASubnetUntilTheLayerIsDeleted`; 2095 ctest, the checks and GUI 57/57 pass.
  **S4b part 4, hybrid panel read, 2026-10-30.** `SketchDocument::stackingOrder()` returns every object id back to front read from the nodes: it walks the root `paint`
  merge node, expands a layer or group subnet where it sits in its parent's merge, and maps an input node to its object through `paintOwners_` (filled by `syncPaintOrder`);
  an object the nodes do not list comes last. The Layers panel takes the order of its object and group rows from it (`stack_`, also in the refresh signature). Layer
  records (name, flags, colour, tree, empty layers, base layer) and group membership still come from `layers_` and `groups()`, so the rows are unchanged except that
  the members of a group now sit together. Test `TheStackingOrderIsReadFromThePaintNodes`; 2096 ctest and the three checks pass; GUI suite 57/57.
  **S4b part 5, the pure node read, 2026-10-30.** The layer records are read from the nodes. A layer subnet holds `visible`, `locked`, `color`, `label` and a new `layerOrder`
  (its index among its siblings); the base layer, which is the root and has no subnet, is held by a `curvyBaseLayer` node in the root (`layerId`, flags, colour, name), present only
  while the record differs from the default (id 1, "Layer 1", visible, unlocked, automatic colour), so a plain document gains no node. `pullLayerNodes` (first step of `changed()`)
  adopts what a graph edit or undo changed in those nodes, including the sibling order (the base layer stays first), unless `layers_` itself was edited since the last sync
  (`layersEdited`, compared with `placedLayers_`), in which case `putLayerProps` and `putBaseLayer` write `layers_` out, as before. `layers_` is now a cache of the nodes plus the view
  state (`expanded`, active layer); creation and deletion of layers still come from `layers_` (a missing subnet is not read as a deletion), and so does the parent of a layer (a subnet
  dragged into another layer's subnet is not adopted). Tests `AnEditOfALayerNodeIsReadBackIntoTheLayer`, `TheBaseLayerLivesOnAMarkerNodeWhileItIsNotTheDefault`,
  `TheOrderOfLayerNodesReordersTheLayers`; 2099 ctest and the three checks pass; GUI suite not rerun.
  **S4b part 6, option 2 (reorder on the merge node), 2026-10-31.** `pullPaintOrder` (second step of `changed()`, after `pullLayerNodes`) adopts a reorder made on a `paint` merge node
  or its undo: when the object id sequence still equals `placedOrder_` (what `syncPaintOrder` last saw) and some scope's merge inputs are the same nodes as `placedPaint_` in a
  different order, `objects_` is rebuilt in `stackingOrder()` order. If `objects_` changed too (any add, remove or arrange) the objects win and the nodes are rewritten, as before. A
  change of which nodes the merge takes (not just their order) is left to `syncPaintOrder`. Not done: dragging a node into another scope's merge is not read as a move. Test
  `AReorderOfThePaintNodeReordersTheObjects`; 2100 ctest pass; GUI suite 57/57.
  **S4b part 7, the nodes carry the whole layer table, 2026-10-31.** A layer subnet also holds `layerParent` (default -1 = never written, so a file from before keeps its
  records' parents), and `pullLayerNodes` now reads back everything but `expanded` (view state): the parent (a missing parent or a loop puts the layer at the top), a subnet
  whose `layerId` no record has (becomes a layer named by its `label`), and a layer other than the base layer that has no subnet any more (removed from the layer list; the active
  layer falls back to the base layer). Tests `TheParentOfALayerNodeIsReadBackAndBadParentsAreMended`, `ALayerSubnetMadeOnTheGraphSideBecomesALayer`,
  `ALayerWhoseSubnetLeavesTheGraphLeavesTheDocument`; 2104 ctest pass; GUI suite not rerun. The scan walks the scope table, not every node: a first version that scanned all
  nodes per edit took 8 to 10 s in three perf tests, so a hand-made subnet is adopted once its scope is known (entered in the dock, a load or a restore), not when it is merely added.
  **Where this leaves `layers_`.** It holds no information the nodes lack, but it is still the edit buffer: the layer edits (add, delete, move, duplicate, merge, flatten, colour)
  and their undo snapshots (`LayerState`) write it, and the sync writes it out to the nodes. Removing the member itself means making the object-to-layer link graph-driven
  (`layerOf` read from the scope an object's nodes sit in, `setLayerOf` a move) and the layer edits operate on subnets; that brings back the per-object move cost of the S4 trial
  and touches about 30 sites in `documentlayers.cpp`, so it is a separate step, not started.
  **Plan for removing `layers_` (S4c, not started; user chose "full" on 2026-10-31).** Findings that shape it: (a) `SketchObject::layer()` is a request field like `groups_`;
  `layerOf(object)` must become `layerOfScope(object.graphScope())` once the object is placed, with the requested layer used only until its first sync (a new object wants the
  active layer). (b) Reparenting a layer cannot move the subnet node (scope ids and `graphScope` of every object inside would go stale); it works today because the sync
  compares each object's wanted scope with its placed one and moves the chains, so the new structure must exist (parent known) before the objects are compared. The edit buffer
  is what gives that ordering, so any replacement needs an equivalent (a pending table that lives only between an edit and its sync). (c) The layer undo currency can stay value
  snapshots (`LayerState`: layer list, active layer, object layers, order) as long as the list is built from the nodes (`snapshotLayers`) and written back through one
  `writeLayerTable` that creates, removes, reparents and updates subnets. (d) Keep the scope-table walk; any per-edit scan of all nodes is too slow.
  Suggested stages, each green before the next: **S4c-1** `layers()` and `findLayer` answer from a table rebuilt from the nodes (cache invalidated by `changed()` and the layer
  edits), `layers_` kept only as the pending table; test that the rebuilt table equals the buffer after every layer-edit test. **S4c-2** one `writeLayerTable(vector<Layer>)`
  used by `setLayers`, `applyLayers`, and every edit in `documentlayers.cpp`; delete `placedLayers_`/`layersEdited`. **S4c-3** `layerOf`/`setLayerOf` through the scope with the
  request field only for unplaced objects; `syncLayers` stops sorting `objects_` (the paint merge node orders it). **S4c-4** remove the member, update the SVG (`data-layers`)
  and .skb loaders, GUI suite rerun. Watch: `AnEditCostsLittleWhenManyObjectsAreGrouped`, `AThousandsObjectDocumentStaysResponsive`,
  `TheFrameOfALargeGraphIsCheapToMeasureWhileScrubbing` (the three that caught the node-scan mistake) and the 27 s big-file load.
- **S5 Scoped selection and graph dock. DONE 2026-10-28** (the enter/exit-group view is not built). `hitTestNode`, `selectedNodes` and `setSelectedNodes` use `NodeRef`;
  `selectionOrigin`/`setSelectionOrigin` go through `transformRefOf`. The graph dock works on `NodeRef` throughout (`GraphRow::scope`, `detailNode()`, detail and link rows,
  edits, paint kind, reset transform), builds appearance chains per scope, and counts nodes over all scopes, so grouped objects list and edit like root ones. Flow view
  still lists the root network only. Test `NodeSelection.GroupedObjectsAreSelectedAndPickedByTheirScopedNode`; 2086 ctest and the three checks pass; GUI suite not rerun.
  Remaining from the old S5: entering a group on the canvas, and `groupAngle_` stays outside the graph like `frameAngle_`.
  **Enter/exit-group view in the dock, 2026-10-30.** The Flow view lists one network, `GraphDock::viewScope()` (0 = root). A group's subnet node is a row of its parent's Flow
  view (type label "Group"); selecting it, or a group row of the Document view, shows an **Enter** button in the box, and `enterGroup(scope)` / `enterSubnet(ref)` switch to the Flow
  view of that scope. Inside, the first row is `..` (click it), Esc leaves while the keyboard focus is in the dock (the root-view key delegate, so Esc keeps its other jobs
  elsewhere), `exitGroup()` goes up one. The status line reads "Inside Root > group1 - Esc leaves the group", the node count is that group's, and a group that is ungrouped or
  undone away takes the view back to the nearest scope that still exists. Not done: the canvas does not scope (it still shows and picks everything); the Flow view of an entered
  group does not yet draw a different background. Checks in `--check-workspace`; GUI tests `Grouping.test_the_flow_view_enters_a_group_and_escape_leaves_it` and
  `test_a_group_row_of_the_document_view_enters_the_group` (run, pass).
  **Canvas scoped to the entered group, 2026-10-31.** `SketchDocument::enteredScope()/setEnteredScope()/inEnteredScope(object)` hold it (view state, not an undo step; the dock
  sets it in `enterGroup`/`exitGroup`/`leaveMissingScope`, `clearObjects` resets it, a scope that no longer exists reads as 0). Inside: `hitTest`, `selectAll`, the marquee, lasso,
  Select Same and the wand reach only objects in the entered scope or below it; entering drops selected objects outside it; `withGroupMembers` picks by the outermost group *below*
  the entered one, so a click picks the member (or the nested group) and not the group being edited; `paint(ctx, true)` (the canvas only, never export) draws objects outside at 25%
  opacity. Entering a layer subnet works the same way. Test `SketchDocumentScopes.EnteringAGroupLimitsPickingAndSelectionToIt`; 2101 ctest pass; GUI suite 57/57 pass, then GUI test `Grouping.test_the_canvas_picks_only_inside_an_entered_group` added (a click picks a member alone; a shape outside the entered group cannot be picked; leaving makes it pickable again); Full GUI suite 58/58 pass. Not done: a
  breadcrumb or exit affordance on the canvas itself (leaving is from the dock), double-click on a group to enter.
- **S6 Save formats:** SVG round trip; `.skb` v5 graph chunk; GUI tests for group move/undo/save/load.
- **S7 Cleanup:** delete `Layer`/`groups_` duplicates, update docs and tracker.

## Risks

- `NodeRef` conversion touches about 240 `NodeId` uses (documentlive.cpp 112, graphdock.cpp 31); S1 is mechanical but wide. Keep it a separate step.
- Moving a chain changes its node ids; anything caching ids across an edit (placedCache_, drivenPlaces_, hover/selection) must be updated by the operation.
- Clip groups ("topmost object is the mask") and opacity/blend groups render through offscreen layers; rendering keeps reading the same per-group props, from nodes.
- Boolean/pathfinder, symbols (`documentsymbols.cpp`), trace, clipboard and expand touch `groups()`/`layer()` (about 180 sites in 17 files): put an accessor
  layer in front before S3 so the flip is mechanical.
- Hierarchical snapshots currently diff per network; confirm undo across a scope move (two networks change in one step) restores both.

## Idea: `pathsnode` (packed paths), from the user 2026-10-28

A single node that accumulates many path objects and still acts as one node, like Houdini's packed geometry: it behaves a bit like a group, but you cannot step into it.
Its shapes can be selected and manipulated (move, edit points, restyle) but cannot be wired to other nodes until they are extracted; the node can extract all of its shapes
(unpack) back into ordinary object chains. Why it matters: it answers the node-count problem (10 000 nodes for 2000 objects) with one node holding N primitives, and gives
bulk content (stipples, tiles, imported SVG art, pattern output) a cheap representation. Questions to settle before building it: how an object inside it is identified (an
index, not a `NodeRef`, so selection and hit testing need a "member of a packed node" address), whether per-member fill/stroke/transform live as primitive attributes (yes,
likely, so it renders without per-member nodes), undo of member edits (the node's geometry parameter changes: one geometry diff per step), SVG import as the first producer
("Import as one packed node" for large files), and unpack/pack as document operations like group/ungroup. Not part of DG-04; it sits beside Symbols/Library.

## Not in this step

Symbols/instances, patterns, brushes as subnets (Library), dragging rows to connect, graph in `.skb` beyond the structure chunk, dopesheet.
