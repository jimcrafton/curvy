r"""GUI check of selecting shapes by clicking them on the canvas (TC-06, step S1), with real mouse and keyboard.

    python D:\code\curvy\tests\gui\test_curvy_canvasselect.py --exe D:\code\curvy\out\build\x64-release\curvy.exe

The scene is drawn from the console panel; click points are derived from the drawn document coordinates and the canvas rectangle
(the page is centred at the fit zoom until the first pan), never hard-coded pixels. What is selected is read back with a helper
`S()` defined in the console that prints `SEL <count> <node name> <shape tag>`.
"""
import os
import sys
import unittest

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from newui_guitest import _win32 as w
from curvyCase import CurvyCase

VK_SHIFT = 0x10
VK_CONTROL = 0x11
VK_SPACE = 0x20

HELPERS = ("exec(\"def P(n):\\n try:\\n  return doc.points(n)[0]\\n except Exception:\\n  return None\")")
SELECTION_HELPER = ("exec(\"def S():\\n n = len(selection)\\n p = selection.primary if n else None\\n"
                    " tag = [k for k, v in shapes.items() if p is not None and P(p) is not None and P(p) == P(v)]\\n"
                    " nm = (p.name() if callable(p.name) else p.name) if n else '-'\\n"
                    " print('SEL', n, nm, tag[0] if tag else '-')\")")


class CanvasSelectBase(CurvyCase):
    def setUp(self):
        self.make_settings_file(self.SCRIPT_PANELS_SETTINGS)
        super().setUp()
        self.pan = (0.0, 0.0)

    # ---- console -------------------------------------------------------------------------------------------------

    def run_line(self, text):
        self.app.click("pyConsoleInput")
        self.app.type(text)
        self.app.key("enter")
        self.wait_script_done()

    def transcript(self):
        return self.app.text_of("pyConsoleOutput")

    def selection(self):
        """(count, node name, shape tag) of the editor selection, read through the console."""
        self.run_line("S()")
        lines = self.transcript().splitlines()
        echoes = [i for i, l in enumerate(lines) if l.strip() == ">>> S()"]
        self.assertTrue(echoes and echoes[-1] + 1 < len(lines) and lines[echoes[-1] + 1].startswith("SEL "), self.transcript())
        parts = lines[echoes[-1] + 1].split()
        return int(parts[1]), parts[2], parts[3]

    def scene(self):
        self.new_a4()
        if self.app.find("rightArea").w <= 0:
            self.toggle_properties()
        self.app.wait_until(lambda: self.app.find("rightArea").w > 0, "the right dock")
        self.app.wait_idle()
        self.run_line(HELPERS)
        self.run_line("layer = doc.add_layer('L')")
        self.run_line("e1 = layer.add_ellipse(150, 150, 40, 40, fill=(1, 0, 0, 1))")
        self.run_line("e2 = layer.add_ellipse(300, 150, 40, 40, fill=(0, 0, 1, 1))")
        self.run_line("r = layer.add_rect(100, 300, 150, 80, fill=(0, 1, 0, 1))")
        self.run_line("ln = layer.add_line(100, 500, 400, 500, stroke=(0, 0, 0, 1), stroke_width=4)")
        self.run_line("grp = layer.add_group('G')")
        self.run_line("inner = grp.add_ellipse(450, 350, 30, 30, fill=(1, 1, 0, 1))")
        self.run_line("shapes = {'e1': e1, 'e2': e2, 'r': r, 'ln': ln, 'inner': inner}")
        self.run_line(SELECTION_HELPER)
        self.run_line("selection.clear()")
        self.run_line("print('PAGE', doc.page_width, doc.page_height)")
        page = [l for l in self.transcript().splitlines() if l.startswith("PAGE ")][-1].split()
        self.page = (float(page[1]), float(page[2]))

    # ---- document to screen --------------------------------------------------------------------------------------

    def screen(self, x, y):
        canvas = self.app.find("canvas")
        pw, ph = self.page
        zoom = min((canvas.w - 80.0) / pw, (canvas.h - 80.0) / ph, 1.0)
        w_, h_ = pw * zoom, ph * zoom
        pan_x = (canvas.w - w_) * 0.5 + self.pan[0]
        pan_y = ((canvas.h - h_) * 0.5 if h_ < canvas.h - 40.0 else 20.0) + self.pan[1]
        return (int(canvas.x + pan_x + x * zoom), int(canvas.y + pan_y + y * zoom))

    def empty_point(self):
        return self.screen(self.page[0] - 20, 20)

    def pasteboard_point(self):
        """The grey workspace around the page: Space there opens the Document ring (over the page it is the Create ring)."""
        canvas = self.app.find("canvas")
        return (canvas.x + 40, canvas.y + canvas.h - 40)

    # ---- input with a key or modifier held -----------------------------------------------------------------------

    def click_holding(self, point, vk):
        app = self.app
        app.bring_to_front()
        app._require_ours_at(point)
        app._move_to(point)
        app._send([app._key(vk), app._mouse(w.MOUSEEVENTF_LEFTDOWN), app._mouse(w.MOUSEEVENTF_LEFTUP), app._key(vk, up=True)])
        app.wait_idle()

    def drag_holding(self, a, b, vk):
        app = self.app
        app.bring_to_front()
        app._send([app._key(vk)])
        try:
            app.drag(a, b)
        finally:
            app._send([app._key(vk, up=True)])
        app.wait_idle()
        if self.ring_open():
            app.key("escape")

    def drag_right(self, a, b, steps=10):
        """Right button down at a, moves, up at b (the driver only drags with the left one)."""
        app = self.app
        app.bring_to_front()
        app._require_ours_at(a)
        app._move_to(a)
        app._send([app._mouse(w.MOUSEEVENTF_RIGHTDOWN)])
        try:
            for i in range(1, steps + 1):
                app._move_to((a[0] + (b[0] - a[0]) * i // steps, a[1] + (b[1] - a[1]) * i // steps))
                app.wait_idle()
        finally:
            app._send([app._mouse(w.MOUSEEVENTF_RIGHTUP)])
        app.wait_idle()

    def click_shape(self, x, y):
        self.app.click_at(self.screen(x, y))

    def expect(self, count, tag, what):
        got = self.selection()
        self.assertEqual((count, tag), (got[0], got[2]), what)


class CanvasSelect(CanvasSelectBase):
    # ---- tests ---------------------------------------------------------------------------------------------------

    def test_a_click_selects_the_shape_and_the_next_click_replaces_it(self):
        self.scene()
        self.click_shape(150, 150)
        self.expect(1, "e1", "the first circle")
        self.app.wait_until(lambda: self.hint_keys() != "", "the hint bar to name the node")
        self.click_shape(300, 150)
        self.expect(1, "e2", "the second circle replaces the first")
        self.click_shape(175, 340)
        self.expect(1, "r", "the rectangle")

    def test_shift_and_ctrl_click_add_to_the_selection(self):
        self.scene()
        self.click_shape(150, 150)
        self.click_holding(self.screen(300, 150), VK_SHIFT)
        self.assertEqual(2, self.selection()[0], "shift adds the second circle")
        self.click_holding(self.screen(175, 340), VK_CONTROL)
        self.assertEqual(3, self.selection()[0], "ctrl adds the rectangle")

    def test_a_click_on_empty_canvas_clears_the_selection(self):
        self.scene()
        self.click_shape(150, 150)
        self.assertEqual(1, self.selection()[0])
        self.app.click_at(self.empty_point())
        self.assertEqual(0, self.selection()[0], "the page corner has no shape")
        self.click_shape(150, 150)
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + 40, canvas.y + canvas.h - 40))
        self.assertEqual(0, self.selection()[0], "the pasteboard has none either")

    def test_clicking_the_selected_shape_again_steps_up_and_then_starts_over(self):
        self.scene()
        self.click_shape(150, 150)
        self.expect(1, "e1", "the shape")
        seen = []
        for _ in range(4):
            self.click_shape(150, 150)
            seen.append(self.selection())
        self.assertNotEqual("e1", seen[0][2], "the second click leaves the shape's own node: %s" % seen)
        self.assertEqual("e1", seen[-1][2], "the steps come round to the shape again: %s" % seen)

    def test_an_open_stroked_line_is_hit_on_the_line_and_not_in_the_gap_beside_it(self):
        self.scene()
        self.click_shape(250, 500)
        self.expect(1, "ln", "on the line")
        self.app.click_at(self.empty_point())
        self.click_shape(250, 530)
        self.assertEqual(0, self.selection()[0], "30 units below the line is empty")

    def test_space_drag_pans_and_leaves_the_selection(self):
        self.scene()
        self.click_shape(150, 150)
        start = self.screen(400, 700)
        self.drag_holding(start, (start[0], start[1] - 40), VK_SPACE)
        self.pan = (0.0, -40.0)
        self.expect(1, "e1", "a pan does not touch the selection")
        self.click_shape(300, 150)
        self.expect(1, "e2", "the page moved: the second circle is where the pan put it")

    @unittest.expectedFailure                                      # only the graph view enters on a double click; the canvas has no handler yet
    def test_a_double_click_on_a_grouped_shape_enters_the_group(self):
        self.scene()
        self.click_shape(450, 350)
        first = self.selection()
        self.assertEqual(1, first[0], "the grouped shape is selectable: %s" % (first,))
        self.app.double_click(self.screen(450, 350))
        after = self.selection()
        self.assertEqual(1, after[0], "after entering the group the shape is selected: %s" % (after,))
        self.assertEqual("inner", after[2], after)

    def test_a_primary_button_drag_selects_what_the_rectangle_contains(self):
        self.scene()
        self.app.drag(self.screen(90, 80), self.screen(360, 200))
        self.assertEqual(2, self.selection()[0], "both circles are fully inside")
        self.app.drag(self.screen(90, 80), self.screen(150, 200))
        self.assertEqual(0, self.selection()[0], "a rectangle that only cuts the first circle takes nothing")

    def test_a_secondary_button_drag_selects_what_the_rectangle_touches(self):
        self.scene()
        self.drag_right(self.screen(90, 80), self.screen(150, 200))
        self.assertEqual(1, self.selection()[0], "the rectangle cuts the first circle")
        self.assertFalse(self.ring_open(), "a drag is a marquee, not a ring")

    def test_a_secondary_click_without_a_drag_opens_the_ring(self):
        self.scene()
        self.app.click_at(self.empty_point(), button="right")
        self.app.wait_until(self.ring_open, "the ring to open on release")
        self.app.key("escape")

    def test_the_m_key_cycles_the_region_and_a_polygon_closes_with_enter(self):
        self.scene()
        self.app.click_at(self.empty_point())
        for _ in range(3):
            self.app.key("m")
        for corner in ((90, 80), (230, 80), (230, 200), (90, 200)):
            self.app.click_at(self.screen(*corner))
        self.app.key("enter")
        self.assertEqual(1, self.selection()[0], "only the first circle is inside the polygon")

    def test_ctrl_a_selects_everything_and_escape_clears_it(self):
        self.scene()
        self.app.click_at(self.empty_point())
        self.app.key("ctrl+a")
        self.assertEqual(5, self.selection()[0], "all five shapes of the scene")
        self.keep_selection_and_focus_canvas()
        self.app.key("escape")
        self.assertEqual(0, self.selection()[0])

    def keep_selection_and_focus_canvas(self):
        """Reading the selection goes through the console, which takes the keyboard focus; a Shift-click on empty canvas gives it back and
        leaves the selection alone."""
        self.click_holding(self.empty_point(), VK_SHIFT)

    def test_tab_steps_through_the_shapes_while_one_is_selected(self):
        self.scene()
        self.click_shape(150, 140)
        self.expect(1, "e1", "the first circle")
        self.keep_selection_and_focus_canvas()
        self.app.key("tab")
        self.expect(1, "e2", "the next shape in drawing order")
        self.keep_selection_and_focus_canvas()
        self.app.key("shift+tab")
        self.expect(1, "e1", "and back")

    def test_a_selected_shape_is_drawn_with_the_accent_outline(self):
        self.scene()
        x, y = self.screen(150, 150)
        box = (x - 60, y - 60, x + 60, y + 60)
        before = self.app.screenshot()
        self.click_shape(150, 150)
        self.app.wait_idle()
        after = self.app.screenshot()
        self.assertTrue(before.changed(after, *box), "the outline puts pixels around the circle")
        self.app.click_at(self.empty_point())
        cleared = self.app.screenshot()
        self.assertFalse(before.changed(cleared, *box), "clearing takes them away again")


if __name__ == "__main__":
    newui_guitest.main()
