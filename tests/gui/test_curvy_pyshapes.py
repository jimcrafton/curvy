r"""GUI check of the shape, point-editing, transform, fill/stroke and selection calls of the curvy Python module, typed into the console panel.

    python D:\code\curvy\tests\gui\test_curvy_pyshapes.py --exe D:\code\curvy\out\build\x64-release\curvy.exe

Every line is a console run, so each is one undo step of the document (the console is one scope: names persist between lines).
"""
import os
import sys

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from curvyCase import CurvyCase


class PyShapes(CurvyCase):
    def setUp(self):
        self.make_settings_file(self.SCRIPT_PANELS_SETTINGS)
        super().setUp()

    def open_console(self):
        self.new_a4()
        if self.app.find("rightArea").w <= 0:
            self.toggle_properties()
        self.app.wait_until(lambda: self.app.find("rightArea").w > 0, "the right dock")
        self.app.wait_idle()

    def run_line(self, text):
        self.app.click("pyConsoleInput")
        self.app.type(text)
        self.app.key("enter")
        self.app.wait_idle()

    def transcript(self):
        return self.app.text_of("pyConsoleOutput")

    def shapes_scene(self):
        self.open_console()
        self.run_line("layer = doc.add_layer('L')")
        self.run_line("r = layer.add_rect(10, 10, 50, 30, fill=(1, 0, 0, 1))")
        self.run_line("e = layer.add_ellipse(120, 60, 20, 10)")

    def test_shapes_are_made_and_their_points_can_be_edited(self):
        self.shapes_scene()
        self.run_line("print(len(doc.points(r)), doc.is_closed(r), len(doc.points(e)), doc.is_curve(e))")
        self.assertIn("4 True 12 True", self.transcript())

        self.run_line("doc.set_point(r, 0, 5, 5)")
        self.run_line("print(doc.points(r)[0])")
        self.assertIn("(5.0, 5.0)", self.transcript())

        self.run_line("doc.insert_point(r, 1, 30, 0)")
        self.run_line("print(len(doc.points(r)))")
        self.assertIn("\n5\n", self.transcript() + "\n")

        self.run_line("doc.undo()")                                # takes the insert back: one console line is one step
        self.run_line("print(len(doc.points(r)))")
        self.assertTrue(self.transcript().rstrip().endswith("4"), self.transcript())

        self.run_line("doc.insert_point(e, 0, 0, 0)")
        self.assertIn("straight paths", self.transcript())         # a curve refuses it and the panel keeps working
        self.run_line("print(doc.artwork() is not None)")
        self.assertTrue(self.transcript().rstrip().endswith("True"), self.transcript())

    def test_transforms_of_an_object_and_of_a_single_shape(self):
        self.shapes_scene()
        self.run_line("layer.translate(5, 7)")
        self.run_line("layer.rotate(30)")
        self.run_line("print(layer.transform['tx'], layer.transform['ty'], layer.transform['rot'])")
        self.assertIn("5.0 7.0 30.0", self.transcript())

        self.run_line("doc.rotate_shape(r, 20, 35, 25)")
        self.run_line("doc.scale_shape(r, 2)")
        self.run_line("t = doc.shape_transform(r); print(t['rot'], t['sx'], t['px'], doc.shape_transform(e)['rot'])")
        self.assertIn("20.0 2.0 35.0 0.0", self.transcript())

        self.run_line("doc.reset_shape_transform(r)")
        self.run_line("layer.reset_transform()")
        self.run_line("print(doc.shape_transform(r)['rot'], layer.transform['rot'])")
        self.assertTrue(self.transcript().rstrip().endswith("0.0 0.0"), self.transcript())

    def test_fills_and_strokes_of_existing_shapes(self):
        self.shapes_scene()
        self.run_line("print(doc.fill(r), doc.stroke(r), doc.fill(e))")
        self.assertIn("(1.0, 0.0, 0.0, 1.0) None (0.0, 0.0, 0.0, 1.0)", self.transcript())

        self.run_line("doc.set_stroke(r, (0, 0, 1, 1), width=3)")
        self.run_line("doc.set_fill(e, None)")
        self.run_line("print(doc.stroke(r), doc.stroke_width(r), doc.fill(e))")
        self.assertIn("(0.0, 0.0, 1.0, 1.0) 3.0 None", self.transcript())

        self.run_line("doc.undo()")                                # the fill of the ellipse comes back
        self.run_line("print(doc.fill(e))")
        self.assertTrue(self.transcript().rstrip().endswith("(0.0, 0.0, 0.0, 1.0)"), self.transcript())

    def test_a_script_selection_shows_in_the_editor_and_a_click_on_the_pasteboard_clears_it(self):
        self.shapes_scene()
        self.run_line("print(len(selection))")
        self.assertTrue(self.transcript().rstrip().endswith("0"), self.transcript())

        self.run_line("selection.set(r)")
        self.run_line("print(len(selection), selection.primary.name)")
        self.assertIn("1 fill", self.transcript())
        self.app.wait_until(lambda: "curvyFill" in self.app.prop("hintInfo", "text"), "the hint bar to name the selected node")

        self.run_line("selection.replace([r, e]); print(len(selection))")
        self.assertTrue(self.transcript().rstrip().endswith("2"), self.transcript())

        canvas = self.app.find("canvas")                           # the pasteboard corner: nothing there to pick
        self.app.click_at((canvas.x + 40, canvas.y + canvas.h - 40))
        self.app.wait_idle()
        self.run_line("print(len(selection))")
        self.assertTrue(self.transcript().rstrip().endswith("0"), self.transcript())


if __name__ == "__main__":
    newui_guitest.main()
