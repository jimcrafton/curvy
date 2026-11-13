r"""GUI check of unsaved changes: the title star, the Save/Don't Save/Cancel question when the window is closed, and a close that is
refused while a long save is still running.

    python D:\code\curvy\tests\gui\test_curvy_unsaved.py --exe D:\code\curvy\out\build\x64-debug\curvy.exe

The document is large (tens of thousands of nodes, a save takes seconds). curvyTests.exe, next to curvy.exe, writes it once per run
(LargeDocument.DISABLED_WriteTheLargeFile); the file opens with the graph view up and the first layer selected, so the edit is a rename
in the property panel and no test needs to know where a box is.
"""
import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from test_curvy_gui import CurvyCase
from test_curvy_samples import Samples

WM_CLOSE = 0x0010
QUESTION = "Unsaved changes"
user32 = ctypes.windll.user32


class UnsavedChanges(Samples):
    sample = "largeDocument.curvy"
    _source_dir = None

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls._source_dir = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls._source_dir.cleanup)
        cls.source = os.path.join(cls._source_dir.name, cls.sample)
        exe = newui_guitest.testing.Settings.exe or cls.exe
        tests = os.path.join(os.path.dirname(exe), "curvyTests.exe")
        subprocess.run([tests, "--gtest_also_run_disabled_tests", "--gtest_filter=LargeDocument.DISABLED_WriteTheLargeFile"],
                       env=dict(os.environ, CURVY_LARGE_FILE=cls.source), check=True, capture_output=True, timeout=300)

    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.addCleanup(self._dir.cleanup)
        self.settings_file = os.path.join(self._dir.name, "settings.json5")
        self.path = os.path.join(self._dir.name, self.sample)
        shutil.copyfile(self.source, self.path)
        CurvyCase.setUp(self)
        self.open_large()
        self.window = self.app._main_window()

    def open_large(self):
        self.open_sample()
        self.app.wait_until(lambda: not self.question_is_up(), "the Open dialog to close")
        self.app.wait_until(lambda: self.doc_name() not in ("", "Untitled"), "the document to open", timeout=60)
        self.app.wait_until(lambda: self.app.find("graphNameField").visible, "the graph panel (the file opens with the graph up)", timeout=60)
        self.app.wait_until(lambda: self.app.text_of("graphNameField") == "Layer1", "the first layer to be selected", timeout=60)
        self.app.wait_idle()

    def rename_layer(self):
        self.app.click("graphNameField")
        self.app.key("ctrl+a")
        self.app.type("Renamed")
        self.app.key("enter")
        self.app.wait_idle()

    def title(self):
        buffer = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(self.window, buffer, 256)
        return buffer.value

    def close_window(self):
        user32.PostMessageW(self.window, WM_CLOSE, 0, 0)

    def window_is_gone(self):
        return not user32.IsWindow(self.window)

    def wait_for_the_close_question(self):
        self.app.wait_until(lambda: self.foreground_title() == QUESTION, "the unsaved changes question")

    def name_in_file(self):
        with open(self.path, encoding="utf-8") as f:
            return "Renamed" in f.read()

    def test_the_title_gets_a_star_after_an_edit(self):
        self.assertTrue(self.title().startswith("curvy - Large"), self.title())
        self.assertFalse(self.title().endswith("*"), "a document that was just opened is not modified")
        self.rename_layer()
        self.assertTrue(self.title().endswith(" *"), self.title())

    def test_closing_an_unedited_document_does_not_ask(self):
        self.close_window()
        self.app.wait_until(self.window_is_gone, "the window to close")

    def test_cancel_keeps_the_window_and_the_edit(self):
        self.rename_layer()
        self.close_window()
        self.wait_for_the_close_question()
        self.app.key_raw("escape")
        self.app.wait_until(lambda: self.foreground_title() != QUESTION, "the question to go away")
        self.assertFalse(self.window_is_gone())
        self.assertTrue(self.title().endswith(" *"), self.title())
        self.assertFalse(self.name_in_file())

    def test_no_closes_without_saving(self):
        self.rename_layer()
        self.close_window()
        self.wait_for_the_close_question()
        self.app.key_raw("n")
        self.app.wait_until(self.window_is_gone, "the window to close")
        self.assertFalse(self.name_in_file())

    def test_yes_saves_and_then_closes(self):
        self.rename_layer()
        self.close_window()
        self.wait_for_the_close_question()
        self.app.key_raw("y")
        self.app.wait_until(self.window_is_gone, "the save and the close", timeout=120)
        self.assertTrue(self.name_in_file())

    def test_closing_is_refused_while_a_save_runs_and_works_after_it(self):
        self.rename_layer()
        self.app.click("graphToggle")                      # with the graph up the Space ring is the node ring and swallows S
        self.app.wait_idle()
        self.key_on_canvas("s", raw=True)
        self.app.wait_until(lambda: "Saving" in self.app.prop("hintKeys", "text"), "the save to start", timeout=10, interval=0.01)
        self.close_window()
        time.sleep(0.5)
        self.assertFalse(self.window_is_gone(), "the window closed during the save")
        self.assertNotEqual(QUESTION, self.foreground_title(), "the question came up during the save")
        self.assertIn("Saving", self.app.prop("hintKeys", "text"), "the save ended before the close was tried; the document is too small")
        self.app.wait_until(lambda: "Saved" in self.app.prop("hintKeys", "text"), "the save to finish", timeout=120)
        self.assertTrue(self.name_in_file())
        self.assertFalse(self.title().endswith("*"), self.title())
        self.close_window()
        self.app.wait_until(self.window_is_gone, "the window to close once the save is over")


if __name__ == "__main__":
    newui_guitest.main()
