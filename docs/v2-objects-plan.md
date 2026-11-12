# Curvy v2 rewrite: layers and groups as Objects

Status: in progress (jitnodes steps 1 to 5 and `ObjectNode` built, 2026-11-09). Work on branch `v2`. This note records the agreed design before any of it is built; update it as steps land.

## Goal

The previous version kept layers and groups as document data. In v2 the node graph is the document, so a layer and a group are both an **Object**: a subnet that owns an object-space transform.

```
Layer1                      (Object, tag "layer")
  xform                     object-level transform, the child network's display node
    merge                   z-order is the merge's input order
      Group1                (Object, tag "group")
        xform
          merge
            stroke
              fill
                xform       local-level transform
                  shape
      Group2
```

Cook order runs shape, local xform, fill, stroke, merge, object xform, so the world transform is `Layer * Group * local`. In the tree (consumers on top, feeders below) the object xform is the first row and everything it contains hangs under it. A transform cannot be first in cook order: shapes are generators and a transform needs an input.

## Findings in jitnodes (checked 2026-11-08)

- `SubnetNode` has no transform; it is a `Node` with a child `Network` whose output is the display node.
- `TransformNode` has only `sx, sy, tx, ty` (`x = x*sx + tx`): no rotation, skew or pivot.
- `Node` has no tag.
- `MergeNode` does not override `inputsOptional()`, so an empty group would fail to cook.
- Serialization: every node write goes through `writeNode`, every node read through `applyNodeBody`. Load, paste and snapshot restore share `applyNodeBody`. `loadGraphBody` starts with `net.clear()`, so nodes a constructor made are replaced by the saved ones. Snapshots store a subnet's children separately from its text.

## jitnodes changes (edit in `D:\code\jitnodes`)

1. Split `writeNode` into the envelope (`id`, `type`, `name`, `inputs`, `graph`) plus `writeNodeBody`; rename `applyNodeBody` to `readNodeBody`; move the child-graph handling out of the body. No behaviour change.
2. `NodeWriter` / `NodeReader` interfaces and virtual `Node::save` / `Node::load`, defaults reproducing the current body. `node.h` does not depend on json5.
3. Node tag: `tag()` / `setTag()`, `Tag::parse` for `key:value` (value string, number or bool). Saved in the body; changes `revision_` but does not dirty the cook. Open: one value or a space-separated list (leaning list).
4. Transform node gains `rot`, `skew`, `px`, `py`; composed as `T(t) R Sk S T(-p)`; defaults keep old graphs unchanged.
5. `MergeNode::inputsOptional()` returns true.

## curvy changes

- `ObjectNode : SubnetNode`, registered as `curvyObject`. The constructor builds `xform` (display node) and `merge`, tagged `object:xform` and `object:merge` and looked up by tag, never by cached id. Loading replaces them. `validate()` and repair run on load.
- Layer is tag `layer` (root only), Group is tag `group` (may nest). Hide is the node's bypass flag; lock is a parameter.
- Restore `curvyFill`, `curvyStroke`, `curvyEffect` from `tmp/`.
- `GraphDocument`: `addLayer`, `addGroup`, `moveChild` (clip, paste, delete, fixing name references), `reorderLayers` (permutes the root merge's inputs), `buildSampleLayers`. One undo step each.
- Sample file: Layer1 (Group1, Group2), Layer2 (Group3), Layer3 (GroupA), no shapes.

## UI

- Graph tree (`docs/mockups/graphTree.html`): layers and groups as rows, structural nodes hidden, drag and drop to reorder layers (Above / Below / Inside zones, reference `tmp/src/layertree.cpp`).
- Full graph view (`docs/mockups/nodes-girl.html`): every node visible and freely editable, with a guard against invalid operations: no deleting or rewiring `object:xform` / `object:merge`, no cross-scope wires, no fan-out of a shape into two objects. Cycles are already refused by `Network::connect`.
- Property pane for the selected node; numeric values get a scrubber (to be confirmed).

## Warp node (agreed 2026-11-09)

The v1 frame warp (drag the four corners of the selection frame for perspective effects) comes back as its own jitnodes node, `warp`, not as part of the transform. It holds a source quad and a destination quad, solves the homography and applies it to points and curve control points; it also records the map so fill and stroke rendering can bend gradients and other paint with the shape. Canvas corner handles edit the destination quad. Open: how curves are re-fitted under strong perspective (check v1 `commitGeometry` in `tmp/src/canvasview.cpp`), and the same recorded map may settle the stroke-scaling question below. Comes after `ObjectNode`.

Kernel temporaries: the wrangle language accepts names with a single leading underscore (`_nx`) as locals that are never channels, used by the transform matrix kernel.

## Risks and open questions

- **Stroke scaling.** Appearance nodes carry the look as parameters, so a group scale moves points but does not scale stroke weight. Options: the transform records its accumulated scale in a geometry detail that stroke rendering uses, or leave stroke weight unscaled for now. Undecided.
- **Pivot** is a stored parameter, not the cooked bounds centre, to avoid a cook dependency.
- **Moving a node between groups** changes its id and scope; `node.param` expressions can break.
- **Group-level appearance** would sit between merge and xform; look chains that see through a node need checking.
- **Performance:** each Object owns a `Network`; check against the sync scaling test.

## Tests (gtest only)

Split changes no output; tag round-trips through save, snapshot, undo, clip and paste; rotated and pivoted transform matches the matrix; an empty group cooks to empty geometry; an object round-trips through save/load and undo/redo; a file with a missing `xform` is repaired on load; layer reorder with undo; the sample file loads; no regression in the scaling test.

## Order of work

1. jitnodes steps 1 to 5 with tests.
2. `ObjectNode` and tests.
3. Document operations and the sample file.
4. Graph tree.
5. Full graph view and property pane.
