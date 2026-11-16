# A grid of flowers: each flower is a group (a centre and six petals) inside one layer.
# Shows layers, groups and the Objects tree.

import math

COLUMNS = 3
ROWS = 4

layer = doc.add_layer("Flowers")
cell_w = doc.page_width / COLUMNS
cell_h = doc.page_height / ROWS
size = min(cell_w, cell_h) * 0.12

for row in range(ROWS):
    for col in range(COLUMNS):
        flower = layer.add_group("Flower %d,%d" % (col + 1, row + 1))
        cx = cell_w * (col + 0.5)
        cy = cell_h * (row + 0.5)
        hue = (row * COLUMNS + col) / (ROWS * COLUMNS)
        petal = (1.0, 0.45 + 0.4 * hue, 0.6 - 0.3 * hue, 1)
        for k in range(6):
            a = k * math.pi / 3
            flower.add_circle(cx + size * 1.4 * math.cos(a), cy + size * 1.4 * math.sin(a), size, petal)
        flower.add_circle(cx, cy, size * 0.8, (0.95, 0.8, 0.1, 1))

print(len(layer.groups), "flowers")
