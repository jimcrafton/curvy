r"""GUI checks of the wrangle Code section of the Node panel (real mouse and keyboard through the automation pipe).

    python D:\code\curvy\tests\gui\test_curvy_wrangle.py --exe D:\code\curvy\out\build\x64-debug\curvy.exe      (add -k Name, --list, --help)

A wrangle node can only be made from Python so far, so every test makes one in the Python console first.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from curvyCase import CurvyCase
import newui_guitest


class WrangleCode(CurvyCase):
    def setUp(self):
        # Listed in this order so the console is on top, where the window shows it; the node panel follows with the room that is left.
        self.make_settings_file("{ panels: { pyconsole: { userBodyHeight: 120 }, node: {}, pyeditor: { collapsed: true }, document: { collapsed: true }, "
                                "transform: { collapsed: true }, select: { hidden: true }, assets: { collapsed: true } } }")
        super().setUp()
        self.console_collapsed = False

    def set_console_collapsed(self, collapsed):
        """The Node panel's buttons can only be clicked when they are in view, so a test that works in it gives it the whole dock."""
        if self.console_collapsed == collapsed:
            return
        panel = self.app.find("pyconsole")
        self.app.click_at((int(panel.x + 14), int(panel.y + 12)))                 # the collapse caret at the left of the title bar
        self.app.wait_idle()
        self.console_collapsed = collapsed

    def run_line(self, text):
        self.set_console_collapsed(False)
        self.app.click("pyConsoleInput")
        self.app.type(text)
        self.app.key("enter")
        self.wait_script_done()

    def transcript(self):
        return self.app.text_of("pyConsoleOutput")

    def scene(self):
        self.new_a4()
        if self.app.find("rightArea").w <= 0:
            self.toggle_properties()
        self.app.wait_until(lambda: self.app.find("rightArea").w > 0, "the right dock")
        self.run_line("w = doc.add_node('wrangle', 'bend'); doc.connect(doc.add_node('circle', 'ring'), w); doc.selection.set(w)")
        self.app.wait_until(lambda: self.app.find("wrangleCodeToggle").w > 0, "the Code row")
        self.app.wait_idle()

    def open_code(self):
        self.set_console_collapsed(True)
        self.app.click("wrangleCodeToggle")
        self.app.wait_until(lambda: self.app.find("wrangleEditorText").w > 0, "the editor")
        self.app.wait_idle()

    def type_code(self, lines):
        self.app.click("wrangleEditorText")
        for i, line in enumerate(lines):
            if i:
                self.app.key("enter")
            self.app.type(line)

    def test_the_code_row_is_there_for_a_wrangle_and_gone_for_another_node(self):
        self.scene()
        self.run_line("other = doc.add_node('circle', 'plain'); doc.selection.set(other)")
        self.app.wait_until(lambda: not self.app.find("nodePanelCode").visible, "the Code row to go")
        self.run_line("doc.selection.set(w)")
        self.app.wait_until(lambda: self.app.find("nodePanelCode").visible, "the Code row to come back")

    def test_committing_declared_parameters_makes_them_parameters_of_the_node(self):
        self.scene()
        self.open_code()
        self.type_code(["| gain = 2(0, 10) |", "y = y * gain;"])
        self.assertIn("gain", self.app.text_of("wrangleEditorText"))
        self.app.click("wrangleCommit")
        self.app.wait_idle()
        self.run_line("print('CODE', repr(w.code))")
        self.run_line("print('GAIN', w.gain)")
        self.assertIn("GAIN 2.0", self.transcript())
        self.run_line("print('CODE', w.code)")
        self.assertIn("y = y * gain;", self.transcript())

    def test_a_cook_error_shows_under_the_editor(self):
        self.scene()
        self.open_code()
        self.type_code(["y = nosuch(y);"])
        self.app.click("wrangleCommit")
        self.app.wait_until(lambda: (self.app.prop("wrangleProblem", "text") or "").strip() != "", "the cook error")

    def test_revert_puts_the_nodes_text_back(self):
        self.scene()
        self.open_code()
        self.type_code(["y = y + 1;"])
        self.app.click("wrangleRevert")
        self.app.wait_idle()
        self.run_line("print('CODE', repr(w.code))")
        self.assertIn("CODE ''", self.transcript())


if __name__ == "__main__":
    newui_guitest.main()
