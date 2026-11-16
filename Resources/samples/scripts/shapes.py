# A small scene made of the shape calls, then a point-level edit.
# Shows add_rect, add_ellipse, add_polygon, add_star, add_line, add_path, points, move_points and selection.

W = doc.page_width
H = doc.page_height
U = min(W, H) / 100  # sizes scale with the page

sky = doc.add_layer("Sky")
sky.add_rect(0, 0, W, H * 0.65, fill=(0.55, 0.78, 0.95, 1))
sky.add_ellipse(W * 0.78, H * 0.14, 6 * U, 6 * U, fill=(1.0, 0.85, 0.3, 1))
for i in range(5):
    sky.add_star(W * (0.12 + 0.17 * i), H * (0.08 + 0.05 * (i % 2)), 2.2 * U, 0.9 * U, points=5 + i % 2,
                 fill=(1, 1, 1, 0.9))

ground = doc.add_layer("Ground")
ground.add_rect(0, H * 0.65, W, H * 0.35, fill=(0.35, 0.65, 0.3, 1))
for i in range(4):
    x = W * (0.15 + 0.22 * i)
    ground.add_line(x, H * 0.65, x, H * 0.65 + 8 * U, stroke=(0.3, 0.2, 0.1, 1), stroke_width=0.6 * U)

house = doc.add_layer("House")
house.add_rect(W * 0.3, H * 0.5, W * 0.4, H * 0.25, radius=1.5 * U, fill=(0.85, 0.35, 0.3, 1),
               stroke=(0.2, 0.1, 0.1, 1), stroke_width=0.4 * U)
roof = house.add_path([(W * 0.27, H * 0.5), (W * 0.5, H * 0.38), (W * 0.73, H * 0.5)], closed=True,
                      fill=(0.4, 0.2, 0.15, 1), stroke=None)
house.add_polygon(W * 0.5, H * 0.6, 4 * U, 6, fill=(1, 0.95, 0.7, 1), stroke=(0.2, 0.1, 0.1, 1))

# Point-level edit: raise the roof's peak. The call is one undo step of the script.
points = doc.points(roof)
doc.set_point(roof, 1, points[1][0], points[1][1] - 4 * U)
print("roof points:", doc.points(roof), "closed:", doc.is_closed(roof))

if selection is not None:
    selection.set(roof)
