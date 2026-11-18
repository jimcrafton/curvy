r"""The vocabulary shared by curvy's GUI test suites (real mouse and keyboard through newui's automation pipe)."""
import os
import sys
import tempfile

sys.path.insert(0, r"D:\code\newui\tools\guitest")

import newui_guitest
from newui_guitest import AppTestCase

WAIT_SECONDS = 10.0                                                # the harness default is 30; a stale test should fail fast


class CurvyCase(AppTestCase):
    def launch(self):
        return newui_guitest.App(newui_guitest.testing.Settings.exe or self.exe, ("--settings", self.settings_file), cwd=newui_guitest.testing.Settings.cwd)

    # The default window is too short for every panel of the right dock to show at once (the lower ones fall below the window), so a suite
    # that works in the script panels puts them on top and starts the other panels collapsed.
    SCRIPT_PANELS_SETTINGS = "{ panels: { pyeditor: {}, pyconsole: {}, document: { collapsed: true }, node: { collapsed: true }, transform: { collapsed: true }, assets: { collapsed: true } } }"

    def make_settings_file(self, text=""):
        self._settings_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self._settings_dir.cleanup)
        self.settings_file = os.path.join(self._settings_dir.name, "settings.json5")
        if text:
            with open(self.settings_file, "w") as f:
                f.write(text)

    def setUp(self):
        # A settings file of its own per test (the app keeps the editor's scratch.py next to it), so nothing a test saves or types reaches
        # another test or the user's own settings. A suite that made its own file before calling this keeps it.
        if getattr(self, "settings_file", None) is None:
            self.make_settings_file()

        original = newui_guitest.App.wait_until
        self.addCleanup(setattr, newui_guitest.App, "wait_until", original)
        newui_guitest.App.wait_until = lambda app, cond, what="the condition", timeout=WAIT_SECONDS, **kw: original(app, cond, what, timeout, **kw)
        super().setUp()

    def wait_script_done(self, timeout=WAIT_SECONDS):
        """Scripts run in the background: the console shows 'running...' until the result arrives."""
        self.app.wait_until(lambda: "running..." not in self.app.text_of("pyConsoleOutput"), "the script to finish", timeout)
        self.app.wait_idle()

    def ring_open(self):
        return self.app.find("radialMenu").visible

    def hint_tool(self):
        return self.app.prop("hintTool", "text")

    def hint_keys(self):
        return self.app.prop("hintKeys", "text")

    def focus_canvas(self):
        self.app.click("canvas")

    def tap_space(self):
        self.app.key("space")
        self.app.wait_until(self.ring_open, "the ring to open")

    def new_a4(self):
        self.focus_canvas()
        self.tap_space()
        self.app.key("2")                                          # the New ring's second slot
        self.app.wait_until(lambda: self.hint_tool() == "Select", "a document to open")

    def toggle_properties(self):
        """Shows or hides the right dock through the View ring (the top bar's Panels button opens a native menu that blocks). It needs a
        document open and nothing selected, with the pointer on the pasteboard so the Document ring comes up."""
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + 40, canvas.y + canvas.h - 40))
        self.tap_space()
        self.app.key("v")
        self.app.wait_until(lambda: self.hint_keys() == "Ring: View", "the View ring")
        self.app.key("p")
        self.app.wait_until(lambda: not self.ring_open(), "the ring to close")
