r"""The vocabulary shared by curvy's GUI test suites (real mouse and keyboard through newui's automation pipe)."""
import os
import sys
import tempfile

sys.path.insert(0, r"D:\code\newui\tools\guitest")

import newui_guitest
from newui_guitest import AppTestCase

WAIT_SECONDS = 10.0                                                # the harness default is 30; a stale test should fail fast


class CurvyCase(AppTestCase):
    def setUp(self):
        # A settings folder of its own per test, so nothing a test saves reaches another test or the user's own settings.
        self._appdata = tempfile.TemporaryDirectory()
        self.addCleanup(self._appdata.cleanup)
        previous = os.environ.get("APPDATA")
        os.environ["APPDATA"] = self._appdata.name
        self.addCleanup(lambda: os.environ.__setitem__("APPDATA", previous) if previous is not None else os.environ.pop("APPDATA", None))

        original = newui_guitest.App.wait_until
        self.addCleanup(setattr, newui_guitest.App, "wait_until", original)
        newui_guitest.App.wait_until = lambda app, cond, what="the condition", timeout=WAIT_SECONDS, **kw: original(app, cond, what, timeout, **kw)
        super().setUp()

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
