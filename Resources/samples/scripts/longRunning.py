# Long-running script for trying the background run: open a document, open this in the Script Editor and press Run.
#
# While it runs (about 40 seconds) check that:
#   - the window stays responsive (pan and zoom the canvas, resize panels);
#   - the console shows the lines below as they are printed, and "running... N% <message>";
#   - the shapes appear on the canvas as they are made;
#   - Cancel (console), Stop (editor Run button) and Esc each end it with "Script cancelled";
#   - menu commands, Open and Save are refused with "A script is running", and closing the window waits for the script;
#   - after a cancel the shapes made so far stay, and one Undo removes all of them.
import math
import time

if doc is None:
    print("Open or create a document first.")
else:
    steps = 80
    layer = doc.add_layer("Long running")
    cx = doc.page_width / 2
    cy = doc.page_height / 2

    for i in range(steps):
        angle = i * 0.35
        radius = 20 + i * (min(cx, cy) - 40) / steps
        x = cx + math.cos(angle) * radius
        y = cy + math.sin(angle) * radius
        hue = i / steps
        layer.add_circle(x, y, 6 + (i % 5), (1.0 - hue, 0.3 + 0.5 * hue, hue, 1.0))

        curvy.progress((i + 1) / steps, "circle %d of %d" % (i + 1, steps))
        if i % 10 == 0:
            print("step", i)
        time.sleep(0.5)

    print("done:", len(doc.layers), "layers")
