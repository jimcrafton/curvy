r"""GUI check of scripts that run in the background: the status bar line, the Cancel and Stop buttons, Esc and the time limit, with real
mouse and keyboard.

    python D:\code\curvy\tests\gui\test_curvy_pybackground.py --exe D:\code\curvy\out\build\x64-release\curvy.exe

The scripts sleep in short steps (a Python-level loop), so a cancel is honoured within a tenth of a second.
"""
import os
import sys

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from curvyCase import CurvyCase

LONG_SCRIPT = "import time; [time.sleep(0.1) for _ in range(1000)]"


class BackgroundCase(CurvyCase):
    SETTINGS = CurvyCase.SCRIPT_PANELS_SETTINGS

    def setUp(self):
        self.make_settings_file(self.SETTINGS)
        super().setUp()

    def open_panels(self):
        self.new_a4()
        if self.app.find("rightArea").w <= 0:
            self.toggle_properties()
        self.app.wait_until(lambda: self.app.find("rightArea").w > 0, "the right dock")
        self.app.wait_idle()

    def start_from_console(self, text=LONG_SCRIPT):
        self.app.click("pyConsoleInput")
        self.app.type(text)
        self.app.key("enter")
        self.wait_running()

    def start_from_editor(self, text=LONG_SCRIPT):
        self.app.click("pyEditorText")
        self.app.type(text)
        self.app.key("ctrl+enter")
        self.wait_running()

    def wait_running(self):
        self.app.wait_until(lambda: "running..." in self.transcript(), "the script to start")

    def transcript(self):
        return self.app.text_of("pyConsoleOutput")

    def status(self):
        return self.app.prop("hintKeys", "text")


class PyBackground(BackgroundCase):
    def test_the_status_bar_counts_the_time_and_says_when_the_script_ended(self):
        self.open_panels()
        self.start_from_console("import time; time.sleep(9)")
        self.assertNotIn("A script is running", self.status(), "a script gets the line only once it has run for a moment")
        self.app.wait_until(lambda: "A script is running" in self.status(), "the status line", timeout=6)
        first = self.status()
        self.app.wait_until(lambda: "A script is running" in self.status() and self.status() != first, "the line to be refreshed", timeout=8)
        self.wait_script_done()
        self.app.wait_until(lambda: "Script ended after" in self.status(), "the end to be reported")

    def test_a_short_script_leaves_the_status_bar_alone(self):
        self.open_panels()
        before = self.status()
        self.start_from_console("print(1)")
        self.wait_script_done()
        self.assertEqual(self.status(), before)

    def test_the_cancel_button_shows_while_a_script_runs_and_stops_it(self):
        self.open_panels()
        self.assertFalse(self.app.find("pyConsoleCancel").visible, "hidden until a script runs")
        self.start_from_console()
        self.app.wait_until(lambda: self.app.find("pyConsoleCancel").visible, "the Cancel button")
        self.app.click("pyConsoleCancel")
        self.wait_script_done()
        self.assertIn("Script cancelled", self.transcript())
        self.app.wait_until(lambda: not self.app.find("pyConsoleCancel").visible, "the Cancel button to go")

    def test_the_editor_run_button_becomes_stop_and_stops_the_script(self):
        self.open_panels()
        self.assertEqual(self.app.prop("pyEditorRun", "text"), "Run")
        self.start_from_editor()
        self.app.wait_until(lambda: self.app.prop("pyEditorRun", "text") == "Stop", "the Run button to read Stop")
        self.app.click("pyEditorRun")
        self.wait_script_done()
        self.assertIn("Script cancelled", self.transcript())
        self.app.wait_until(lambda: self.app.prop("pyEditorRun", "text") == "Run", "the button to read Run again")

    def test_a_second_run_while_one_is_running_does_not_start(self):
        self.open_panels()
        self.start_from_editor()
        self.app.click("pyEditorRun")                                  # reads Stop now: this is a stop, not a second run
        self.wait_script_done()
        self.assertEqual(self.transcript().count(">>> run"), 1)

    def test_esc_cancels_the_running_script(self):
        self.open_panels()
        self.start_from_console()
        self.focus_canvas()
        self.app.key("esc")
        self.wait_script_done()
        self.assertIn("Script cancelled", self.transcript())

    def test_edits_made_before_the_cancel_stay_and_one_undo_takes_them_back(self):
        self.open_panels()
        self.start_from_console("import time; layer = doc.add_layer('Long'); [(layer.add_circle(10 * i, 10, 5), time.sleep(0.1)) for i in range(1000)]")
        self.app.wait_until(lambda: self.app.find("pyConsoleCancel").visible, "the Cancel button")
        self.app.click("pyConsoleCancel")
        self.wait_script_done()
        self.app.click("pyConsoleInput")
        self.app.type("print(len(doc.layers))")
        self.app.key("enter")
        self.wait_script_done()
        self.assertTrue(self.transcript().rstrip().endswith("1"), self.transcript())
        self.app.click("pyConsoleInput")
        self.app.type("doc.undo(); print(len(doc.layers))")                 # the cancelled run is one undo step
        self.app.key("enter")
        self.wait_script_done()
        self.assertTrue(self.transcript().rstrip().endswith("0"), self.transcript())


class PyBackgroundTimeout(BackgroundCase):
    SETTINGS = "{ panels: { pyeditor: {}, pyconsole: {}, document: { collapsed: true }, node: { collapsed: true }, transform: { collapsed: true }, assets: { collapsed: true } }, scriptEditor: { scriptTimeout: true, scriptTimeoutSeconds: 2 } }"

    def test_a_script_past_the_time_limit_is_stopped_and_says_so(self):
        self.open_panels()
        self.start_from_console()
        self.wait_script_done(timeout=10)
        self.assertIn("Script stopped: it ran longer than 2 seconds", self.transcript())


if __name__ == "__main__":
    newui_guitest.main()
