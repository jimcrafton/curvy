r"""GUI check of the Select tool's options card: the Create ring (over the page) has an option box on its Select slot; Shift + V (the keyed
route to the same box) opens the card, built from the tool's options, and Esc closes it.

    python D:\code\curvy\tests\gui\test_curvy_selectoptions.py --exe D:\code\curvy\out\build\x64-release\curvy.exe

The scene is the one of test_curvy_canvasselect.py, drawn from the console.
"""
import os
import sys

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from test_curvy_canvasselect import CanvasSelectBase


class SelectOptionsGui(CanvasSelectBase):
    def open_creation_ring(self):
        self.app.click_at(self.empty_point())                      # on the page
        self.tap_space()
        self.app.wait_until(lambda: self.hint_keys() == "Ring: Create", "the Create ring")

    def open_card_from_the_ring(self):
        self.open_creation_ring()
        self.app.key("shift+v")                                    # Shift + the slot's key = the slot's option box
        self.app.wait_until(lambda: not self.ring_open(), "the ring to close")
        self.app.wait_until(lambda: self.app.find("toolOptionsCard").visible, "the options card to show")

    def test_over_the_page_space_opens_the_creation_ring(self):
        self.scene()
        self.open_creation_ring()
        self.app.key("escape")

    def test_the_select_slots_option_box_opens_the_card_with_the_three_options(self):
        self.scene()
        self.open_card_from_the_ring()
        for name in ("option_region", "option_secondTouches", "option_containment"):
            self.assertGreater(self.app.find(name).h, 0, name + " is on the card")

    def test_a_click_on_a_segment_changes_the_region(self):
        self.scene()
        self.open_card_from_the_ring()
        region = self.app.find("option_region")
        self.app.click_at((region.x + region.w - 8, region.y + region.h // 2))   # the last segment: Polygon
        self.app.wait_idle()
        self.assertEqual(3, int(self.app.prop("option_region", "selectedIndex")))

    def test_escape_closes_the_card(self):
        self.scene()
        self.open_card_from_the_ring()
        self.app.key("escape")
        self.app.wait_until(lambda: not self.app.find("toolOptionsCard").visible, "the card to close")


if __name__ == "__main__":
    newui_guitest.main()
