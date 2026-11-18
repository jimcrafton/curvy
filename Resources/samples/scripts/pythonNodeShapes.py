# Makes a python node with no input that builds a closed star: points, one primitive with a shape key, and a string attribute.
# Parameter a is the number of points (10). Select the node, open the Node panel's Data button to see the points and the "label" column,
# and change a (or the radii in the code) to see it recook.
# Run it with a document open (File > New first). The first cook asks whether the code may run, as pythonNodeBasics.py does.

layer = doc.add_layer("Python shapes")
layer.add_circle(doc.page_width / 2, doc.page_height / 2, 40, (0.9, 0.3, 0.5, 1))

star = doc.add_node("python", "star")
doc.set_param(star, "a", 10)

doc.set_string(star, "code", """import math
import jitnodes

P = jitnodes.AttribClass.point
count = max(int(params["a"]), 3)

geo.resize_points(count)
x = geo.point_attrib("x")
y = geo.point_attrib("y")
for i in range(count):
    angle = 2 * math.pi * i / count
    radius = 80 if i % 2 == 0 else 35
    x[i] = 150 + radius * math.cos(angle)
    y[i] = 150 + radius * math.sin(angle)
del x, y      # an array must be released before the point count changes again

geo.add_primitive(0, count, closed=True, key="star")
geo.add_attrib(P, "label", jitnodes.AttribType.string)
geo.set_string_values(P, "label", ["tip" if i % 2 == 0 else "dent" for i in range(count)])
geo.set_detail("points", count)
""")

if selection is not None:
    selection.set(star)
print("added", star.name)
