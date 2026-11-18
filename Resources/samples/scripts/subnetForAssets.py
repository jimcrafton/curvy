# Makes a small subnet (an input and a transform inside it) in the document's graph and selects it.
# Used by docs/asset-manager-manual-checks.md: with this subnet selected, + in the Assets panel makes an asset from it.
# Run it with a document open (File > New first). Undo takes everything back.

layer = doc.add_layer("Assets")
layer.add_circle(doc.page_width / 2, doc.page_height / 2, 60, (0.9, 0.5, 0.2, 1))

source = doc.add_node("circle", "ring")
box = doc.add_node("subnet", "box")
doc.connect(source, box, 0)

inside = doc.add_node("subnetInput", "in", parent=box)
move = doc.add_node("transform", "move", parent=box)
doc.connect(inside, move, 0)

if selection is not None:
    selection.set(box)
print("added the subnet", box.name, "(", inside.name, "->", move.name, "inside)")
