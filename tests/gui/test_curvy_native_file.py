r"""GUI tests for the native .curvy file through the real file dialogs: Save asks for a path, Save then writes in place, Open reads it back.

    python D:\code\curvy\tests\gui\test_curvy_native_file.py --exe D:\code\curvy\out\build\x64-release\curvy.exe

Edits made in the graph and saved are covered by test_curvy_graphview.py; the sample files by test_curvy_samples.py.
"""
import os
import re
import shutil
import sys
import tempfile
import time

from curvyCase import CurvyCase
import newui_guitest

SAMPLES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Resources", "samples")


class NativeFile(CurvyCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.addCleanup(self._dir.cleanup)
        self.settings_file = os.path.join(self._dir.name, "settings.json5")
        self.path = os.path.join(self._dir.name, "round.curvy")
        super().setUp()

    def launch(self):
        return newui_guitest.App(newui_guitest.testing.Settings.exe or self.exe, ("--settings", self.settings_file),
                                 cwd=newui_guitest.testing.Settings.cwd)

    def key_on_canvas(self, chord, raw=False):
        """Space off the page opens the Document ring, whose slots are the file commands (S Save, O Open)."""
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + canvas.w - 40, canvas.y + canvas.h - 40))
        self.tap_space()
        (self.app.key_raw if raw else self.app.key)(chord)       # a key that opens a modal dialog must not wait for idle

    def dialog_path(self, chord, path):
        self.key_on_canvas(chord, raw=True)
        self.app.wait_for_dialog()
        time.sleep(1.0)                                          # typing at once loses the first characters of the path
        self.app.type_raw(path)
        self.app.key_raw("enter")
        self.app.wait_idle()

    def doc_name(self):
        return self.app.prop("documentName", "text")

    def read(self, path=None):
        """The file's text without its saved-at time, which differs on every save."""
        with open(path or self.path, encoding="utf-8") as f:
            return re.sub(r'"\d{4}-\d\d-\d\dT[\d:]+Z"', '"time"', f.read())

    def save_untitled(self):
        self.new_a4()
        self.dialog_path("s", self.path)                         # untitled: Save asks for a path
        self.app.wait_until(lambda: self.hint_keys() == "Saved round.curvy", "the status to say it saved")
        self.assertGreater(os.path.getsize(self.path), 0)


class SaveAndOpen(NativeFile):
    def test_save_asks_for_a_path_and_writes_a_curvy_file(self):
        self.save_untitled()
        self.assertIn("version", self.read())
        self.assertEqual("A4", self.doc_name(), "the document keeps its own name; only the file has the new one")

    def test_save_of_a_titled_document_writes_in_place_without_a_dialog(self):
        self.save_untitled()
        first = self.read()
        os.remove(self.path)
        self.key_on_canvas("s")                                  # no dialog may come up, so this waits for idle as usual
        self.app.wait_until(lambda: os.path.exists(self.path), "Save to write the file again")
        self.app.wait_idle()
        self.assertEqual(first, self.read())

    def test_opening_the_saved_file_and_saving_again_gives_the_same_text(self):
        self.save_untitled()
        first = self.read()
        self.dialog_path("o", self.path)
        self.app.wait_until(lambda: self.hint_keys() == "Opened round.curvy", "the file to open")
        self.key_on_canvas("s")
        self.app.wait_idle()
        self.assertEqual(first, self.read())

    def test_opening_a_sample_replaces_the_open_document(self):
        sample = os.path.join(self._dir.name, "layersAndGroups.curvy")
        shutil.copyfile(os.path.join(SAMPLES, "layersAndGroups.curvy"), sample)
        self.new_a4()
        self.dialog_path("o", sample)
        self.app.wait_until(lambda: self.hint_keys() == "Opened layersAndGroups.curvy", "the sample to open")
        self.assertNotEqual("A4", self.doc_name(), "the sample's own document, not the A4 that was open")

    def test_a_file_that_is_not_a_document_is_refused_and_the_open_one_is_kept(self):
        bad = os.path.join(self._dir.name, "notes.curvy")
        with open(bad, "w", encoding="utf-8") as f:
            f.write("this is not a curvy document")
        self.new_a4()
        name = self.doc_name()
        self.dialog_path("o", bad)
        self.app.wait_until(lambda: self.hint_keys().startswith("Could not open notes.curvy"), "the refusal")
        self.assertEqual(name, self.doc_name())
        self.assertEqual("Select", self.hint_tool(), "the document that was open is still there")


if __name__ == "__main__":
    sys.exit(newui_guitest.main())
