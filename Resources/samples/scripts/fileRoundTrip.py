# Saves the document, changes it, and opens the saved copy again.
#
# doc.save(path) writes a .curvy file (Save As: the document belongs to that file afterwards); doc.save() rewrites doc.path.
# doc.open(path) replaces the whole document with a file. It refuses while the document has unsaved changes unless you pass
# discard_changes=True, and the layers and nodes you held before the open are gone.
import os
import tempfile

if doc is None:
    print("Open or create a document first.")
else:
    folder = tempfile.mkdtemp(prefix="curvy_")
    path = os.path.join(folder, "roundTrip.curvy").replace("\\", "/")

    layer = doc.add_layer("Before the save")
    layer.add_circle(100, 100, 40, (0.2, 0.5, 0.9, 1))
    print("saved:", doc.save(path), "modified:", doc.is_modified)

    doc.add_layer("After the save")
    print("layers now:", [l.name for l in doc.layers], "modified:", doc.is_modified)

    doc.open(path, discard_changes=True)
    print("layers after open:", [l.name for l in doc.layers], "modified:", doc.is_modified)
    print("path:", doc.path)

    # An open starts the undo history again, so what a script does after it is a new step of the opened document.
    doc.add_layer("Added after the open")
