# Makes a python node whose code always fails, to see how an error shows in the Node panel (the first line of the message, under the editor).
# Edit the code in the panel (Commit or Ctrl+Enter) and the error clears. Try a syntax mistake too: the editor underlines it and names the line.
# Run it with a document open (File > New first).

layer = doc.add_layer("Python error")
layer.add_circle(doc.page_width / 2, doc.page_height / 2, 60, (0.7, 0.2, 0.2, 1))

source = doc.add_node("circle", "ring")
broken = doc.add_node("python", "broken")
doc.connect(source, broken, 0)
doc.set_string(broken, "code", """x = geo.point_attrib("x")
raise ValueError("this node always fails")
""")

if selection is not None:
    selection.set(broken)
print("added", broken.name)
