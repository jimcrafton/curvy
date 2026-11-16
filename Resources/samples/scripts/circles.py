# A row of coloured circles on a new layer.
# Run it with a document open (File > New first). Undo takes the whole thing back in one step.

layer = doc.add_layer("Circles")
colors = [
    (0.90, 0.20, 0.20, 1),
    (0.95, 0.60, 0.15, 1),
    (0.95, 0.85, 0.20, 1),
    (0.30, 0.70, 0.35, 1),
    (0.25, 0.50, 0.90, 1),
    (0.55, 0.35, 0.80, 1),
]

width = doc.page_width
step = width / (len(colors) + 1)
for i, color in enumerate(colors):
    layer.add_circle(step * (i + 1), doc.page_height / 2, step * 0.4, color)

print("added", len(colors), "circles to", layer.name)
