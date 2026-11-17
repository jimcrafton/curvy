# Makes a circle node feeding a wrangle node and selects the wrangle, so its Code section shows in the Node panel.
# Run it with a document open (File > New first), then open the Node panel's Code toggle. Undo takes the nodes back.

layer = doc.add_layer("Wrangle")
layer.add_circle(doc.page_width / 2, doc.page_height / 2, 60, (0.25, 0.5, 0.9, 1))

source = doc.add_node("circle", "ring")
wrangle = doc.add_node("wrangle", "bend")
doc.connect(source, wrangle, 0)

doc.set_string(wrangle, "code", """| gain = 20(0, 100), steps = 3(1,) |
# lift every point by a sine of its x
y = y + sin(x * steps * 0.05) * gain;
""")

if selection is not None:
    selection.set(wrangle)
print("added", source.name, "->", wrangle.name)
