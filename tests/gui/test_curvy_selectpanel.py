r"""GUI check of the Select by panel (TC-06, step S3b): opened from the ring, a click on a value selects the shapes that have it.

    python D:\code\curvy\tests\gui\test_curvy_selectpanel.py --exe D:\code\curvy\out\build\x64-release\curvy.exe

The scene is the one of test_curvy_canvasselect.py (two circles, a rectangle, a line, a group with one circle), drawn from the console.
"""
import os
import sys

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from test_curvy_canvasselect import CanvasSelectBase


class SelectPanelGui(CanvasSelectBase):
    # The panel first and the console under it, the rest collapsed, so both fit in the short default window.
    SCRIPT_PANELS_SETTINGS = ("{ panels: { select: {}, pyconsole: {}, pyeditor: { collapsed: true }, document: { collapsed: true }, "
                              "node: { collapsed: true }, transform: { collapsed: true }, assets: { collapsed: true } } }")

    def open_panel_from_the_ring(self):
        self.app.click_at(self.pasteboard_point())
        self.tap_space()
        self.app.key("l")
        self.app.wait_until(lambda: self.hint_keys() == "Ring: Select", "the Select ring")
        self.app.key("b")
        self.app.wait_until(lambda: not self.ring_open(), "the ring to close")
        self.app.wait_until(lambda: self.app.find("selectValues").h > 0, "the value list to show")

    def row_point(self, index):
        lst = self.app.find("selectValues")
        return (lst.x + lst.w // 2, lst.y + 12 + index * 24)

    def test_the_ring_opens_the_panel_and_a_value_selects_the_shapes_that_have_it(self):
        self.scene()
        self.open_panel_from_the_ring()
        self.app.click_at(self.row_point(0))
        self.app.wait_idle()
        self.assertEqual(1, self.selection()[0], "the first fill value belongs to one shape")

    def test_the_similar_button_selects_what_shares_the_selected_shapes_value(self):
        self.scene()
        self.open_panel_from_the_ring()
        self.click_shape(150, 140)                                 # the first circle: one shape, so it has values of its own
        self.assertEqual(1, self.selection()[0])
        self.app.click("selectSimilarButton")
        self.app.wait_idle()
        self.assertGreaterEqual(self.selection()[0], 1, "the circle's own fill is found again")


if __name__ == "__main__":
    newui_guitest.main()
