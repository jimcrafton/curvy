r"""GUI check of the Python script editor panel (right dock): typing, running, find and replace with real keys and clicks.

    python D:\code\curvy\tests\gui\test_curvy_pyeditor.py --exe D:\code\curvy\out\build\x64-release\curvy.exe
"""
import os
import sys

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from curvyCase import CurvyCase


class PyEditor(CurvyCase):
    def setUp(self):
        self.make_settings_file(self.SCRIPT_PANELS_SETTINGS)
        super().setUp()

    def open_editor(self):
        self.new_a4()
        if self.app.find("rightArea").w <= 0:
            self.toggle_properties()
        self.app.wait_until(lambda: self.app.find("rightArea").w > 0, "the right dock")
        self.app.wait_idle()

    def type_script(self, *lines):
        self.app.click("pyEditorText")
        for index, line in enumerate(lines):
            if index > 0:
                self.app.key("enter")
            self.app.type(line)
        self.app.wait_idle()

    def script(self):
        return self.app.text_of("pyEditorText")

    def find_status(self):
        return self.app.prop("pyEditorFindStatus", "text")

    def test_ctrl_enter_runs_the_script_and_the_console_shows_the_output(self):
        self.open_editor()
        self.type_script("print(6 * 7)")
        self.app.key("ctrl+enter")
        self.app.wait_idle()
        self.assertIn("42", self.app.text_of("pyConsoleOutput"))

    def test_ctrl_f_finds_every_match_and_escape_closes_the_bar(self):
        self.open_editor()
        self.type_script("cat dog cat", "bird cat")
        self.app.key("ctrl+f")
        self.app.wait_until(lambda: self.app.find("pyEditorFind").w > 0, "the find field")
        self.app.type("cat")
        self.app.wait_idle()
        self.assertEqual(self.find_status(), "1 of 3")
        self.app.key("enter")
        self.app.wait_idle()
        self.assertEqual(self.find_status(), "2 of 3")
        self.app.key("shift+enter")
        self.app.wait_idle()
        self.assertEqual(self.find_status(), "1 of 3")
        self.app.key("esc")
        self.app.wait_idle()
        self.assertEqual(self.find_status(), "")

    def test_replace_all_changes_every_match_and_one_undo_brings_them_back(self):
        self.open_editor()
        self.type_script("cat dog cat")
        self.app.key("ctrl+h")
        self.app.wait_until(lambda: self.app.find("pyEditorReplace").w > 0, "the replace field")
        self.app.type("cat")
        self.app.click("pyEditorReplace")
        self.app.type("bird")
        self.app.click("pyEditorReplaceAll")
        self.app.wait_idle()
        self.assertEqual(self.script(), "bird dog bird")
        self.app.click("pyEditorText")
        self.app.key("ctrl+z")
        self.app.wait_idle()
        self.assertEqual(self.script(), "cat dog cat")


if __name__ == "__main__":
    newui_guitest.main()
