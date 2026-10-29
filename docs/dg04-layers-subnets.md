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
- **S3 Groups by containment.** group/ungroup/move-to-layer move chains between scopes; `groups_`/`groupProps_` removed from `SketchObject`; load builds
  subnets from old files; clip/opacity/blend read from the subnet.
- **S4 Layers by containment.** `layers_` replaced; Layers panel and order via the container merge.
- **S5 Enter/exit group** in canvas and graph dock; `groupAngle_` stays outside the graph like `frameAngle_`.
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
