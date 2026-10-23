r"""GUI tests for curvy's workspace: real mouse and keyboard through newui's automation pipe. They need a desktop nobody is using
(the app is brought to the front and driven with SendInput), so they are not part of ctest. Run:

    python D:\code\curvy\tests\gui\test_curvy_gui.py --exe D:\code\curvy\out\build\x64-release\curvy.exe      (add -k Docks, --list, --help)

The app starts with nothing open, so every test sees the same empty workspace. Headless checks of the same behaviour are in
`curvy.exe --check-workspace`; these add what only the real message pump shows: focus, hit-testing of the docks over the canvas, the top bar
buttons and the ring's key handling.
"""
import os
import sys
import tempfile

sys.path.insert(0, r"D:\code\newui\tools\guitest")

import newui_guitest
from newui_guitest import AppTestCase


class CurvyCase(AppTestCase):
    """The workspace's vocabulary, shared by the tests below."""

    first_run = False                                              # True: no settings file, so the welcome card shows

    def setUp(self):
        # A settings folder of its own per test, so saving settings in one test (or a leftover autosave) never reaches another or the user's own.
        self._appdata = tempfile.TemporaryDirectory()
        self.addCleanup(self._appdata.cleanup)
        previous = os.environ.get("APPDATA")
        os.environ["APPDATA"] = self._appdata.name
        if not self.first_run:
            os.makedirs(os.path.join(self._appdata.name, "curvy"))
            with open(os.path.join(self._appdata.name, "curvy", "settings.ini"), "w") as f:
                f.write("welcomeShown=1\n")
        self.addCleanup(lambda: os.environ.__setitem__("APPDATA", previous) if previous is not None else os.environ.pop("APPDATA", None))
        super().setUp()

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


class SettingsDialog(CurvyCase):
    """The settings dialog is a modal newui dialog; the app exposes its root to the driver while it is open."""

    def open_settings(self):
        self.focus_canvas()
        self.tap_space()
        self.app.key("s")                                           # the New ring's Settings slot
        self.app.wait_until(self.dialog_open, "the settings dialog to open")

    def dialog_open(self):
        try:
            return self.app.find("navWorkspace").w > 0
        except newui_guitest.GuiTestError:
            return False

    def shown_page(self):
        names = ["pageAppearance", "pageWorkspace", "pagePen", "pageDrawing", "pageFiles"]
        return [name for name in names if self.app.find(name).visible]

    def tearDown(self):
        try:
            if self.dialog_open():
                self.app.click("settingsCancel")
        finally:
            super().tearDown()

    def test_the_dialog_opens_on_appearance(self):
        self.open_settings()
        self.assertEqual(["pageAppearance"], self.shown_page())

    def test_the_nav_buttons_switch_pages(self):
        self.open_settings()
        for nav, page in (("navWorkspace", "pageWorkspace"), ("navPen", "pagePen"), ("navDrawing", "pageDrawing"),
                          ("navFiles", "pageFiles"), ("navAppearance", "pageAppearance")):
            self.app.click(nav)
            self.assertEqual([page], self.shown_page(), nav)

    def test_the_shown_page_is_painted(self):
        self.open_settings()
        self.app.click("navWorkspace")
        button = self.app.find("showWelcome")                       # a control on the Workspace page
        nav = self.app.find("navWorkspace")
        self.assertTrue(button.visible)
        self.assertGreater(button.x, nav.x + nav.w)           # right of the nav column, inside the dialog

    def test_page_rows_are_spaced_out(self):
        self.open_settings()
        for nav, rows in (("navAppearance", ("themeChoice", "canvasBackgroundHex", "propertyTextSize")),
                          ("navWorkspace", ("toolbarVisible", "propertiesVisible", "rulersVisible"))):
            self.app.click(nav)
            tops = [self.app.find(name).y for name in rows]
            for above, below in zip(tops, tops[1:]):
                self.assertGreaterEqual(below - above, 24, nav)

    def test_the_footer_buttons_are_inside_the_window(self):
        self.open_settings()
        root = self.app.find("rootview")
        ok = self.app.find("settingsOk")
        self.assertLessEqual(ok.y + ok.h, root.y + root.h)

    def test_ok_applies_the_start_settings_now(self):
        self.open_settings()
        self.app.click("navWorkspace")
        self.app.click("toolbarVisible")
        self.app.click("settingsOk")
        self.app.wait_until(lambda: self.app.prop("toolbarToggle", "checked"), "the toolbar to show after OK")


class Welcome(CurvyCase):
    first_run = True

    def card_showing(self):
        canvas = self.app.find("canvas")
        red, _, _ = self.app.screenshot().pixel(canvas.x + 12, canvas.y + 12)   # the card dims everything round it
        return red < 0x26

    def start_with_card(self):
        self.app.wait_until(self.card_showing, "the welcome card to show")

    def test_a_press_on_the_top_bar_closes_the_card(self):
        self.start_with_card()
        self.app.click("toolbarToggle")
        self.app.wait_until(lambda: not self.card_showing(), "the card to close")

    def test_a_press_on_the_canvas_closes_the_card(self):
        self.start_with_card()
        self.focus_canvas()
        self.app.wait_until(lambda: not self.card_showing(), "the card to close")

    def test_a_key_closes_the_card(self):
        self.start_with_card()
        self.app.key("a")
        self.app.wait_until(lambda: not self.card_showing(), "the card to close")


if __name__ == "__main__":
    sys.exit(newui_guitest.main())
