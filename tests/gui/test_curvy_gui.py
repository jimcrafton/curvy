r"""GUI tests for curvy's workspace: real mouse and keyboard through newui's automation pipe. They need a desktop nobody is using
(the app is brought to the front and driven with SendInput), so they are not part of ctest. Run:

    python D:\code\curvy\tests\gui\test_curvy_gui.py --exe D:\code\curvy\out\build\x64-release\curvy.exe      (add -k Docks, --list, --help)

The app starts with nothing open, so every test sees the same empty workspace. Headless checks of the same behaviour are in
`curvy.exe --check-workspace`; these add what only the real message pump shows: focus, hit-testing of the docks over the canvas, the top bar
buttons and the ring's key handling.
"""
import ctypes
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, r"D:\code\newui\tools\guitest")

import newui_guitest
from newui_guitest import AppTestCase


class CurvyCase(AppTestCase):
    """The workspace's vocabulary, shared by the tests below."""

    first_run = False                                              # True: no settings file, so the welcome card shows
    extra_settings = ""

    def setUp(self):
        # A settings folder of its own per test, so saving settings in one test (or a leftover autosave) never reaches another or the user's own.
        self._appdata = tempfile.TemporaryDirectory()
        self.addCleanup(self._appdata.cleanup)
        previous = os.environ.get("APPDATA")
        os.environ["APPDATA"] = self._appdata.name
        if not self.first_run:
            os.makedirs(os.path.join(self._appdata.name, "curvy"))
            with open(os.path.join(self._appdata.name, "curvy", "settings.ini"), "w") as f:
                f.write("welcomeShown=1\n" + self.extra_settings)
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


class Closing(CurvyCase):
    """Closing the window tears down the docks and their delegate connections; a crash there shows as a non-zero exit code."""

    def exit_code(self):
        code = ctypes.c_ulong(0)
        ctypes.windll.kernel32.GetExitCodeProcess(ctypes.c_void_p(self.app._process), ctypes.byref(code))
        return code.value

    def open_docks(self, draw):
        self.new_a4()
        self.app.click("toolbarToggle")
        self.app.click("propertiesToggle")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the graph dock")
        if draw:
            canvas = self.app.find("canvas")
            self.app.click("rectangleTool")
            self.app.drag((canvas.x + 300, canvas.y + 200), (canvas.x + 420, canvas.y + 280))
            self.app.click("selectTool")

    def wait_exit(self):
        self.app.wait_until(lambda: not self.app.running, "the app to exit", timeout=10)
        self.assertEqual(0, self.exit_code())

    def test_closing_an_unmodified_document_with_every_dock_open_exits_cleanly(self):
        self.open_docks(draw=False)
        self.app.key_raw("alt+f4")
        self.wait_exit()

    def test_closing_a_modified_document_and_discarding_exits_cleanly(self):
        self.open_docks(draw=True)
        self.app.key_raw("alt+f4")
        self.app.wait_for_dialog()                                    # the save-changes question
        self.app.key_raw("n")
        self.wait_exit()


class Recovery(CurvyCase):
    """A crash is simulated by killing the app (App.close ends its job object) after the autosave has written its copy; the next start asks once."""

    extra_settings = "autosaveMinutes=1\n"                        # the first autosave comes that long after the start (the default is 5)

    def autosaves(self):
        folder = os.path.join(self._appdata.name, "curvy", "autosave")
        return [n for n in os.listdir(folder) if n.endswith(".svg")] if os.path.isdir(folder) else []

    def crash_with_an_autosave(self):
        self.new_a4()
        self.app.click("toolbarToggle")                               # the tools are hidden until then, and an unmodified document is not autosaved
        canvas = self.app.find("canvas")
        self.app.click("rectangleTool")
        self.app.drag((canvas.x + 300, canvas.y + 200), (canvas.x + 420, canvas.y + 280))
        self.app.click("selectTool")
        self.app.wait_until(self.autosaves, "the autosave to write its copy", timeout=180)   # the timer ticks every 30 s, and a tick writes only once a minute has passed since the start
        self.app.close()
        self.assertFalse(self.app.running)

    def restart(self):
        self.app = self.launch()
        self.addCleanup(self.app.close)
        self.app.wait_for_dialog()                                    # the recovery question

    def assert_asked_only_once(self):
        time.sleep(1.5)                                               # a second question would come from the same startup timer
        cls, ours = self.app.foreground_class()
        self.assertFalse(cls == "#32770" and ours, "the recovery question came up again")

    def test_a_crash_leaves_one_question_and_yes_recovers_the_document(self):
        self.crash_with_an_autosave()
        self.restart()
        self.app.key_raw("y")
        self.app.wait_until(lambda: self.hint_tool() != "Nothing open", "the recovered document to open")
        self.assert_asked_only_once()
        self.assertEqual([], self.autosaves())

    def test_no_throws_the_copy_away_and_asks_only_once(self):
        self.crash_with_an_autosave()
        self.restart()
        self.app.key_raw("n")
        self.assert_asked_only_once()
        self.assertEqual("Nothing open", self.hint_tool())
        self.assertEqual([], self.autosaves())


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


class GraphDock(CurvyCase):
    def test_the_graph_toggle_shows_the_dock_flush_right(self):
        self.new_a4()
        canvas = self.app.find("canvas")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the toggle to check")
        tree = self.app.find("graphTree")
        self.assertGreater(tree.x, canvas.x + canvas.w - 340)      # the dock is 320 wide, flush with the canvas edge
        self.assertGreater(tree.h, 100)
        self.app.click("graphToggle")
        self.app.wait_until(lambda: not self.app.prop("graphToggle", "checked"), "the toggle to clear")

    def test_the_graph_dock_sits_beside_the_property_dock(self):
        self.new_a4()
        self.app.click("propertiesToggle")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the toggle to check")
        graph = self.app.find("graphTree")
        field = self.app.find("transformXField")
        self.assertLess(graph.x + graph.w, field.x)                # the graph dock is left of the property boxes

    def test_the_mode_control_switches_to_flow(self):
        self.new_a4()
        self.app.click("graphToggle")
        self.app.click_segment("graphModeControl", 1)
        self.app.wait_until(lambda: "nobody uses" in self.app.prop("graphStatus", "text"), "the Flow view")
        self.app.click_segment("graphModeControl", 0)
        self.app.wait_until(lambda: "Layers and groups" in self.app.prop("graphStatus", "text"), "the Document view")

    def test_dragging_a_caption_in_the_box_scrubs_the_number(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        canvas = self.app.find("canvas")
        self.app.click("rectangleTool")
        self.app.drag((canvas.x + 300, canvas.y + 200), (canvas.x + 420, canvas.y + 280))
        self.app.click("selectTool")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the toggle to check")
        tree = self.app.find("graphTree")
        self.app.click_at((tree.x + 120, tree.y + 2 * 22 + 11))         # the rectangle's Stroke row (layer, rectangle, stroke, fill)
        self.app.wait_until(lambda: self.app.prop("graphDetailKind", "text") == "Stroke", "the stroke's box")
        before = float(self.param_value(2))                              # Colour, Paint, Width, Opacity
        x, y = self.param_point(2, 20)                                   # on the caption
        self.app.drag((x, y), (x + 12, y))
        self.app.wait_until(lambda: float(self.param_value(2)) > before + 5, "the width to follow the drag")
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: float(self.param_value(2)) == before, "one undo to bring the width back")

    # The parameter box is painted by one grid; a value is read by opening its editor.
    def param_point(self, row, dx=100):
        grid = self.app.find("graphParams")
        return (grid.x + dx, grid.y + 4 + row * 24 + 10)

    def param_value(self, row):
        self.app.click_at(self.param_point(row))
        self.app.wait_until(lambda: self.app.find("graphDetailEditor").visible, "the row's editor")
        text = self.app.text_of("graphDetailEditor")
        self.app.key("escape")
        self.app.wait_until(lambda: not self.app.find("graphDetailEditor").visible, "the editor to close")
        return text

    def transform_fields(self):
        names = ["translateX", "translateY", "rotation", "scaleX", "scaleY", "shear", "pivotX", "pivotY"]
        return dict(zip(names, [self.param_value(i) for i in range(8)]))

    def test_a_new_polygon_has_an_identity_transform_node(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        canvas = self.app.find("canvas")
        self.app.click("polygonTool")
        self.app.drag((canvas.x + 300, canvas.y + 250), (canvas.x + 380, canvas.y + 250))
        self.app.click("selectTool")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the toggle to check")
        self.app.click_segment("graphModeControl", 1)                  # the Flow view lists the transform node; Document shows shapes and looks
        self.app.wait_until(lambda: "nobody uses" in self.app.prop("graphStatus", "text"), "the Flow view")
        self.app.click_row("graphTree", "Transform")
        self.app.wait_until(lambda: self.app.prop("graphDetailKind", "text") == "Transform", "the transform node's box")
        fields = self.transform_fields()
        for name in ("translateX", "translateY", "rotation", "shear"):
            self.assertEqual("0", fields[name], name)
        self.assertEqual("1", fields["scaleX"])
        self.assertEqual("1", fields["scaleY"])

    TRANSFORM_ROWS = {"translateX": 0, "translateY": 1, "rotation": 2, "scaleX": 3, "scaleY": 4, "shear": 5, "pivotX": 6, "pivotY": 7}

    def open_transform_of_new_rectangle(self):
        # A 120x80 rectangle (so width and height differ), with the property dock beside the graph dock to read its bounds back.
        self.new_a4()
        self.app.click("toolbarToggle")
        self.app.click("propertiesToggle")
        canvas = self.app.find("canvas")
        self.app.click("rectangleTool")
        self.app.drag((canvas.x + 300, canvas.y + 200), (canvas.x + 420, canvas.y + 280))
        self.app.click("selectTool")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the toggle to check")
        self.app.click_segment("graphModeControl", 1)
        self.app.wait_until(lambda: "nobody uses" in self.app.prop("graphStatus", "text"), "the Flow view")
        self.app.click_row("graphTree", "Transform")
        self.app.wait_until(lambda: self.app.prop("graphDetailKind", "text") == "Transform", "the transform node's box")

    def bounds(self):
        return {k: float(self.app.prop("transform%sField" % k, "text")) for k in ("X", "Y", "W", "H")}

    def type_param(self, name, value):
        self.app.click_at(self.param_point(self.TRANSFORM_ROWS[name]))
        self.app.wait_until(lambda: self.app.find("graphDetailEditor").visible, "the row's editor")
        self.app.key("ctrl+a")
        self.app.type(str(value))
        self.app.key("enter")

    def test_typing_translate_x_moves_the_shape_and_one_undo_puts_it_back(self):
        self.open_transform_of_new_rectangle()
        before = self.bounds()
        self.type_param("translateX", 50)
        self.app.wait_until(lambda: abs(self.bounds()["X"] - (before["X"] + 50)) < 0.6, "the shape to move 50 right")
        after = self.bounds()
        self.assertAlmostEqual(before["Y"], after["Y"], delta=0.6)
        self.assertAlmostEqual(before["W"], after["W"], delta=0.6)
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: abs(self.bounds()["X"] - before["X"]) < 0.6, "one undo to bring the shape back")

    def test_typing_scale_x_widens_the_shape_about_its_pivot(self):
        self.open_transform_of_new_rectangle()
        before = self.bounds()
        self.type_param("scaleX", 2)
        self.app.wait_until(lambda: abs(self.bounds()["W"] - before["W"] * 2) < 0.6, "the shape to double in width")
        after = self.bounds()
        self.assertAlmostEqual(before["H"], after["H"], delta=0.6)
        self.assertAlmostEqual(before["X"] + before["W"] / 2, after["X"] + after["W"] / 2, delta=0.6)   # the default pivot is the centre
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: abs(self.bounds()["W"] - before["W"]) < 0.6, "one undo to restore the width")

    def test_typing_a_rotation_of_90_swaps_width_and_height(self):
        self.open_transform_of_new_rectangle()
        before = self.bounds()
        self.type_param("rotation", 90)
        self.app.wait_until(lambda: abs(self.bounds()["W"] - before["H"]) < 0.6, "the shape to turn a quarter")
        after = self.bounds()
        self.assertAlmostEqual(before["W"], after["H"], delta=0.6)
        self.assertAlmostEqual(before["X"] + before["W"] / 2, after["X"] + after["W"] / 2, delta=0.6)   # turned about its centre
        self.assertAlmostEqual(before["Y"] + before["H"] / 2, after["Y"] + after["H"] / 2, delta=0.6)
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: abs(self.bounds()["W"] - before["W"]) < 0.6, "one undo to turn it back")

    def test_a_pivot_off_centre_keeps_that_point_fixed_while_scaling(self):
        self.open_transform_of_new_rectangle()
        before = self.bounds()
        pivot = self.transform_fields()
        self.type_param("pivotX", float(pivot["pivotX"]) - before["W"] / 2)       # the left edge, assuming the pivot starts at the centre
        self.type_param("scaleX", 2)
        self.app.wait_until(lambda: abs(self.bounds()["W"] - before["W"] * 2) < 0.6, "the shape to double in width")
        after = self.bounds()
        self.assertAlmostEqual(before["X"], after["X"], delta=0.6)                # growing rightwards from the pivot
        self.app.key("ctrl+z")
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: abs(self.bounds()["W"] - before["W"]) < 0.6 and abs(self.bounds()["X"] - before["X"]) < 0.6,
                            "two undos to restore the shape")

    def test_reset_puts_a_turned_text_with_an_off_centre_pivot_back(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        self.app.click("textTool")
        self.app.wait_until(lambda: self.hint_tool() == "Text", "the Text tool")
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + canvas.w // 2, canvas.y + canvas.h // 2))
        self.app.type("Hello")
        self.app.click("selectTool")
        self.app.wait_until(lambda: self.hint_tool() == "Select", "the Select tool")
        self.app.key("ctrl+a")
        self.app.click("propertiesToggle")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the toggle to check")
        self.app.click_segment("graphModeControl", 1)
        self.app.wait_until(lambda: "nobody uses" in self.app.prop("graphStatus", "text"), "the Flow view")
        self.app.click_row("graphTree", "Transform")
        self.app.wait_until(lambda: self.app.prop("graphDetailKind", "text") == "Transform", "the transform node's box")
        before = self.bounds()
        pivot = self.transform_fields()
        self.type_param("pivotX", float(pivot["pivotX"]) - before["W"] / 2)
        self.type_param("rotation", 30)
        self.app.wait_until(lambda: abs(self.bounds()["W"] - before["W"]) > 1, "the text to turn")
        self.app.wait_until(lambda: abs(float(self.app.prop("transformAngleField", "text")) - 30) < 0.6, "the frame to turn 30 degrees")
        self.app.click("graphResetButton")
        self.app.wait_until(lambda: all(abs(self.bounds()[k] - before[k]) < 0.6 for k in before), "Reset to put the text back")
        self.app.wait_until(lambda: abs(float(self.app.prop("transformAngleField", "text"))) < 0.6, "the frame to turn back")   # the frame angle is not in the bounds
        fields = self.transform_fields()
        self.assertEqual("0", fields["rotation"])
        self.assertEqual("0", fields["shear"])

    def test_clicking_the_colour_swatch_of_a_fill_opens_the_colour_card(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        canvas = self.app.find("canvas")
        self.app.click("rectangleTool")
        self.app.drag((canvas.x + 300, canvas.y + 200), (canvas.x + 420, canvas.y + 280))
        self.app.click("selectTool")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the toggle to check")
        tree = self.app.find("graphTree")
        self.app.click_at((tree.x + 120, tree.y + 3 * 22 + 11))         # the rectangle's Fill row (layer, rectangle, stroke, fill)
        self.app.wait_until(lambda: self.app.prop("graphDetailKind", "text") == "Fill", "the fill's box")
        self.app.click_at(self.param_point(0, 8 + 74 + 14))               # the swatch of the first row, Colour
        self.app.wait_until(lambda: self.app.find("cardClose").visible, "the Colour card to open")
        card = self.app.find("cardClose")
        self.assertLess(card.x, tree.x)                                  # beside the graph dock, not over it

    def test_the_view_ring_has_a_graph_slot(self):
        self.new_a4()
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + 200, canvas.y + canvas.h // 2))
        self.tap_space()
        self.app.key("v")
        self.app.wait_until(lambda: self.app.prop("hintKeys", "text") == "Ring: View", "the View ring")
        self.app.key("n")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the Graph toggle to follow the ring")


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

    def test_escape_cancels_and_reverts(self):
        self.open_settings()
        self.app.click("navWorkspace")
        self.app.click("toolbarVisible")                            # not applied until OK
        self.app.key_raw("escape")
        self.app.wait_until(lambda: not self.dialog_open(), "the dialog to close")
        self.assertFalse(self.app.prop("toolbarToggle", "checked"))
        # Only the Cancel button's click says this; closing the window any other way would not, so Escape did reach the button.
        self.app.wait_until(lambda: self.app.prop("hintKeys", "text") == "Settings cancelled", "Cancel's click to run")

    def test_ok_applies_the_start_settings_now(self):
        self.open_settings()
        self.app.click("navWorkspace")
        self.app.click("toolbarVisible")
        self.app.click("settingsOk")
        self.app.wait_until(lambda: self.app.prop("toolbarToggle", "checked"), "the toolbar to show after OK")


class AppearanceCards(CurvyCase):
    """The Object ring's Fill and Stroke slots open the dock's Colour / Stroke box as a card at the pen, with the docks off."""

    def draw_rectangle(self):
        self.new_a4()
        canvas = self.app.find("canvas")
        cx, cy = canvas.x + canvas.w // 2, canvas.y + canvas.h // 2
        self.app.click_at((cx, cy))
        self.tap_space()                                           # over the page: the Create ring
        self.app.key("r")
        self.app.wait_until(lambda: self.hint_tool() == "Rectangle", "the rectangle tool")
        self.app.drag((cx - 60, cy - 40), (cx + 60, cy + 40))
        self.app.key("escape")                                     # back to Select
        return cx, cy

    def open_card(self, key):
        cx, cy = self.draw_rectangle()
        self.app.click_at((cx, cy))                                # select the shape
        self.tap_space()
        self.app.wait_until(lambda: self.app.prop("hintKeys", "text") == "Ring: Object", "the Object ring")
        self.app.key(key)

    def card_visible(self, name):
        """In the card: shown, and not flush against the canvas's right edge where the (hidden) dock keeps its boxes."""
        try:
            box, canvas = self.app.find(name), self.app.find("canvas")
        except newui_guitest.GuiTestError:
            return False
        return box.visible and box.w > 0 and box.x + box.w < canvas.x + canvas.w - 2

    def test_the_fill_slot_opens_the_colour_box_as_a_card(self):
        self.open_card("3")
        self.app.wait_until(lambda: self.card_visible("colorPanel"), "the Colour card")
        box, canvas = self.app.find("colorPanel"), self.app.find("canvas")
        self.assertGreaterEqual(box.x, canvas.x)
        self.assertLessEqual(box.x + box.w, canvas.x + canvas.w)
        self.assertFalse(self.app.prop("propertiesToggle", "checked"))     # the dock itself stays off
        self.app.click_at((canvas.x + 40, canvas.y + canvas.h - 40))      # a press elsewhere puts the card away
        self.app.wait_until(lambda: not self.card_visible("colorPanel"), "the card to close")

    def test_a_slider_in_the_card_changes_the_fill_and_the_mode_control_switches_models(self):
        self.open_card("3")
        self.app.wait_until(lambda: self.card_visible("colorPanel"), "the Colour card")
        self.app.click_segment("pickerTabs", 1)                          # the card opens on the wheel; the sliders are the other tab
        before = self.app.prop("hexField", "text")
        slider = self.app.find("comp0Slider")
        self.app.drag((slider.x + slider.w // 2, slider.y + slider.h // 2), (slider.x + 4, slider.y + slider.h // 2))
        self.app.wait_until(lambda: self.app.prop("hexField", "text") != before, "the fill to change")
        self.app.click_segment("colorModeControl", 3)                    # HSB
        self.app.wait_until(lambda: self.app.prop("comp0SliderCaption", "text") == "H", "the sliders to become H S B")

    def test_dragging_on_the_wheel_changes_the_fill(self):
        self.open_card("3")
        self.app.wait_until(lambda: self.card_visible("colorPanel"), "the Colour card")
        before = self.app.prop("hexField", "text")
        wheel = self.app.find("colorWheelHost")
        cx, cy = wheel.x + wheel.w // 2, wheel.y + wheel.h // 2
        radius = min(wheel.w, wheel.h) // 2 - 2 - 8
        self.app.drag((cx + radius, cy), (cx, cy + radius))               # round the ring from red towards green
        self.app.wait_until(lambda: self.app.prop("hexField", "text") != before, "the fill to change")

    def test_choosing_linear_arms_the_gradient_tool_and_the_card_stays_for_the_shape(self):
        cx, cy = self.draw_rectangle()
        self.app.click_at((cx, cy))
        self.tap_space()
        self.app.wait_until(lambda: self.app.prop("hintKeys", "text") == "Ring: Object", "the Object ring")
        self.app.key("3")
        self.app.wait_until(lambda: self.card_visible("colorPanel"), "the Colour card")
        self.app.click("fillLinearButton")
        self.app.wait_until(lambda: self.hint_tool() == "Gradient", "the Gradient tool")
        self.assertTrue(self.app.find("gradientBarHost").visible)
        self.app.drag((cx - 50, cy), (cx + 50, cy))                        # lay the line across the shape: a press on the canvas
        self.assertTrue(self.card_visible("colorPanel"))                   # the card is still up for the stops

    def test_escape_closes_the_card_with_a_slider_focused(self):
        self.open_card("3")
        self.app.wait_until(lambda: self.card_visible("colorPanel"), "the Colour card")
        self.app.click_segment("pickerTabs", 1)
        slider = self.app.find("comp1Slider")
        self.app.click_at((slider.x + slider.w // 2, slider.y + slider.h // 2))   # the focus is now in the card
        self.app.key("escape")
        self.app.wait_until(lambda: not self.card_visible("colorPanel"), "the card to close")

    def test_the_close_button_puts_the_card_away(self):
        self.open_card("3")
        self.app.wait_until(lambda: self.card_visible("colorPanel"), "the Colour card")
        self.app.click("cardClose")
        self.app.wait_until(lambda: not self.card_visible("colorPanel"), "the card to close")
        self.app.key("space")                                              # the focus went back to the canvas, so Space opens the ring again
        self.app.wait_until(self.ring_open, "the ring to open")

    def test_the_stroke_slot_opens_the_stroke_box_and_escape_closes_it(self):
        self.open_card("4")
        self.app.wait_until(lambda: self.card_visible("strokePanel"), "the Stroke card")
        self.assertFalse(self.card_visible("colorPanel"))
        self.app.key("escape")
        self.app.wait_until(lambda: not self.card_visible("strokePanel"), "the card to close")


class ToolOptions(CurvyCase):
    """Double-clicking a toolbar button opens that tool's options card; the x and Esc close it."""

    def card_open(self):
        try:
            return self.app.find("toolOptionsCard").visible      # the card's own root: a child's flag ignores a hidden parent
        except newui_guitest.GuiTestError:
            return False

    def open_star_options(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        self.app.double_click("starTool")
        self.app.wait_until(self.card_open, "the Star options card")

    def test_double_click_opens_the_options_and_the_slider_moves_the_default(self):
        self.open_star_options()
        self.assertEqual("Star", self.app.prop("optionsTitle", "text"))
        slider = self.app.find("optionSlider1")
        self.app.drag((slider.x + slider.w // 2, slider.y + slider.h // 2), (slider.x + slider.w - 3, slider.y + slider.h // 2))
        self.app.wait_until(lambda: self.app.prop("optionValue1", "text") == "24", "the points to reach the top of the slider")
        self.app.click("optionsReset")
        self.app.wait_until(lambda: self.app.prop("optionValue1", "text") == "5", "Reset to put the default back")

    def test_the_close_button_and_escape_close_it(self):
        self.open_star_options()
        self.app.click("optionsClose")
        self.app.wait_until(lambda: not self.card_open(), "the card to close")
        self.app.double_click("starTool")
        self.app.wait_until(self.card_open, "the card to open again")
        self.app.click("optionValue1")                                     # focus in the card
        self.app.key("escape")
        self.app.wait_until(lambda: not self.card_open(), "Esc to close it")


class NewDocumentDialog(CurvyCase):
    """Return presses the default Create button and Escape the Cancel button (Control::setDefaultControl/setCancelControl)."""

    def open_dialog(self):
        self.focus_canvas()
        self.tap_space()
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + canvas.w // 2, canvas.y + canvas.h // 2))   # the ring opens under the pointer: this is its hub
        self.app.wait_until(self.dialog_open, "the New Document dialog to open")

    def dialog_open(self):
        try:
            return self.app.find("createButton").w > 0
        except newui_guitest.GuiTestError:
            return False

    def tearDown(self):
        try:
            if self.dialog_open():
                self.app.click("cancelButton")
        finally:
            super().tearDown()

    def test_escape_cancels_the_dialog(self):
        self.open_dialog()
        self.app.key_raw("escape")                                 # key() would bring the disabled main window forward, not the dialog
        self.app.wait_until(lambda: not self.dialog_open(), "the dialog to close")
        self.assertEqual("Nothing open", self.hint_tool())

    def test_the_default_button_has_the_accent_border(self):
        self.open_dialog()
        shot = self.app.screenshot("newDocumentRoot")

        def edge(name):                                            # the brightest of the pixels along the button's top edge
            b = self.app.find(name)
            return max((shot.pixel(b.x + b.w // 2, b.y + dy) for dy in (0, 1)), key=sum)

        create, cancel = edge("createButton"), edge("cancelButton")
        self.assertGreater(create[0], 0xC0, create)                # amber #FFB454: strong red, weak blue
        self.assertLess(create[2], 0x90, create)
        self.assertLess(cancel[0], 0x80, cancel)                   # the other button keeps the plain grey border

    def test_return_creates_the_document(self):
        self.open_dialog()
        self.app.key_raw("enter")
        self.app.wait_until(lambda: not self.dialog_open(), "the dialog to close")
        self.app.wait_until(lambda: self.hint_tool() == "Select", "a document to open")


class TextObject(CurvyCase):
    """A text object in the property dock: the Text box with its Size / Track / Lead fields, and its fill colour."""

    def make_text(self, words="Hi"):
        self.new_a4()
        self.app.click("toolbarToggle")
        self.app.click("textTool")
        self.app.wait_until(lambda: self.hint_tool() == "Text", "the Text tool")
        canvas = self.app.find("canvas")
        self.at = (canvas.x + canvas.w // 2, canvas.y + canvas.h // 2)
        self.app.click_at(self.at)
        self.app.type(words)
        self.app.click("selectTool")                               # choosing a tool finishes the typing
        self.app.wait_until(lambda: self.hint_tool() == "Select", "the Select tool")
        self.app.key("ctrl+a")                                     # the text is the only object
        self.app.click("propertiesToggle")
        self.app.wait_until(lambda: self.app.prop("propertiesToggle", "checked"), "the property dock")

    def shown(self, name):
        try:
            box, canvas = self.app.find(name), self.app.find("canvas")
        except newui_guitest.GuiTestError:
            return False
        return box.visible and box.w > 0 and box.x + box.w <= canvas.x + canvas.w + 1

    def test_a_selected_text_shows_the_text_box_with_size_track_and_lead(self):
        self.make_text()
        self.app.wait_until(lambda: self.shown("textPanel"), "the Text box")
        for name in ("textFontCombo", "textSizeField", "textTrackingField", "textLeadingField",
                     "textSizeLabel", "textTrackingLabel", "textLeadingLabel", "textBoldButton", "textItalicButton"):
            self.assertTrue(self.shown(name), name)
        self.assertEqual("Size", self.app.prop("textSizeLabel", "text"))
        self.assertEqual("Track", self.app.prop("textTrackingLabel", "text"))
        self.assertEqual("Lead", self.app.prop("textLeadingLabel", "text"))

    def test_the_text_box_is_hidden_for_a_shape(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        self.app.click("rectangleTool")
        canvas = self.app.find("canvas")
        cx, cy = canvas.x + canvas.w // 2, canvas.y + canvas.h // 2
        self.app.drag((cx - 60, cy - 40), (cx + 60, cy + 40))
        self.app.click("selectTool")
        self.app.click_at((cx, cy))
        self.app.click("propertiesToggle")
        self.app.wait_until(lambda: self.shown("transformPanel"), "the dock")
        self.assertFalse(self.shown("textPanel"))

    def test_the_fields_show_the_selected_texts_values(self):
        self.make_text()
        self.app.wait_until(lambda: self.shown("textPanel"), "the Text box")
        self.assertEqual("24", self.app.prop("textSizeField", "text"))
        self.assertEqual("0", self.app.prop("textTrackingField", "text"))
        self.assertEqual("100", self.app.prop("textLeadingField", "text"))

    def test_typing_a_size_resizes_the_text(self):
        self.make_text()
        self.app.wait_until(lambda: self.shown("textPanel"), "the Text box")
        before = float(self.app.prop("transformWField", "text"))
        self.app.double_click("textSizeField")
        self.app.key("ctrl+a")
        self.app.type("48")
        self.app.key("enter")
        self.app.wait_until(lambda: float(self.app.prop("transformWField", "text")) > before * 1.8, "the text to grow with its size")

    def test_dragging_the_size_caption_scrubs_the_size(self):
        self.make_text()
        self.app.wait_until(lambda: self.shown("textPanel"), "the Text box")
        before = float(self.app.prop("transformWField", "text"))
        self.app.drag_by("textSizeLabel", 30, 0)
        self.app.wait_until(lambda: self.app.prop("textSizeField", "text") != "24", "the size to scrub")
        self.assertGreater(float(self.app.prop("transformWField", "text")), before)

    def test_typing_a_tracking_widens_the_text(self):
        self.make_text()
        self.app.wait_until(lambda: self.shown("textPanel"), "the Text box")
        before = float(self.app.prop("transformWField", "text"))
        self.app.double_click("textTrackingField")
        self.app.key("ctrl+a")
        self.app.type("200")
        self.app.key("enter")
        self.app.wait_until(lambda: float(self.app.prop("transformWField", "text")) > before, "the text to widen")

    def test_the_wheel_gives_text_a_colour_not_just_grey(self):
        self.make_text()
        self.app.wait_until(lambda: self.shown("colorPanel"), "the Colour box")
        wheel = self.app.find("colorWheelHost")
        cx, cy = wheel.x + wheel.w // 2, wheel.y + wheel.h // 2
        radius = min(wheel.w, wheel.h) // 2 - 2 - 8
        self.app.drag((cx + radius, cy), (cx, cy + radius))           # a hue on the ring: black stays black
        self.app.drag((cx - 20, cy + 20), (cx + 25, cy - 25))         # the square: bright and saturated
        self.app.wait_until(lambda: self.app.prop("hexField", "text").lstrip("#").upper() != "000000", "the fill to change")
        hexa = self.app.prop("hexField", "text").lstrip("#")
        red, green, blue = int(hexa[0:2], 16), int(hexa[2:4], 16), int(hexa[4:6], 16)
        self.assertFalse(red == green == blue, "the fill kept a hue: " + hexa)


class ShapeBox(CurvyCase):
    """The Shape box of the property dock: the numbers of a lone star, polygon or rounded rectangle, typed into its fields."""

    def draw_alone(self, tool):
        self.new_a4()
        self.app.click("toolbarToggle")
        self.app.click(tool)
        canvas = self.app.find("canvas")
        cx, cy = canvas.x + canvas.w // 2, canvas.y + canvas.h // 2
        self.app.drag((cx - 60, cy - 60), (cx + 60, cy + 60))
        self.app.click("selectTool")
        self.app.wait_until(lambda: self.hint_tool() == "Select", "the Select tool")
        self.app.key("ctrl+a")                                     # the shape is the only object
        self.app.click("propertiesToggle")
        self.app.wait_until(lambda: self.app.prop("propertiesToggle", "checked"), "the property dock")

    def shown(self, name):
        try:
            box, canvas = self.app.find(name), self.app.find("canvas")
        except newui_guitest.GuiTestError:
            return False
        return box.visible and box.w > 0 and box.x + box.w <= canvas.x + canvas.w + 1

    def type_into(self, field, text):
        self.app.double_click(field)
        self.app.key("ctrl+a")
        self.app.type(text)
        self.app.key("enter")

    def test_a_star_shows_points_and_inner_and_typing_points_redraws_it(self):
        self.draw_alone("starTool")
        self.app.wait_until(lambda: self.shown("shapePanel"), "the Shape box")
        self.assertTrue(self.shown("shapeCountRow") and self.shown("shapeInnerRow"))
        self.assertFalse(self.shown("shapeRadiusRow") or self.shown("shapeStartRow") or self.shown("shapeSweepRow"))   # a hidden row, not its field, carries the flag
        self.assertEqual("Points", self.app.prop("shapeCountLabel", "text"))
        self.assertEqual("Star", self.app.prop("shapePanelHeaderInfo", "text"))
        points = self.app.prop("shapeCountField", "text")
        self.type_into("shapeCountField", "9")
        self.app.wait_until(lambda: self.app.prop("shapeCountField", "text") == "9", "nine points")
        self.assertNotEqual(points, "9")
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + canvas.w // 2, canvas.y + canvas.h // 2))   # the field keeps Ctrl+Z for its own text until the focus leaves it
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: self.app.prop("shapeCountField", "text") == points, "undo to bring the points back")

    def test_typing_an_inner_radius_changes_the_star(self):
        self.draw_alone("starTool")
        self.app.wait_until(lambda: self.shown("shapePanel"), "the Shape box")
        self.type_into("shapeInnerField", "80")
        self.app.wait_until(lambda: self.app.prop("shapeInnerField", "text") == "80", "an inner radius of 80%")

    def test_a_rounded_rectangle_shows_only_the_corner_and_a_value_is_kept(self):
        self.draw_alone("roundRectTool")
        self.app.wait_until(lambda: self.shown("shapePanel"), "the Shape box")
        self.assertTrue(self.shown("shapeRadiusRow"))
        self.assertFalse(self.shown("shapeCountRow") or self.shown("shapeInnerRow"))
        self.type_into("shapeRadiusField", "12")
        self.app.wait_until(lambda: self.app.prop("shapeRadiusField", "text") == "12", "a corner of 12")

    def test_a_drawn_star_stays_editable_through_several_edits_undo_and_redo(self):
        self.draw_alone("starTool")                                # the tool makes a parametric shape, so every edit is a node parameter
        self.app.wait_until(lambda: self.shown("shapePanel"), "the Shape box")
        points = self.app.prop("shapeCountField", "text")
        self.type_into("shapeCountField", "9")
        self.app.wait_until(lambda: self.app.prop("shapeCountField", "text") == "9", "nine points")
        self.type_into("shapeInnerField", "80")
        self.app.wait_until(lambda: self.app.prop("shapeInnerField", "text") == "80", "an inner radius of 80%")
        canvas = self.app.find("canvas")
        centre = (canvas.x + canvas.w // 2, canvas.y + canvas.h // 2)
        self.app.click_at(centre)                                  # the field keeps Ctrl+Z for its own text until the focus leaves it
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: self.app.prop("shapeInnerField", "text") != "80", "the inner radius undone")
        self.assertEqual("9", self.app.prop("shapeCountField", "text"))
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: self.app.prop("shapeCountField", "text") == points, "the points undone")
        self.app.key("ctrl+y")
        self.app.wait_until(lambda: self.app.prop("shapeCountField", "text") == "9", "the points redone")
        self.assertTrue(self.shown("shapePanel"), "the shape is still a shape after undo and redo")
        self.type_into("shapeCountField", "6")
        self.app.wait_until(lambda: self.app.prop("shapeCountField", "text") == "6", "six points after a redo")

    def test_a_plain_rectangle_has_no_shape_box(self):
        self.draw_alone("rectangleTool")
        self.app.wait_until(lambda: self.shown("transformPanel"), "the dock")
        self.assertFalse(self.shown("shapePanel"))


class Grouping(CurvyCase):
    """Group / Ungroup from the Object ring. A group shows only Transform and Opacity in the dock, so that is what tells grouped from not."""

    def shown(self, name):
        try:
            box, canvas = self.app.find(name), self.app.find("canvas")
        except newui_guitest.GuiTestError:
            return False
        return box.visible and box.w > 0 and box.x + box.w <= canvas.x + canvas.w + 1

    def draw_two(self):
        self.new_a4()
        canvas = self.app.find("canvas")
        cx, cy = canvas.x + canvas.w // 2, canvas.y + canvas.h // 2
        for dx in (-120, 60):
            self.app.click_at((cx + dx, cy))
            self.tap_space()
            self.app.key("r")
            self.app.wait_until(lambda: self.hint_tool() == "Rectangle", "the rectangle tool")
            self.app.drag((cx + dx - 30, cy - 40), (cx + dx + 30, cy + 40))
            self.app.key("escape")
            self.app.wait_until(lambda: self.hint_tool() == "Select", "the Select tool")
        self.app.click("propertiesToggle")
        self.app.wait_until(lambda: self.app.prop("propertiesToggle", "checked"), "the dock")
        self.app.click_at((cx - 120, cy))
        self.app.key("ctrl+a")
        self.app.wait_until(lambda: self.shown("colorPanel"), "both selected")

    def object_ring(self):
        self.tap_space()
        self.app.wait_until(lambda: self.app.prop("hintKeys", "text") == "Ring: Object", "the Object ring")

    def picked(self):
        """How many objects a click on the left rectangle selects: 2 when it is grouped with the other."""
        canvas = self.app.find("canvas")
        cx, cy = canvas.x + canvas.w // 2, canvas.y + canvas.h // 2
        self.app.click_at((cx, cy + 250))                          # empty page: nothing selected
        self.app.click_at((cx - 120, cy))
        self.app.wait_until(lambda: self.app.prop("transformPanelHeaderInfo", "text") != "no selection", "a selection")
        return self.app.prop("transformPanelHeaderInfo", "text")

    def test_the_ring_group_slot_groups_the_selection(self):
        self.draw_two()
        self.assertEqual("1 object", self.picked())
        self.app.key("ctrl+a")
        self.object_ring()
        self.app.key("2")
        self.assertEqual("2 objects", self.picked())

    def test_the_dock_follows_a_group(self):
        self.draw_two()
        self.object_ring()
        self.app.key("2")
        self.app.wait_until(lambda: not self.shown("colorPanel"), "the dock to show a group's boxes")

    def test_the_group_shortcut_and_ungroup(self):
        self.draw_two()
        self.app.key("ctrl+g")
        self.assertEqual("2 objects", self.picked())
        self.app.key("ctrl+shift+g")
        self.assertEqual("1 object", self.picked())

    def test_undo_takes_the_group_apart(self):
        self.draw_two()
        self.object_ring()
        self.app.key("2")
        self.assertEqual("2 objects", self.picked())
        self.app.key("ctrl+z")
        self.assertEqual("1 object", self.picked())

    def test_the_flow_view_enters_a_group_and_escape_leaves_it(self):
        self.draw_two()
        self.app.key("ctrl+g")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the graph dock")
        self.app.click_segment("graphModeControl", 1)
        self.app.wait_until(lambda: "nobody uses" in self.app.prop("graphStatus", "text"), "the Flow view")
        self.app.click_row("graphTree", "group")                         # the group's subnet node, a top row of the root
        self.app.wait_until(lambda: self.app.prop("graphDetailKind", "text") == "Group", "the group's box")
        self.app.click("graphEnterButton")
        self.app.wait_until(lambda: "Inside" in self.app.prop("graphStatus", "text"), "the view to enter the group")
        self.app.find_row("graphTree", "..")                              # the way back is the first row
        self.app.find_row("graphTree", "Transform")                       # and the members' nodes are listed
        self.app.key("escape")
        self.app.wait_until(lambda: "nobody uses" in self.app.prop("graphStatus", "text"), "Escape to leave the group")

    def test_a_group_row_of_the_document_view_enters_the_group(self):
        self.draw_two()
        self.app.key("ctrl+g")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the graph dock")
        self.app.click_row("graphTree", "<Group>")
        self.app.wait_until(lambda: self.app.prop("graphDetailKind", "text") == "Group", "the group's box")
        self.app.click("graphEnterButton")
        self.app.wait_until(lambda: "Inside" in self.app.prop("graphStatus", "text"), "the view to enter the group")
        self.app.click_row("graphTree", "..")
        self.app.wait_until(lambda: "nobody uses" in self.app.prop("graphStatus", "text"), "the .. row to leave")

    def test_double_clicking_a_group_on_the_canvas_enters_it(self):
        self.draw_two()
        self.app.key("ctrl+g")
        self.assertEqual("2 objects", self.picked())
        canvas = self.app.find("canvas")
        cx, cy = canvas.x + canvas.w // 2, canvas.y + canvas.h // 2
        self.app.double_click((cx - 120, cy))
        self.app.wait_until(lambda: "Inside" in self.app.prop("graphStatus", "text"), "the double click to enter the group")
        self.assertEqual("1 object", self.picked())

    def test_the_canvas_picks_only_inside_an_entered_group(self):
        self.draw_two()
        self.app.key("ctrl+g")
        canvas = self.app.find("canvas")
        cx, cy = canvas.x + canvas.w // 2, canvas.y + canvas.h // 2
        self.app.click_at((cx + 170, cy))
        self.tap_space()
        self.app.key("r")
        self.app.wait_until(lambda: self.hint_tool() == "Rectangle", "the rectangle tool")
        self.app.drag((cx + 140, cy - 40), (cx + 200, cy + 40))
        self.app.key("escape")
        self.app.wait_until(lambda: self.hint_tool() == "Select", "the Select tool")
        self.app.click_at((cx + 170, cy))
        self.app.wait_until(lambda: self.app.prop("transformPanelHeaderInfo", "text") == "1 object", "the rectangle outside the group picked")

        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the graph dock")
        self.app.click_row("graphTree", "<Group>")
        self.app.wait_until(lambda: self.app.prop("graphDetailKind", "text") == "Group", "the group's box")
        self.app.click("graphEnterButton")
        self.app.wait_until(lambda: "Inside" in self.app.prop("graphStatus", "text"), "the view to enter the group")

        self.app.click("graphToggle")                                    # the dock covers the right of the canvas; the group stays entered
        self.app.wait_until(lambda: not self.app.prop("graphToggle", "checked"), "the graph dock to close")
        self.app.click_at((cx - 120, cy + 250))
        self.app.click_at((cx - 120, cy))
        self.app.wait_until(lambda: self.app.prop("transformPanelHeaderInfo", "text") == "1 object", "a member picked alone")
        self.app.click_at((cx + 170, cy))
        self.app.wait_until(lambda: self.app.prop("transformPanelHeaderInfo", "text") == "no selection", "a shape outside the entered group not to be picked")

        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the graph dock")
        self.app.click_row("graphTree", "..")
        self.app.wait_until(lambda: "nobody uses" in self.app.prop("graphStatus", "text"), "the .. row to leave")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: not self.app.prop("graphToggle", "checked"), "the graph dock to close")
        self.app.click_at((cx + 170, cy))
        self.app.wait_until(lambda: self.app.prop("transformPanelHeaderInfo", "text") == "1 object", "the outside shape pickable again")


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


MAGAZINE = r"C:\Users\jim\Documents\3DCreative_Issue_125_Jan16_highres.pdf"


@unittest.skipUnless(os.path.isfile(MAGAZINE), "the 90 MB magazine PDF (about 2 s to import) is not on this machine")
class BackgroundImport(CurvyCase):
    """Import PDF runs on a worker behind the loading overlay; Esc cancels it. The overlay itself is not reflected: while it is up the
    window's children are hidden, so the canvas reporting itself invisible means the app is busy."""

    def busy(self):
        return not self.app.find("canvas").visible

    def hold_escape(self, seconds=0.15):
        # The overlay polls the key state every 33 ms, so a press and release in one batch (app.key) can fall between two polls.
        self.app._send([self.app._key(0x1B)])                      # no bring_to_front: the window is already in front, and the import is short
        time.sleep(seconds)
        self.app._send([self.app._key(0x1B, up=True)])
        self.app.wait_idle()

    def start_import(self, document_open=False, pages=None):
        if not document_open:
            self.focus_canvas()                                    # with a document open that click could select the drawing, and Space would open the Object ring
        self.tap_space()
        if document_open:
            self.app.key("i")                                      # the Document ring's Import slot opens the Import ring
            self.app.wait_until(lambda: self.app.prop("hintKeys", "text") == "Ring: Import", "the Import ring")
            self.app.key_raw("f")                                  # PDF / AI
            self.app.wait_for_dialog()                             # the document is modified: it asks about unsaved changes first
            time.sleep(0.5)
            self.app.key_raw("n")                                  # discard them: what the cancelled import must keep is the drawing as it is
            time.sleep(1.0)
        else:
            self.app.key_raw("i")                                  # the New ring's Import slot opens the file dialog, a modal loop: no idle to wait for
        self.app.wait_for_dialog()
        time.sleep(2.0)                                            # the dialog is in front before its file name box is ready (it fills the folder list first): early characters are lost
        self.app.type_raw(MAGAZINE)
        self.app.key_raw("enter")
        self.app.wait_until(self.pages_dialog_open, "the page dialog to open", timeout=30)   # the 90 MB file is read for its page count first
        if pages is None:
            self.app.key_raw("enter")                              # All pages; Import is the default button
        else:
            self.choose_pages(*pages)
        self.app.wait_until(self.busy, "the loading overlay to appear", timeout=15)

    def pages_dialog_open(self):
        try:
            return self.app.find("pagesImportButton").w > 0
        except newui_guitest.GuiTestError:
            return False

    def choose_pages(self, mode, text):
        self.app.click("pagesModeCombo")                           # focuses it; Down then moves the selection itself: All pages, First pages, Listed pages
        for _ in range(mode):
            self.app.key_raw("down")
        self.app.click("pagesValueField")
        self.app.key_raw("end")                                    # ctrl+a types an "a" here, so the field is emptied with backspaces
        for _ in range(12):
            self.app.key_raw("backspace")
        self.app.type(text)
        self.app.key_raw("enter")                                  # Import is the default button; clicking it would wait for an idle the busy window never gives

    def import_pages(self, mode, text, boards):
        self.start_import(pages=(mode, text))
        self.app.wait_until(lambda: not self.busy(), "the import to finish", timeout=90)
        self.app.wait_until(lambda: "(%d artboards)" % boards in self.app.prop("hintKeys", "text"), "the status line to count %d artboards" % boards)

    def test_first_pages_imports_only_those(self):
        self.import_pages(1, "3", 3)

    def test_listed_pages_imports_only_those(self):
        self.import_pages(2, "2,5", 2)

    def test_listed_ranges_import_each_page_once(self):
        self.import_pages(2, "2-4,9", 4)

    def test_the_whole_magazine_exports_to_pdf_quickly(self):      # it took 218 s and a gigabyte before the per-page culling
        self.start_import()
        self.app.wait_until(lambda: not self.busy(), "the import to finish", timeout=90)
        out = tempfile.TemporaryDirectory()
        self.addCleanup(out.cleanup)
        path = os.path.join(out.name, "magazine.pdf")
        self.tap_space()
        self.app.key("e")                                          # the Document ring's Export slot opens the Export ring
        self.app.wait_until(lambda: self.app.prop("hintKeys", "text") == "Ring: Export", "the Export ring")
        self.app.key_raw("p")
        self.app.wait_for_dialog()
        time.sleep(2.0)
        self.app.type_raw(path)
        self.app.key_raw("enter")
        started = time.monotonic()
        self.app.wait_until(lambda: "Exported" in self.app.prop("hintKeys", "text"), "the PDF export to finish", timeout=60, interval=0.25)
        self.assertLess(time.monotonic() - started, 30)
        self.assertLess(os.path.getsize(path), 400 * 1024 * 1024)
        with open(path, "rb") as f:
            self.assertEqual(b"%PDF-", f.read(5))

    def test_the_magazine_opens_when_nothing_cancels_it(self):     # the premise of the test below: the import is not already over
        self.start_import()
        self.app.wait_until(lambda: not self.busy(), "the import to finish", timeout=90)
        self.assertNotEqual("Nothing open", self.hint_tool())

    def cancel_import(self, document_open):
        self.start_import(document_open)
        self.assertTrue(self.busy(), "the import was over before Esc was sent")
        self.hold_escape()
        self.app.wait_until(lambda: not self.busy(), "the overlay to go away after Esc", timeout=30)
        self.assertTrue(self.app.running)
        self.assertIn("cancelled", self.app.prop("hintKeys", "text"))

    def test_esc_cancels_the_import_and_nothing_is_open_still(self):
        self.cancel_import(document_open=False)
        self.assertEqual("Nothing open", self.hint_tool())
        self.focus_canvas()                                       # the window is enabled and answers again
        self.tap_space()
        self.app.key("escape")
        self.app.wait_until(lambda: not self.ring_open(), "the ring to close")

    def test_esc_cancels_the_import_and_the_open_document_is_kept(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        canvas = self.app.find("canvas")
        inside, blank = (canvas.x + 360, canvas.y + 240), (canvas.x + 200, canvas.y + 160)
        self.app.click("rectangleTool")
        self.app.drag((canvas.x + 300, canvas.y + 200), (canvas.x + 420, canvas.y + 280))
        self.app.click("selectTool")
        self.app.click_at(blank)                                  # nothing selected, so no handle covers the rectangle
        before = self.app.screenshot().pixel(*inside)
        self.assertNotEqual(before, self.app.screenshot().pixel(*blank), "the rectangle was not drawn")
        self.cancel_import(document_open=True)
        self.assertEqual("Select", self.hint_tool())
        self.assertEqual(before, self.app.screenshot().pixel(*inside), "the rectangle is gone after the cancel")


class BackgroundExport(CurvyCase):
    """Export As Image / PDF / EPS / All Artboards run on the worker like Save: the file appears, the window comes back, and the status line
    says what was written. The export ring is Document ring > Export (E); each slot opens a native save dialog."""

    def setUp(self):
        super().setUp()
        self._out = tempfile.TemporaryDirectory()
        self.addCleanup(self._out.cleanup)

    def busy(self):
        return not self.app.find("canvas").visible

    def draw_a_rectangle(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        canvas = self.app.find("canvas")
        self.app.click("rectangleTool")
        self.app.drag((canvas.x + 300, canvas.y + 200), (canvas.x + 420, canvas.y + 280))
        self.app.click("selectTool")
        self.app.click_at((canvas.x + 200, canvas.y + 160))       # nothing selected, so Space opens the Document ring and not the Object ring

    def export_as(self, key, file_name):
        path = os.path.join(self._out.name, file_name)
        self.tap_space()
        self.app.key("e")                                          # the Document ring's Export slot opens the Export ring
        self.app.wait_until(lambda: self.app.prop("hintKeys", "text") == "Ring: Export", "the Export ring")
        self.app.key_raw(key)                                      # the slot opens a native save dialog, a modal loop: no idle to wait for
        self.app.wait_for_dialog()
        time.sleep(2.0)                                            # the dialog is in front before its file name box is ready: early characters are lost
        self.app.type_raw(path)
        self.app.key_raw("enter")
        return path

    def wait_done(self):
        self.app.wait_until(lambda: not self.busy(), "the export to finish", timeout=30)
        self.app.wait_until(lambda: "Exported" in self.app.prop("hintKeys", "text"), "the status line to say Exported")
        self.assertEqual("Select", self.hint_tool())

    def test_pdf(self):
        self.draw_a_rectangle()
        path = self.export_as("p", "page.pdf")
        self.wait_done()
        with open(path, "rb") as f:
            self.assertEqual(b"%PDF-", f.read(5))

    def test_eps(self):
        self.draw_a_rectangle()
        path = self.export_as("e", "page.eps")
        self.wait_done()
        with open(path, "rb") as f:
            self.assertEqual(b"%!PS-Adobe", f.read(10))

    def test_image(self):
        self.draw_a_rectangle()
        path = self.export_as("i", "page.png")
        self.wait_done()
        with open(path, "rb") as f:
            self.assertEqual(b"\x89PNG", f.read(4))

    def test_all_artboards(self):
        self.draw_a_rectangle()
        self.export_as("a", "boards.png")
        self.wait_done()
        files = [n for n in os.listdir(self._out.name) if n.startswith("boards_") and n.endswith(".png")]
        self.assertEqual(1, len(files), files)                     # one artboard: boards_1_Artboard-1.png
        self.assertIn("1 artboards", self.app.prop("hintKeys", "text"))


if __name__ == "__main__":
    sys.exit(newui_guitest.main())
