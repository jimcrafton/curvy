# Finds shapes by point, rectangle and property, and selects them. Undo takes the drawing back in one step.
# With an editor open the selection shows in the canvas, graph view and panels; without one the selection part is skipped.

layer = doc.add_layer("Selectable")
red = (0.80, 0.20, 0.20, 1)
blue = (0.20, 0.40, 0.80, 1)

step = doc.page_width / 5
for i in range(4):
    layer.add_rect(step * (i + 0.5), 100, step * 0.6, 100, fill=red if i % 2 == 0 else blue)

top = doc.shapes_in_rect(0, 0, doc.page_width, 300)
print(len(top), "shapes wholly inside the top band")

under = doc.shape_at(step * 0.8, 150)
print("under the first rectangle:", under.name() if under is not None else None)

reds = doc.shapes_where(fill="#CC3333")
print(len(reds), "red shapes")

if selection is not None:
    doc.select_where(fill="#CC3333")
    doc.select_where("add", fill="#3366CC")
    print(len(selection), "selected")
