r"""GUI tests for curvy's workspace as it is now: the start screen, the rings, the tool buttons, drawing and selecting a shape, closing.

    python D:\code\curvy\tests\gui\test_curvy_workspace.py --exe D:\code\curvy\out\build\x64-release\curvy.exe      (add -k Name, --list, --help)
"""
import ctypes
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from curvyCase import CurvyCase
import newui_guitest


class StartUp(CurvyCase):
    def test_the_workspace_starts_empty(self):
        self.assertTrue(self.app.running)
        self.assertEqual("Nothing open", self.hint_tool())
        self.assertEqual("Tap Space to start", self.hint_keys())
        self.assertFalse(self.ring_open())

    def test_the_docks_start_hidden(self):
        self.assertFalse(self.app.find("rightArea").visible)
        self.assertGreater(self.app.find("canvas").w, 600)


class Rings(CurvyCase):
    def test_space_opens_the_new_ring_and_a_slot_key_starts_a_document(self):
        self.focus_canvas()
        self.tap_space()
        self.assertEqual("Ring: New", self.hint_keys())
        self.app.key("2")
        self.app.wait_until(lambda: self.hint_tool() == "Select", "a document to open")
        self.assertEqual("New A4", self.hint_keys())
        self.assertFalse(self.ring_open())

    def test_with_a_document_open_space_over_the_page_opens_the_creation_ring(self):
        self.new_a4()                                              # the pointer is left at the middle of the canvas: on the page
        self.tap_space()
        self.assertEqual("Ring: Create", self.hint_keys())

    def test_space_over_the_workspace_around_the_page_opens_the_document_ring(self):
        self.new_a4()
        canvas = self.app.find("canvas")
        self.app.click_at((canvas.x + 40, canvas.y + canvas.h - 40))   # the pasteboard corner
        self.tap_space()
        self.assertEqual("Ring: Document", self.hint_keys())

    def test_escape_closes_the_ring_and_changes_nothing(self):
        self.new_a4()
        self.tap_space()
        self.app.key("escape")
        self.app.wait_until(lambda: not self.ring_open(), "the ring to close")
        self.assertEqual("Select", self.hint_tool())


class Closing(CurvyCase):
    def exit_code(self):
        code = ctypes.c_ulong(0)
        ctypes.windll.kernel32.GetExitCodeProcess(ctypes.c_void_p(self.app._process), ctypes.byref(code))
        return code.value

    def wait_exit(self):
        self.app.wait_until(lambda: not self.app.running, "the app to exit", timeout=10)
        self.assertEqual(0, self.exit_code())

    def test_closing_with_nothing_open_exits_cleanly(self):
        self.app.key_raw("alt+f4")
        self.wait_exit()

    def test_closing_an_unmodified_document_exits_cleanly(self):
        self.new_a4()
        self.app.key_raw("alt+f4")
        self.wait_exit()


if __name__ == "__main__":
    newui_guitest.main()
