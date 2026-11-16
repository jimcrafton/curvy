r"""GUI check of doc.save() and doc.open() from the Python console: the window shows what a script opened and the files really land on disk.

    python D:\code\curvy\tests\gui\test_curvy_pyfiles.py --exe D:\code\curvy\out\build\x64-release\curvy.exe
"""
import os
import sys
import tempfile

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from curvyCase import CurvyCase

SAMPLE = r"D:\code\curvy\Resources\samples\layersAndGroups.curvy"


class PyFiles(CurvyCase):
    def setUp(self):
        self.make_settings_file(self.SCRIPT_PANELS_SETTINGS)
        super().setUp()
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)

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
        self.wait_script_done()

    def transcript(self):
        return self.app.text_of("pyConsoleOutput")

    def status(self):
        return self.app.prop("hintKeys", "text")

    def forward(self, path):
        return path.replace("\\", "/")

    def test_a_script_opens_a_file_and_the_window_follows(self):
        self.open_console()
        self.run_line("doc.open('%s', discard_changes=True)" % self.forward(SAMPLE))
        self.assertIn("A script opened layersAndGroups.curvy", self.status())
        self.run_line("print([l.name for l in doc.layers], doc.path.endswith('layersAndGroups.curvy'), doc.is_modified)")
        self.assertTrue(self.transcript().splitlines()[-1].endswith("True False"), self.transcript())
        self.assertNotIn("Error", self.transcript())

    def test_a_script_saves_a_file_that_opens_again_with_its_layers(self):
        self.open_console()
        target = self.forward(os.path.join(self.folder.name, "saved.curvy"))
        self.run_line("doc.add_layer('FromScript')")
        self.run_line("print(doc.save('%s') == '%s', doc.is_modified)" % (target, target))
        self.assertIn("True False", self.transcript())
        self.assertTrue(os.path.exists(target.replace("/", os.sep)))
        self.run_line("doc.open('%s')" % target)
        self.run_line("print([l.name for l in doc.layers])")
        self.assertIn("FromScript", self.transcript().splitlines()[-1])

    def test_open_refuses_unsaved_changes_and_says_how_to_go_on(self):
        self.open_console()
        self.run_line("doc.add_layer('Unsaved')")
        self.run_line("doc.open('%s')" % self.forward(SAMPLE))
        self.assertIn("unsaved changes", self.transcript())
        self.run_line("print([l.name for l in doc.layers])")
        self.assertIn("Unsaved", self.transcript().splitlines()[-1])


if __name__ == "__main__":
    newui_guitest.main()
