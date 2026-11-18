# Makes a circle node feeding a python node and selects the python node, so its Code section shows in the Node panel.
# The python node stretches its input with NumPy: parameter a scales x, b scales y (here 1.5 and 0.75).
# Run it with a document open (File > New first), then see docs/python-node-manual-checks.md.
# The first time the node cooks, curvy asks whether the code may run: it was written by a script, not typed in the panel.

layer = doc.add_layer("Python node")
layer.add_circle(doc.page_width / 2, doc.page_height / 2, 60, (0.25, 0.5, 0.9, 1))

source = doc.add_node("circle", "ring")
stretch = doc.add_node("python", "stretch")
doc.connect(source, stretch, 0)
doc.set_param(stretch, "a", 1.5)
doc.set_param(stretch, "b", 0.75)

doc.set_string(stretch, "code", """# geo is a copy of the input; params holds this node's number parameters (a, b, c, d and any you add)
x = geo.point_attrib("x")
y = geo.point_attrib("y")
x *= params["a"]
y *= params["b"]
""")

if selection is not None:
    selection.set(stretch)
print("added", source.name, "->", stretch.name)
