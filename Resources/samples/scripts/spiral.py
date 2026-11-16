# A spiral of shrinking circles that shifts from blue to orange.
# Change TURNS and DOTS to taste.

import math

TURNS = 4
DOTS = 120

layer = doc.add_layer("Spiral")
cx = doc.page_width / 2
cy = doc.page_height / 2
reach = min(doc.page_width, doc.page_height) * 0.42

for i in range(DOTS):
    t = i / (DOTS - 1)
    angle = t * TURNS * 2 * math.pi
    radius = reach * t
    size = 2 + 12 * (1 - t)
    color = (0.15 + 0.8 * t, 0.35 + 0.25 * t, 0.9 - 0.8 * t, 1)
    layer.add_circle(cx + radius * math.cos(angle), cy + radius * math.sin(angle), size, color)

print("spiral of", DOTS, "dots")
