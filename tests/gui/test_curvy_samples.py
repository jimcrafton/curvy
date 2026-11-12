r"""GUI check of the layer and group sample files: a sound one opens quietly, a damaged one asks before it is repaired.

    python D:\code\curvy\tests\gui\test_curvy_samples.py --exe D:\code\curvy\out\build\x64-release\curvy.exe

The app opens copies in a temporary folder through the real Open dialog, with --settings on a temporary file, so neither the samples nor
the user's settings are touched. The repair question is a native message box: Y answers Yes, N answers No.
"""
import ctypes
import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from test_curvy_gui import CurvyCase

SAMPLES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Resources", "samples")
OBJECT_XFORM = "object:xform"     # every layer and group has one; the damaged sample is missing Group2's


class Samples(CurvyCase):
    sample = None

    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.addCleanup(self._dir.cleanup)
        self.settings_file = os.path.join(self._dir.name, "settings.json5")
        self.path = os.path.join(self._dir.name, self.sample)
        shutil.copyfile(os.path.join(SAMPLES, self.sample), self.path)
        super().setUp()

    def launch(self):
        return newui_guitest.App(newui_guitest.testing.Settings.exe or self.exe, ("--settings", self.settings_file),
                                 cwd=newui_guitest.testing.Settings.cwd)

    def key_on_canvas(self, chord, raw=False):
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + canvas.w - 40, canvas.y + canvas.h - 40))
        self.tap_space()
        (self.app.key_raw if raw else self.app.key)(chord)

    def open_sample(self):
        """Starts the open and leaves it there: the repair question is modal, so nothing may wait for idle."""
        self.key_on_canvas("o", raw=True)
        self.app.wait_for_dialog()
        time.sleep(1.0)    # typing at once loses the first characters of the path
        self.app.type_raw(self.path)
        self.app.key_raw("enter")

    def question_is_up(self):
        cls, ours = self.app.foreground_class()
        return cls == "#32770" and ours

    @staticmethod
    def foreground_title():
        window = ctypes.windll.user32.GetForegroundWindow()
        buffer = ctypes.create_unicode_buffer(256)
        ctypes.windll.user32.GetWindowTextW(window, buffer, 256)
        return buffer.value

    def wait_for_the_question(self):
        self.app.wait_until(lambda: self.foreground_title() == "Damaged objects", "the repair question")

    def doc_name(self):
        return self.app.prop("documentName", "text")

    def wait_until_open(self):
        self.app.wait_until(lambda: self.doc_name() not in ("", "Untitled"), "the sample to open")

    def xforms_in_file(self):
        with open(self.path, encoding="utf-8") as f:
            return f.read().count(OBJECT_XFORM)

    def save(self):
        self.app.wait_idle()
        self.key_on_canvas("s")
        self.app.wait_idle()


class SoundSample(Samples):
    sample = "layersAndGroups.curvy"

    def test_it_opens_without_a_question(self):
        self.open_sample()
        self.app.wait_until(lambda: not self.question_is_up(), "the Open dialog to close")
        self.wait_until_open()
        self.assertFalse(self.question_is_up(), "a dialog came up for a sound file")
        self.save()
        self.assertEqual(self.xforms_in_file(), 7)


class DamagedSampleRepaired(Samples):
    sample = "layersAndGroupsDamaged.curvy"

    def test_yes_repairs_it(self):
        self.open_sample()
        self.wait_for_the_question()
        self.app.key_raw("y")
        self.wait_until_open()
        self.save()
        self.assertEqual(self.xforms_in_file(), 7)


class DamagedSampleLeftAlone(Samples):
    sample = "layersAndGroupsDamaged.curvy"

    def test_no_leaves_it_as_it_was(self):
        self.open_sample()
        self.wait_for_the_question()
        self.app.key_raw("n")
        self.wait_until_open()
        self.save()
        self.assertEqual(self.xforms_in_file(), 6)


if __name__ == "__main__":
    newui_guitest.main()
