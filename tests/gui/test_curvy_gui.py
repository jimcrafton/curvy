r"""GUI tests for curvy's workspace: real mouse and keyboard through newui's automation pipe. They need a desktop nobody is using
(the app is brought to the front and driven with SendInput), so they are not part of ctest. Run:

    python D:\code\curvy\tests\gui\test_curvy_gui.py --exe D:\code\curvy\out\build\x64-release\curvy.exe      (add -k Docks, --list, --help)

The app starts with nothing open, so every test sees the same empty workspace. Headless checks of the same behaviour are in
`curvy.exe --check-workspace`; these add what only the real message pump shows: focus, hit-testing of the docks over the canvas, the top bar
buttons and the ring's key handling.
"""
import sys

sys.path.insert(0, r"D:\code\newui\tools\guitest")

import newui_guitest
from newui_guitest import AppTestCase


class CurvyCase(AppTestCase):
    """The workspace's vocabulary, shared by the tests below."""

    def ring_open(self):
        return self.app.find("radialMenu").visible              # the ring is not reflected, but its bounds report visibility

    def hint_tool(self):
        return self.app.prop("hintTool", "text")

    def focus_canvas(self):
        self.app.click("canvas")                                   # nothing open: the press is only a hint, but it focuses the canvas

    def tap_space(self):
        self.app.key("space")
        self.app.wait_until(self.ring_open, "the ring to open")

    def new_a4(self):
        self.focus_canvas()
        self.tap_space()
        self.app.key("2")                                          # the New ring's second slot
        self.app.wait_until(lambda: self.hint_tool() == "Select", "a document to open")


class StartUp(CurvyCase):
    def test_the_workspace_starts_empty(self):                     # read-only
        self.assertTrue(self.app.running)
        self.assertEqual("Nothing open", self.hint_tool())
        self.assertEqual("Tap Space to start", self.app.prop("hintKeys", "text"))
        self.assertFalse(self.ring_open())

    def test_the_docks_are_off_by_default(self):                   # read-only
        self.assertFalse(self.app.prop("toolbarToggle", "checked"))
        self.assertFalse(self.app.prop("propertiesToggle", "checked"))
        canvas = self.app.find("canvas")
        self.assertGreater(canvas.w, 600)


class Rings(CurvyCase):
    def test_tapping_space_opens_the_new_ring_and_a_key_picks_a_slot(self):
        self.focus_canvas()
        self.tap_space()
        self.app.key("escape")
        self.app.wait_until(lambda: not self.ring_open(), "the ring to close")
        self.assertEqual("Nothing open", self.hint_tool())
        self.tap_space()
        self.app.key("2")
        self.app.wait_until(lambda: self.hint_tool() == "Select", "A4 to be created")
        self.assertIn("A4", self.app.prop("documentName", "text") + " A4")

    def test_a_document_has_a_page_after_new(self):
        self.new_a4()
        self.assertFalse(self.ring_open())


class Docks(CurvyCase):
    def test_the_top_bar_buttons_show_and_hide_the_toolbar(self):
        self.new_a4()
        canvas = self.app.find("canvas")
        self.app.click("toolbarToggle")
        self.app.wait_until(lambda: self.app.prop("toolbarToggle", "checked"), "the toggle to check")
        select = self.app.find("selectTool")
        self.assertLess(select.x, canvas.x + 60)                   # flush left, inside the canvas
        self.app.click("toolbarToggle")
        self.app.wait_until(lambda: not self.app.prop("toolbarToggle", "checked"), "the toggle to clear")

    def test_a_tool_button_picks_the_tool(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        self.app.click("starTool")
        self.app.wait_until(lambda: self.hint_tool() == "Star", "the Star tool")
        self.assertTrue(self.app.prop("starTool", "checked"))
        self.app.click("selectTool")
        self.app.wait_until(lambda: self.hint_tool() == "Select", "the Select tool")
        self.assertFalse(self.app.prop("starTool", "checked"))

    def test_the_property_dock_sits_flush_right(self):
        self.new_a4()
        canvas = self.app.find("canvas")
        self.app.click("propertiesToggle")
        self.app.wait_until(lambda: self.app.prop("propertiesToggle", "checked"), "the toggle to check")
        field = self.app.find("transformXField")
        self.assertGreater(field.x, canvas.x + canvas.w - 268)

    def test_the_view_ring_toggles_the_toolbar(self):
        self.new_a4()
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + 200, canvas.y + canvas.h // 2))   # the pasteboard left of the page: Document ring
        self.tap_space()
        self.app.key("v")
        self.app.wait_until(lambda: self.app.prop("hintKeys", "text") == "Ring: View", "the View ring")
        self.app.key("t")
        self.app.wait_until(lambda: self.app.prop("toolbarToggle", "checked"), "the toolbar toggle to follow the ring")
        self.assertFalse(self.ring_open())


if __name__ == "__main__":
    sys.exit(newui_guitest.main())
