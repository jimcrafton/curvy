# Tries the editing calls one by one and prints what each did, so you can check them in the console.
# Covers point editing, whole-object transforms, single-shape transforms and the selection.
# Run it on an empty document; afterwards press Ctrl+Z once to take everything back.

W = doc.page_width
H = doc.page_height
U = min(W, H) / 100

layer = doc.add_layer("Editing")

# 1. Point editing on a polygon (straight points) -------------------------------------------------
tri = layer.add_polygon(W * 0.2, H * 0.25, 8 * U, 3, fill=(0.9, 0.4, 0.3, 1))
print("triangle points:", doc.points(tri))
doc.insert_point(tri, 1, W * 0.2 + 9 * U, H * 0.25 - 2 * U)   # a new corner
doc.move_points(tri, 0, -2 * U, indices=[0])                   # pull the first point up
print("after insert and move:", len(doc.points(tri)), "points, closed:", doc.is_closed(tri))

# 2. Point editing on a curve: handles are points too ---------------------------------------------
blob = layer.add_ellipse(W * 0.5, H * 0.25, 9 * U, 6 * U, fill=(0.3, 0.6, 0.9, 1))
pts = doc.points(blob)
print("ellipse is a curve:", doc.is_curve(blob), "with", len(pts), "control points")
doc.set_point(blob, 0, pts[0][0] + 5 * U, pts[0][1])           # drag its right anchor outwards
try:
    doc.insert_point(blob, 0, 0, 0)
except RuntimeError as error:
    print("insert on a curve is refused:", error)

# 3. Open a shape, then close it again (open leaves out the side from the last point to the first) --
star = layer.add_star(W * 0.8, H * 0.25, 8 * U, 3.5 * U, points=5, fill=None, stroke=(0.1, 0.1, 0.1, 1),
                      stroke_width=0.5 * U)
doc.set_closed(star, False)
print("star closed:", doc.is_closed(star), "(look at the canvas now: one side is missing)")
doc.set_closed(star, True)
print("star closed again:", doc.is_closed(star), "with", len(doc.points(star)), "points")

# 4. Whole-object transforms: a group of two shapes moves, turns and scales as one ----------------
group = layer.add_group("Pair")
group.add_rect(W * 0.15, H * 0.55, 10 * U, 6 * U, fill=(0.4, 0.75, 0.4, 1))
group.add_ellipse(W * 0.15 + 12 * U, H * 0.55 + 3 * U, 4 * U, 4 * U, fill=(0.95, 0.8, 0.2, 1))
group.translate(W * 0.05, 0)
group.rotate(15, W * 0.2, H * 0.58)
group.scale(1.3)
print("group transform:", group.transform)

# 5. Single-shape transforms: one rectangle is turned about its own centre ------------------------
card = layer.add_rect(W * 0.5, H * 0.55, 14 * U, 9 * U, radius=1.5 * U, fill=(0.7, 0.5, 0.8, 1))
doc.rotate_shape(card, 20, W * 0.5 + 7 * U, H * 0.55 + 4.5 * U)
twin = layer.add_rect(W * 0.5, H * 0.75, 14 * U, 9 * U, fill=(0.5, 0.5, 0.5, 1))
doc.set_shape_transform(twin, tx=4 * U, sx=0.6)
print("card:", doc.shape_transform(card)["rot"], "twin:", doc.shape_transform(twin)["sx"])
doc.reset_shape_transform(twin)

# 6. Fills and strokes of shapes that already exist -------------------------------------------------
print("card fill/stroke before:", doc.fill(card), doc.stroke(card))
doc.set_stroke(card, (0.2, 0.1, 0.4, 1), width=0.6 * U)        # makes the stroke node, the card had none
doc.set_fill(blob, doc.fill(blob), opacity=0.5)                 # same colour, half see-through
doc.set_fill(tri, None)                                         # hides the triangle's fill ...
doc.set_stroke(tri, (0.6, 0.1, 0.05, 1), width=0.8 * U)         # ... and gives it an outline instead
print("card fill/stroke after:", doc.fill(card), doc.stroke(card), doc.stroke_width(card))
print("triangle fill:", doc.fill(tri), "stroke:", doc.stroke(tri))

# 7. Selection: it is the editor's, so you will see it on the canvas -------------------------------
if selection is None:
    print("no editor selection in this run")
else:
    print("selected before:", len(selection))
    selection.replace([tri, blob, card])
    print("now selected:", len(selection), "primary is", selection.primary)
    print("object of the primary:", selection.object.name, "| fill node:", selection.fill)
    selection.remove(blob)
    selection.toggle(star)
    print("after remove and toggle:", len(selection), "selected")
    selection.set(group)
    print("stepping up from the group:", selection.step_up(), "->", selection.primary)
