r"""GUI check of the dock areas: real mouse drags on the splitters through the automation pipe.

    python D:\code\curvy\tests\gui\test_curvy_dock.py --exe D:\code\curvy\out\build\x64-release\curvy.exe

The app is started with --settings on a temporary file, so the user's own settings are never touched.
"""
import os
import sys
import tempfile

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from test_curvy_gui import CurvyCase

class Docks(CurvyCase):
    def setUp(self):
        self._settings_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self._settings_dir.cleanup)
        self.settings_file = os.path.join(self._settings_dir.name, "settings.json5")
        super().setUp()

    def launch(self):
        return newui_guitest.App(newui_guitest.testing.Settings.exe or self.exe, ("--settings", self.settings_file), cwd=newui_guitest.testing.Settings.cwd)

    def geometry(self):
        names = ["topSplitter", "bottomSplitter", "leftSplitter", "rightSplitter", "topArea", "leftArea", "rightArea", "bottomArea", "canvas"]
        return {n: self.app.find(n) for n in names}

    def show(self, label):
        print("\n" + label)
        for name, r in self.geometry().items():
            print("  %-15s x=%5d y=%5d w=%5d h=%5d" % (name, r.x, r.y, r.w, r.h))

    def open_all(self):
        top = self.app.find("topSplitter")
        self.app.double_click((top.x + 600, top.y + 3))
        self.app.click("toolbarToggle")
        self.app.click("propertiesToggle")
        self.app.click("graphToggle")

    def test_dragging_the_top_splitter_off_the_window_keeps_everything(self):
        self.open_all()
        self.show("all four areas open")
        top = self.app.find("topSplitter")
        area = self.app.find("topArea")
        grab = (top.x + 600, top.y + area.h + 3)
        self.app.drag(grab, (grab[0], top.y - 150), steps=30)
        self.show("after dragging the top splitter far above the window")
        for name, r in self.geometry().items():
            self.assertGreater(r.w, 0, name)
            self.assertGreater(r.h, 0, name)
        self.assertGreaterEqual(self.app.find("topArea").h, 60)

        left = self.app.find("leftArea")
        grab = (left.x + left.w + 3, left.y + 200)
        self.app.drag(grab, (left.x - 400, grab[1]), steps=30)
        self.show("after dragging the left splitter far left of the window")
        for name, r in self.geometry().items():
            self.assertGreater(r.w, 0, name)
            self.assertGreater(r.h, 0, name)
        self.assertGreaterEqual(self.app.find("leftArea").w, 60)


    def test_a_dragged_area_keeps_its_header_bar(self):
        self.app.click("propertiesToggle")
        self.app.click("toolbarToggle")
        parts = ["rightArea", "rightAreaHeaderBar", "rightAreaHeader", "rightAreaBody", "leftArea", "leftAreaHeaderBar", "leftAreaBody"]

        def dump(label):
            print("\n" + label)
            for n in parts:
                r = self.app.find(n)
                print("  %-19s x=%5d y=%5d w=%5d h=%5d vis=%s" % (n, r.x, r.y, r.w, r.h, r.visible))

        def pixels(label):
            shot = self.app.screenshot()
            out = []
            for n in ("rightAreaHeaderBar", "rightAreaBody", "leftAreaHeaderBar", "leftAreaBody"):
                r = self.app.find(n)
                out.append((n, shot.pixel(r.x + r.w - 30, r.y + r.h / 2 if n.endswith("Bar") else r.y + 30)))
            print("  pixels " + label + ": " + ", ".join("%s=%s" % p for p in out))
            return dict(out)

        dump("both opened by the buttons")
        first = pixels("opened")
        right = self.app.find("rightArea")
        self.app.drag((right.x - 3, right.y + 200), (right.x - 150, right.y + 200), steps=20)
        dump("right area dragged 150 wider")
        second = pixels("right dragged")
        left = self.app.find("leftArea")
        self.app.drag((left.x + left.w + 3, left.y + 200), (left.x + left.w + 100, left.y + 200), steps=20)
        dump("left area dragged 100 wider")
        third = pixels("left dragged")
        for n in ("rightAreaHeaderBar", "leftAreaHeaderBar"):
            self.assertGreater(self.app.find(n).h, 0, n)
            self.assertGreater(self.app.find(n).w, 0, n)
            self.assertNotEqual(first[n], first[n.replace("HeaderBar", "Body")], n + " is drawn apart from the body")
            self.assertEqual(first[n], second[n], n + " after the right drag")
            self.assertEqual(first[n], third[n], n + " after the left drag")


    def test_dragging_a_hidden_areas_splitter_opens_it_with_its_header_bar(self):
        self.app.click("propertiesToggle")                       # shown
        self.app.click("propertiesToggle")                       # hidden again
        self.assertFalse(self.app.find("rightArea").visible)
        splitter = self.app.find("rightSplitter")
        grab = (splitter.x + splitter.w - 3, splitter.y + 200)
        self.app.drag(grab, (grab[0] - 200, grab[1]), steps=20)
        area = self.app.find("rightArea")
        bar = self.app.find("rightAreaHeaderBar")
        self.assertTrue(area.visible)
        self.assertGreaterEqual(area.w, 60)
        shot = self.app.screenshot()
        self.assertEqual((46, 46, 46), shot.pixel(bar.x + bar.w - 30, bar.y + bar.h / 2), "the header bar is drawn")


    def test_the_session_comes_back_after_a_restart(self):
        with open(self.settings_file, "w") as f:
            f.write("{ appearance: { theme: 'light' }, window: { x: 120, y: 90, width: 1000, height: 700 }, docks: { left: { size: 200, collapsed: false } } }")
        self.app.close()
        self.app = self.launch()
        self.addCleanup(self.app.close)
        self.assertFalse(self.app.find("leftArea").w < 150, "the left area opens from the file")
        self.assertAlmostEqual(200, self.app.find("leftArea").w, delta=1)
        self.assertGreater(self.app.screenshot().pixel(*self.app.find("topBar").center), (200, 200, 200), "the light theme from the file")
        root = self.app.find("rootview")
        self.assertAlmostEqual(1000, root.w + 16, delta=40, msg="the window size from the file")

        self.app.click("propertiesToggle")                       # one change this session: the right area
        self.app.key_raw("alt+f4")
        self.app.wait_until(lambda: not self.app.running, "the app to exit", timeout=10)
        with open(self.settings_file) as f:
            saved = f.read()
        self.assertIn("light", saved)
        self.assertRegex(saved, r"width:\s*1000")

        self.app = self.launch()
        self.addCleanup(self.app.close)
        self.assertAlmostEqual(268, self.app.find("rightArea").w, delta=1)
        self.assertAlmostEqual(200, self.app.find("leftArea").w, delta=1)


    def saved_window(self):
        import re
        with open(self.settings_file) as f:
            text = f.read()
        return {k: int(re.search(k + r":\s*(-?\d+)", text.split("window:")[1]).group(1)) for k in ("x", "y", "width", "height")}

    def test_the_window_position_is_restored_and_saved(self):
        with open(self.settings_file, "w") as f:
            f.write("{ window: { x: 120, y: 90, width: 1000, height: 700 } }")
        self.app.close()
        self.app = self.launch()
        self.addCleanup(self.app.close)
        root = self.app.find("rootview")
        print("\nrestored: client origin (%d, %d) size %dx%d" % (root.x, root.y, root.w, root.h))
        self.assertAlmostEqual(120, root.x, delta=20, msg="x from the file")
        self.assertAlmostEqual(90, root.y, delta=60, msg="y from the file")

        import ctypes
        user32 = ctypes.windll.user32
        hwnd = user32.FindWindowW(None, "curvy")                  # a title-bar drag would start Windows' own move loop, which the driver cannot wait out
        self.assertTrue(hwnd, "the window")
        user32.MoveWindow(hwnd, 320, 190, 1000, 700, True)
        self.app.wait_until(lambda: self.app.find("rootview").x > root.x + 150, "the window to move", timeout=10)
        moved = self.app.find("rootview")
        print("moved:    client origin (%d, %d)" % (moved.x, moved.y))
        user32.PostMessageW(hwnd, 0x0010, 0, 0)                   # WM_CLOSE
        self.app.wait_until(lambda: not self.app.running, "the app to exit", timeout=10)
        saved = self.saved_window()
        print("saved:    %s" % saved)
        self.assertAlmostEqual(saved["x"], 120 + 200, delta=30)
        self.assertAlmostEqual(saved["y"], 90 + 100, delta=30)

        self.app = self.launch()
        self.addCleanup(self.app.close)
        again = self.app.find("rootview")
        print("relaunch: client origin (%d, %d)" % (again.x, again.y))
        self.assertAlmostEqual(moved.x, again.x, delta=2)
        self.assertAlmostEqual(moved.y, again.y, delta=2)


    def test_a_maximized_window_and_its_screen_are_kept(self):
        import ctypes
        with open(self.settings_file, "w") as f:
            f.write("{ window: { x: 120, y: 90, width: 1000, height: 700, maximized: true } }")
        self.app.close()
        self.app = self.launch()
        self.addCleanup(self.app.close)
        screen_width = ctypes.windll.user32.GetSystemMetrics(0)
        self.app.wait_until(lambda: self.app.find("rootview").w > 1100, "the window to be maximized", timeout=10)
        self.assertGreater(self.app.find("rootview").w, screen_width * 0.9, "it fills the screen it is on")
        self.app.key_raw("alt+f4")
        self.app.wait_until(lambda: not self.app.running, "the app to exit", timeout=10)
        with open(self.settings_file) as f:
            text = f.read()
        print("\nsaved window: " + text.split("window:")[1].split("}")[0].replace("\n", " "))
        self.assertIn("maximized: true", text)
        self.assertRegex(text, r"width:\s*1000")                 # the normal size is kept for when it is restored
        self.assertRegex(text, r'monitor:\s*"\\{4}\.\\{2}DISPLAY\d+"')   # \\.\DISPLAYn, escaped in the file

        self.app = self.launch()
        self.addCleanup(self.app.close)
        self.app.wait_until(lambda: self.app.find("rootview").w > 1100, "it to come back maximized", timeout=10)


    def test_the_window_opens_on_the_screen_that_was_saved(self):
        import ctypes
        import ctypes.wintypes as wt
        screens = []

        @ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(wt.RECT), ctypes.c_void_p)
        def each(monitor, dc, rect, data):
            class Info(ctypes.Structure):
                _fields_ = [("cbSize", wt.DWORD), ("rcMonitor", wt.RECT), ("rcWork", wt.RECT), ("dwFlags", wt.DWORD), ("szDevice", ctypes.c_wchar * 32)]
            info = Info()
            info.cbSize = ctypes.sizeof(Info)
            ctypes.windll.user32.GetMonitorInfoW(ctypes.c_void_p(monitor), ctypes.byref(info))
            screens.append((info.szDevice, info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom, bool(info.dwFlags & 1)))
            return 1
        ctypes.windll.user32.EnumDisplayMonitors(None, None, each, None)
        if len(screens) < 2:
            self.skipTest("needs two screens")
        other = [s for s in screens if not s[5]][0]
        device = other[0].replace("\\", "\\\\")
        with open(self.settings_file, "w") as f:
            f.write('{ window: { x: 120, y: 90, width: 1000, height: 700, monitor: "%s" } }' % device)   # the position is on the primary screen
        self.app.close()
        self.app = self.launch()
        self.addCleanup(self.app.close)
        root = self.app.find("rootview")
        print("\nsaved screen %s at x=%d..%d; the window opened with client origin (%d, %d)" % (other[0], other[1], other[3], root.x, root.y))
        self.assertGreaterEqual(root.x, other[1], "the window moved onto the saved screen")
        self.assertLess(root.x, other[3])


    def dialog_open(self):
        try:
            return self.app.find("settingsOk").w > 0
        except newui_guitest.GuiTestError:
            return False

    def open_settings(self):
        self.focus_canvas()
        self.tap_space()
        self.app.key("s")                                         # the New ring's Settings slot
        self.app.wait_until(self.dialog_open, "the Settings dialog to open")

    def test_the_settings_dialog_applies_live_and_cancel_puts_it_back(self):
        self.open_settings()
        self.app.click("navWorkspace")
        self.assertEqual(0, self.app.find("leftArea").w)
        self.app.click("toolbarVisible")                           # "Show the left toolbar" is the left dock area
        self.app.wait_until(lambda: self.app.find("leftArea").w >= 200, "the left area to show while the dialog is open")
        self.app.click("settingsCancel")
        self.app.wait_until(lambda: not self.dialog_open(), "the dialog to close")
        self.assertEqual(0, self.app.find("leftArea").w, "Cancel puts the area back")
        self.assertFalse(os.path.exists(self.settings_file), "Cancel saves nothing")

    def test_ok_in_the_settings_dialog_keeps_and_saves_the_changes(self):
        self.open_settings()
        self.app.click("navWorkspace")
        self.app.click("toolbarVisible")
        self.app.click("settingsOk")
        self.app.wait_until(lambda: not self.dialog_open(), "the dialog to close")
        self.assertGreaterEqual(self.app.find("leftArea").w, 200)
        with open(self.settings_file) as f:
            saved = f.read()
        self.assertRegex(saved, r"left:\s*\{\s*size:\s*220,\s*collapsed:\s*false")


if __name__ == "__main__":
    newui_guitest.main()
