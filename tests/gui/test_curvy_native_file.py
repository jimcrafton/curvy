r"""N5 GUI check of the native .curvy file: Save As, Save, Open through the real file dialogs.

    python D:\code\curvy\tests\gui\test_curvy_native_file.py --exe D:\code\curvy\out\build\x64-release\curvy.exe
"""
import ctypes
import ctypes.wintypes
import os
import sys
import tempfile
import time

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from test_curvy_gui import CurvyCase


class NativeFile(CurvyCase):
    def setUp(self):
        super().setUp()
        self._dir = tempfile.TemporaryDirectory()
        self.addCleanup(self._dir.cleanup)
        self.path = os.path.join(self._dir.name, "n5check.curvy")

    def draw_rectangle(self, at):
        canvas = self.app.find("canvas")
        self.app.click("rectangleTool")
        self.app.drag((canvas.x + at, canvas.y + 200), (canvas.x + at + 120, canvas.y + 280))
        self.app.click("selectTool")

    def key_on_canvas(self, chord, raw=False):
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + canvas.w - 40, canvas.y + canvas.h - 40))   # off the page: gives the canvas the focus a toolbar button took
        self.tap_space()                                           # off every artboard Space opens the Document ring
        # A key that opens a modal dialog must not wait for idle or take the foreground.
        (self.app.key_raw if raw else self.app.key)(chord)

    def dialog_path(self, chord):
        self.key_on_canvas(chord, raw=True)
        self.app.wait_for_dialog()
        self.app.type_raw(self.path)
        self.app.key_raw("enter")
        self.app.wait_idle()

    def doc_name(self):
        return self.app.prop("documentName", "text")

    def test_save_as_save_and_open_round_trip(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        self.draw_rectangle(300)

        self.dialog_path("s")                                 # untitled: Save asks for a path
        self.app.wait_until(lambda: os.path.exists(self.path), "the file to be written")
        self.app.wait_until(lambda: self.doc_name() == "n5check.curvy", "the name to follow Save As")
        with open(self.path, encoding="utf-8") as f:
            first = f.read()
        self.assertIn("version", first)
        size1 = len(first)

        self.draw_rectangle(500)
        self.key_on_canvas("s")                               # titled: saves without a dialog
        self.app.wait_until(lambda: os.path.getsize(self.path) > size1, "Save to rewrite the file")
        self.app.wait_idle()
        with open(self.path, encoding="utf-8") as f:
            second = f.read()

        self.dialog_path("o")                                 # reopen the same file over the saved document
        self.app.wait_idle()
        self.app.wait_until(lambda: "n5check.curvy" in self.doc_name(), "the reopened document")
        self.key_on_canvas("s")                               # an unmodified reopen resaves to identical text
        self.app.wait_idle()
        with open(self.path, encoding="utf-8") as f:
            self.assertEqual(second, f.read())


class LayerReorder(NativeFile):
    """Back / Forward in the graph dock reorder layers of the sample document Resources/samples/layerStack.curvy."""

    def setUp(self):
        super().setUp()
        self.path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Resources", "samples", "layerStack.curvy")
        self.path = os.path.normpath(self.path)

    def dialog_path(self, chord):
        self.key_on_canvas(chord, raw=True)
        self.app.wait_for_dialog()
        time.sleep(1.0)                                       # the edit box takes keys a moment after the dialog shows; the first letter was lost
        self.app.type_raw(self.path)
        self.app.key_raw("enter")
        self.app.wait_idle()

    def select_layer_row(self, title):
        tree = self.app.find("graphTree")
        seen = []
        for i in range(int(tree.h // 22)):
            self.app.click_at((tree.x + 120, tree.y + i * 22 + 11))
            seen.append((self.app.prop("graphDetailTitle", "text"), self.app.prop("graphDetailKind", "text")))
            if seen[-1] == (title, "Layer"):
                return
        self.fail("no Layer row named %s; saw %s" % (title, seen))

    def test_back_and_forward_reorder_layers_and_undo_restores_them(self):
        self.new_a4()
        self.dialog_path("o")
        self.app.wait_until(lambda: "layerStack" in self.doc_name(), "the sample document")
        self.app.click("graphToggle")
        self.app.wait_until(lambda: self.app.prop("graphToggle", "checked"), "the toggle to check")
        self.select_layer_row("Discs")
        self.assertTrue(self.app.prop("graphBackButton", "enabled"))
        self.assertFalse(self.app.prop("graphForwardButton", "enabled"), "Discs is the front layer")
        self.app.click("graphBackButton")
        self.app.wait_until(lambda: self.app.prop("graphForwardButton", "enabled"), "Discs to move back")
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: not self.app.prop("graphForwardButton", "enabled"), "undo to put Discs in front again")

        self.select_layer_row("Green")                         # between Red and Blue
        self.assertTrue(self.app.prop("graphBackButton", "enabled"))
        self.assertTrue(self.app.prop("graphForwardButton", "enabled"))
        self.app.click("graphBackButton")                      # now the first layer above the base layer's objects
        self.app.wait_until(lambda: not self.app.prop("graphBackButton", "enabled"), "Green to stop at the base layer's objects")
        state = {n: (self.app.prop(n, "text"), self.app.prop(n, "visible"), self.app.prop(n, "enabled"))
                 for n in ("graphStatus", "graphDetailTitle", "graphDetailKind", "graphBackButton", "graphForwardButton")}
        self.assertTrue(self.app.prop("graphForwardButton", "enabled"), state)
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: self.app.prop("graphBackButton", "enabled"), "undo to put Green back between Red and Blue")


class Recovery(NativeFile):
    """Autosave writes a .curvy copy; after a crash the next start offers it and Yes brings the document back."""

    def setUp(self):
        previous = os.environ.get("CURVY_AUTOSAVE_SECONDS")
        os.environ["CURVY_AUTOSAVE_SECONDS"] = "2"                  # the test hook: a tick and a gap of 2 seconds
        self.addCleanup(lambda: os.environ.__setitem__("CURVY_AUTOSAVE_SECONDS", previous) if previous is not None
                        else os.environ.pop("CURVY_AUTOSAVE_SECONDS", None))
        super().setUp()
        self.folder = os.path.join(self._appdata.name, "curvy", "autosave")

    def our_dialog(self):
        """The handle of a visible standard dialog ("#32770") belonging to the app, or 0."""
        found = []
        user32 = ctypes.windll.user32

        @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
        def visit(hwnd, _):
            name = ctypes.create_unicode_buffer(64)
            user32.GetClassNameW(hwnd, name, 64)
            pid = ctypes.c_ulong(0)
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if name.value == "#32770" and pid.value == self.app.pid and user32.IsWindowVisible(hwnd):
                found.append(hwnd)
            return True

        user32.EnumWindows(visit, 0)
        return found[0] if found else 0

    def copies(self):
        return [f for f in os.listdir(self.folder) if f.endswith((".curvy", ".svg"))] if os.path.isdir(self.folder) else []

    def test_a_crash_leaves_a_curvy_copy_that_the_next_start_recovers(self):
        self.new_a4()
        self.app.click("toolbarToggle")
        self.draw_rectangle(300)
        self.app.wait_until(lambda: any(f.endswith(".curvy") for f in self.copies()), "the autosave copy", timeout=20)
        self.assertEqual([], [f for f in self.copies() if f.endswith(".svg")])
        copy = os.path.join(self.folder, self.copies()[0])
        with open(copy, encoding="utf-8") as f:
            autosaved = f.read()

        self.app.close()                                            # a crash: nothing runs, so nothing clears the copy
        self.assertTrue(os.path.exists(copy))

        self.app = self.launch()
        self.addCleanup(self.app.close)
        self.app.wait_for_dialog()                                  # "curvy closed before ... was saved"
        for _ in range(5):                                          # the main window can take the foreground back from the box: put the box in front, then answer
            dialog = self.our_dialog()
            if not dialog:
                break
            box = ctypes.wintypes.RECT()
            ctypes.windll.user32.GetWindowRect(dialog, ctypes.byref(box))
            self.app.click_at(((box.left + box.right) // 2, box.top + 12), to_front=False, settle=False)   # the title bar: activates the box
            time.sleep(0.5)
            self.app.key_raw("y")
            time.sleep(1.0)
        self.app.wait_until(lambda: self.hint_tool() == "Select", "the recovered document")
        self.app.wait_until(lambda: not self.copies(), "the recovery copy to be consumed")

        self.dialog_path("s")                                       # the recovered document is untitled: Save asks for a path
        self.app.wait_until(lambda: os.path.exists(self.path), "the recovered document to be saved")
        self.app.wait_idle()
        with open(self.path, encoding="utf-8") as f:
            self.assertEqual(autosaved, f.read())


if __name__ == "__main__":
    sys.exit(newui_guitest.main())
