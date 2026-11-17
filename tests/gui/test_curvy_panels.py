r"""GUI checks of the Swatches tab of the Document panel and of the Transform panel (real mouse and keyboard through the automation pipe).

    python D:\code\curvy\tests\gui\test_curvy_panels.py --exe D:\code\curvy\out\build\x64-debug\curvy.exe      (add -k Name, --list, --help)

The image-palette dialog starts from a native file dialog, which the pipe cannot drive; it is covered by the unit tests only.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from curvyCase import CurvyCase
import newui_guitest


class DockCase(CurvyCase):
    def open_dock(self):
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


class SwatchesTab(DockCase):
    def setUp(self):
        # Listed in this order so the Document panel is on top; its body is tall enough for the tiles and the details box.
        self.make_settings_file("{ panels: { document: { userBodyHeight: 440 }, node: { collapsed: true }, transform: { collapsed: true }, "
                                "pyeditor: { collapsed: true }, pyconsole: { collapsed: true } } }")
        super().setUp()

    def show_tab(self):
        self.open_dock()
        tabs = self.app.find("documentTabs")
        self.app.click_at((tabs.x + 95, tabs.y + 14))                  # the second tab strip header, "Swatches"
        self.app.wait_until(lambda: self.app.find("swatchGrid").w > 100, "the Swatches tab to lay out")
        self.app.wait_idle()

    def tile_centre(self, index):
        grid = self.app.find("swatchGrid")
        columns = max(1, int((grid.w + 5) // 34))
        return (int(grid.x + (index % columns) * 34 + 14), int(grid.y + (index // columns) * 34 + 14))

    def tile_colour(self, index):
        return tuple(self.app.screenshot().pixel(*self.tile_centre(index)))[:3]

    def hex_text(self):
        return self.app.text_of("detailHex").strip().lower()

    def test_the_default_palette_shows_as_tiles(self):
        self.show_tab()
        self.assertEqual("#000000", self.hex_text(), "the first swatch is selected")
        black, white = self.tile_colour(0), self.tile_colour(4)        # black, three greys, then white
        self.assertLess(max(black), 40)
        self.assertGreater(min(white), 215)

    def test_clicking_a_tile_selects_it_and_shows_its_colour(self):
        self.show_tab()
        self.app.click_at(self.tile_centre(4))
        self.app.wait_until(lambda: self.hex_text() != "#000000", "the details to follow the click")
        self.assertEqual("#ffffff", self.hex_text())

    def test_dragging_a_tile_onto_another_moves_the_swatch(self):
        self.show_tab()
        before = [self.tile_colour(i) for i in range(5)]
        self.app.drag(self.tile_centre(0), self.tile_centre(4), steps=12)
        self.app.wait_idle()
        after = [self.tile_colour(i) for i in range(5)]
        self.assertEqual(before[1:5], after[0:4], "the others moved down to fill the gap")
        self.assertEqual(before[0], after[4], "the dragged swatch landed on the drop tile")
        self.assertEqual("#000000", self.hex_text(), "the moved swatch stays selected")

    def test_a_small_wobble_on_a_tile_is_a_click_not_a_move(self):
        self.show_tab()
        before = [self.tile_colour(i) for i in range(3)]
        x, y = self.tile_centre(2)
        self.app.drag((x, y), (x + 2, y), steps=3)
        self.app.wait_idle()
        self.assertEqual(before, [self.tile_colour(i) for i in range(3)])

    def test_choosing_recent_shows_no_drag_to_reorder(self):
        self.show_tab()
        rows = self.app.find("paletteRows")
        self.app.click_at((int(rows.x + rows.w / 2), int(rows.y + 48 + 20)))   # the second row: a 44 px row and a 4 px gap, then its middle
        self.app.wait_idle()
        self.app.wait_until(lambda: self.app.prop("paletteName", "text") == "Recent", "the palette button to say Recent")


class TransformPanel(DockCase):
    def setUp(self):
        self.make_settings_file("{ panels: { pyconsole: {}, pyeditor: { collapsed: true }, document: { collapsed: true }, node: { collapsed: true } } }")
        super().setUp()

    def scene(self):
        self.open_dock()
        self.run_line("layer = doc.add_layer('L')")
        self.run_line("r = layer.add_rect(10, 10, 50, 30, fill=(1, 0, 0, 1))")
        self.run_line("doc.selection.set(r)")
        self.app.wait_idle()

    def test_it_shows_the_transform_of_what_is_selected(self):
        self.scene()
        self.app.wait_until(lambda: self.app.find("objectXform_tx").w > 0, "the Object page")
        self.assertEqual("0", self.app.text_of("objectXform_tx").strip())

    def test_the_local_page_offers_to_add_a_transform_and_typing_a_value_moves_the_shape(self):
        self.scene()
        tabs = self.app.find("transformTabs")
        self.app.click_at((tabs.x + 90, tabs.y + 14))                  # the Local header
        self.app.wait_until(lambda: self.app.find("addLocalTransform").w > 0, "the Add button")
        self.app.click("addLocalTransform")
        self.app.wait_until(lambda: self.app.find("localXform_tx").w > 0, "the local rows")
        self.app.click("localXform_tx")
        self.app.key("end")
        for _ in range(4):
            self.app.key("backspace")                                  # ctrl+a does not select in a text field here, it types an "a"
        self.app.type("40")
        self.app.key("enter")
        self.app.wait_idle()
        self.run_line("print('TX', doc.shape_transform(r)['tx'])")
        self.assertIn("TX 40", self.transcript())


if __name__ == "__main__":
    newui_guitest.main()
