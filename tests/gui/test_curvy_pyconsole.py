r"""GUI check of the Python console panel in the right dock: real typing into its input line.

    python D:\code\curvy\tests\gui\test_curvy_pyconsole.py --exe D:\code\curvy\out\build\x64-release\curvy.exe
"""
import os
import sys

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from curvyCase import CurvyCase


class PyConsole(CurvyCase):
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

    def test_a_typed_line_runs_and_prints(self):
        self.open_console()
        self.run_line("print(6 * 7)")
        self.assertIn(">>> print(6 * 7)", self.transcript())
        self.assertIn("42", self.transcript())

    def test_an_error_shows_the_traceback_and_the_panel_keeps_working(self):
        self.open_console()
        self.run_line("1/0")
        self.assertIn("ZeroDivisionError", self.transcript())
        self.run_line("print('still here')")
        self.assertIn("still here", self.transcript())

    def test_a_script_edits_the_document_and_the_page_size_follows(self):
        self.open_console()
        self.run_line("doc.set_page_size(300, 400)")
        self.run_line("print(doc.page_width, doc.page_height)")
        self.assertIn("300.0 400.0", self.transcript())

    def test_up_arrow_recalls_the_last_line(self):
        self.open_console()
        self.run_line("x = 5")
        self.app.click("pyConsoleInput")
        self.app.key("up")
        self.app.wait_idle()
        self.assertEqual(self.app.text_of("pyConsoleInput"), "x = 5")


if __name__ == "__main__":
    newui_guitest.main()
