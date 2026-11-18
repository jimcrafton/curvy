# Makes a python node that never ends, to try the time limit and Esc. Selecting it runs it: the window freezes until the limit (10 seconds
# unless scriptEditor.pythonNodeSeconds in the settings file says otherwise) or until you press Esc. The node then shows "Python node stopped"
# and does not run again until its code, a parameter or an input changes.
# Run it with a document open (File > New first). Undo takes the node back.

layer = doc.add_layer("Python runaway")
layer.add_circle(doc.page_width / 2, doc.page_height / 2, 60, (0.4, 0.4, 0.4, 1))

source = doc.add_node("circle", "ring")
runaway = doc.add_node("python", "runaway")
doc.connect(source, runaway, 0)
doc.set_string(runaway, "code", """total = 0
while True:
    total += 1
""")

if selection is not None:
    selection.set(runaway)
print("added", runaway.name, "- selecting it starts the loop")
