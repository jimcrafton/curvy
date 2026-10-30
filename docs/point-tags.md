# Point tags

A tag is a named set of an object's points. Tagging lets one node act on some of a shape's points instead of all of them: today that is the Move Points node.
This page covers how tags are stored, how to make them, and how they behave when the shape changes. The research and history are in `docs/design.md` ("Point ids and tags").

## Model

- Every point of a shape node's curve has an int attribute `id` (`kPointIdAttrib`). A new shape numbers its points 0..n-1. The id travels with the point through the nodes and into
  saved files. When an outline is edited, points still where they were keep their id (matched in order, 64-point lookahead) and new points get ids above all the old ones. This is
  why a tag survives adding or deleting other points.
- A tag is a **Tag node** (`curvyTag`) in the object's chain: shape, then tags, then the placement (transform) node. It passes its input through unchanged and adds a point
  group, the int attribute `group.<name>` (1 for members). Nodes later in the chain read the group by name.
- A tag between a shape and its transform does not change what the object is: `shapeBehind` looks through it, and the object still finds its placement node.
- Tags are stored in the graph, so they are saved with the document, undone with it, and shown in the graph dock as "Tag" rows.

## Making tags

**On the canvas (direct-select tool).** The document has one *active tag*, "tag1" to start with (`SketchDocument::activeTag`).

- Ctrl-click an anchor to tag it, or to take it out of the tag if it is already in.
- Tagged anchors are drawn with an orange ring.
- Object > Points has Tag Picked Point, Untag Picked Point, Clear Tag (removes the tag node) and Move Tagged Points.

Each of these is one undo step. They edit the tag node's `ids` (`tagPoints`, `clearTag`); the node is created on first use and removed by Clear Tag.
There is not yet a way to rename or switch the active tag, so the canvas always edits "tag1".

**In the graph dock.** Select a Tag node to edit its parameters.

| Parameter | Meaning |
|---|---|
| `group` | The name of the point group the node writes (`group.<name>`). Empty passes the input through. |
| `ids` | Points by id: `3,7,10-14`. |
| `every`, `offset` | Every nth anchor, starting at `offset`. |
| `box` | `x0 y0 x1 y1` in the shape's own space. |
| `kind` | Narrows the picked points: `any`, `anchors`, `handles`, `corners` or `smooth`. |
| `operation` | How to combine with a group of the same name already on the input: `replace`, `union`, `subtract`, `intersect`. |

`ids`, `every` and `box` are selectors: a point matching any of them is picked. A cubic's handles follow their anchor (except for `anchors` and `corners`, which pick anchors only,
and `handles`, which picks handles only). `every` and `offset` can be animated like other float parameters.

Chaining several Tag nodes, with `operation` set to union, subtract or intersect, builds a group from simpler ones.

## Using tags: Move Points

Move Tagged Points adds a **Move Points** node (`curvyMovePoints`, `addMovePoints`) just before the object's placement node. It has the same translate, rotate, scale, shear and pivot
parameters as a transform, in the shape's own space, but applies them only to the points of its `group` (all points when `group` is empty). The pivot starts at the middle of the
tagged points, so rotating and scaling hold still about them. Its `group` can be changed afterwards; a group nobody is in moves nothing.

It is a node of its own, not a parameter of the placement transform, because the placement is one rigid matrix shared by the object, its paints and its children.

An object with a Move Points node in its chain is **driven**: its outline is what the chain cooks. Moving or rotating it changes only the placement matrix, the shape node is never
written from it, and drawing on a driven object snaps back. Objects whose placement or deformation has an expression or keys are driven too.

## Scopes

A tag and its Move Points node live in the same scope (root, or a group's subnet) as the object's shape and placement nodes. `tagNodeOf` and `addMovePoints` return a `NodeRef`, so
they work for objects inside groups as well as at the root.

## Not yet

A way to name or switch tags in the UI, tagging several points at once (marquee), a group parameter on nodes other than Move Points, ids kept through a move-and-insert in a
single edit, and tags that follow ids through join, reverse and simplify.

## Code

`src/pathnode.cpp` (`TagNode`, `MovePointsNode`), `src/documentlive.cpp` (tag and move-points editing on `SketchDocument`), `src/canvastools.cpp` (Ctrl-click),
`src/graphdock.cpp` (the node boxes). Tests: `PointTags.*`, `PathNode.*`.
