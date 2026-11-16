# Shows `with doc.transaction()`: a block that either completes or leaves no trace.
# The first block works; the second fails half way and is taken back, but the script carries on.

layer = doc.add_layer("Transaction demo")

with doc.transaction("Good block"):
    layer.add_circle(150, 200, 60, (0.2, 0.7, 0.3, 1))
    layer.add_circle(300, 200, 60, (0.2, 0.7, 0.3, 1))

try:
    with doc.transaction("Bad block"):
        layer.add_circle(150, 400, 60, (0.9, 0.2, 0.2, 1))
        raise ValueError("something went wrong")
except ValueError as error:
    print("rolled back:", error)

print("circles drawn: expected 2 (the bad block left nothing)")
