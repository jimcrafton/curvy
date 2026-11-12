r"""GUI check of the node graph view: a real double-click on a node box enters it (the breadcrumb changes).

    python D:\code\curvy\tests\gui\test_curvy_graphview.py --exe D:\code\curvy\out\build\x64-release\curvy.exe
"""
import os
import re
import sys
import tempfile
import time

sys.path.insert(0, r"D:\code\newui\tools\guitest")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import newui_guitest
from test_curvy_samples import Samples


class GraphViewCase(Samples):
    sample = "layersAndGroups.curvy"
    shots = os.path.join(tempfile.gettempdir(), "curvyGraphShots")

    def shot(self, name):
        os.makedirs(self.shots, exist_ok=True)
        path = os.path.join(self.shots, name + ".bmp")
        self.app.ask("screenshot", name="rootview", path=path)
        print("screenshot: " + path)

    def crumb(self):
        return self.app.prop("graphCrumb", "text")

    def positions_in_file(self):
        with open(self.path, encoding="utf-8") as f:
            return re.findall(r"pos:\s*\[\s*(-?[\d.e+-]+),\s*(-?[\d.e+-]+)\s*\]", f.read())

    def save(self):
        """With the graph up the canvas is covered and the ring that Space opens is the node ring, which swallows S."""
        self.app.click("graphToggle")
        self.app.wait_idle()
        super().save()
        self.app.click("graphToggle")
        self.app.wait_idle()

    def open_graph(self):
        self.open_sample()
        self.wait_until_open()
        self.app.click("graphToggle")
        self.app.wait_idle()
        root = self.app.find("rootview")
        return (root.x + 330, root.y + 370)             # a layer box, as in the double-click test


class GraphEditing(GraphViewCase):
    """Delete and wire-drag in Group1 of the sample: circle > fill > merge > xform, boxes at fixed places once the view is fitted."""

    FILL = (437, 430)
    FILL_INPUT = (362, 431)
    CIRCLE_OUTPUT = (302, 429)      # one column (150 + 60) left of the fill, same row; the output dot is at the right middle

    def enter_group(self):
        layer = self.open_graph()
        self.app.double_click(layer)                        # Layer1
        self.app.wait_idle()
        self.app.double_click((layer[0], layer[1] + 33))    # Group1
        self.app.wait_idle()
        return self.app.find("rootview")

    def fills_in_file(self):
        with open(self.path, encoding="utf-8") as f:
            return f.read().count('type: "curvyFill"')

    def first_fill_inputs(self):
        with open(self.path, encoding="utf-8") as f:
            found = re.search(r'type: "curvyFill",\s*name: "fill",.*?inputs: \[(.*?)\]', f.read(), re.S)
        return re.sub(r"\s", "", found.group(1)) if found else None

    def test_delete_removes_the_selected_node_and_undo_brings_it_back(self):
        root = self.enter_group()
        self.app.click_at((root.x + self.FILL[0], root.y + self.FILL[1]))
        self.app.key("delete")
        self.app.wait_idle()
        self.save()
        self.assertEqual(3, self.fills_in_file())
        self.app.key("ctrl+z")
        self.app.wait_idle()
        self.save()
        self.assertEqual(4, self.fills_in_file())

    def test_dragging_a_wire_off_an_input_dot_unwires_it_and_undo_rewires(self):
        root = self.enter_group()
        wired = self.first_fill_inputs_after_save()
        self.assertEqual("3", wired)
        self.app.drag((root.x + self.FILL_INPUT[0], root.y + self.FILL_INPUT[1]), (root.x + 300, root.y + 600))
        self.app.wait_idle()
        self.save()
        self.assertNotEqual("3", self.first_fill_inputs(), "the wire is off")
        self.app.key("ctrl+z")
        self.app.wait_idle()
        self.save()
        self.assertEqual("3", self.first_fill_inputs(), "undo puts it back")

    def first_fill_tags(self):
        with open(self.path, encoding="utf-8") as f:
            found = re.search(r'type: "curvyFill",\s*name: "fill",\s*tags: \[(.*?)\]', f.read(), re.S)
        return re.findall(r'"([^"]*)"', found.group(1)) if found else []

    def type_tags(self, root, text):
        self.app.click_at((root.x + self.FILL[0], root.y + self.FILL[1]))
        self.app.wait_idle()
        self.app.click("graphTagsField")
        self.app.key("ctrl+a")
        self.app.type(text)
        self.app.key("enter")
        self.app.wait_idle()

    def test_typing_tags_in_the_panel_sets_them_and_undo_takes_them_back(self):
        root = self.enter_group()
        self.assertEqual([], self.first_fill_tags())
        self.type_tags(root, "hot warm hot")
        self.save()
        self.assertEqual(["hot", "warm"], self.first_fill_tags(), "words split on spaces, duplicates dropped")
        self.app.click_at((root.x + 300, root.y + 650))                 # out of the field, onto the background
        self.app.key("ctrl+z")
        self.app.wait_idle()
        self.save()
        self.assertEqual([], self.first_fill_tags(), "one undo")

    def test_reserved_tags_are_refused(self):
        root = self.enter_group()
        self.type_tags(root, "hot layer")
        self.save()
        self.assertEqual([], self.first_fill_tags(), "the whole edit is refused")

    def test_the_graph_view_comes_back_when_the_file_is_opened_again(self):
        """The file's "view" section: level, zoom, pan and selection. The save hides the graph first, so this is the graph-hidden case;
        the graph-open flag is covered by the unit tests (with the graph up, Space opens the node ring and swallows the S of Save)."""
        root = self.enter_group()
        self.app.click_at((root.x + self.FILL[0], root.y + self.FILL[1]))
        self.app.wheel("graphNodes", 2)                     # a drag on empty space would clear the selection
        self.app.wait_idle()
        zoom = self.app.prop("graphZoom", "text")
        self.assertEqual("fill", self.app.text_of("graphNameField"))
        self.save()
        with open(self.path, encoding="utf-8") as f:
            self.assertIn("view", f.read())
        self.app.click("graphToggle")                       # off: the Open below goes through the canvas
        self.app.wait_idle()
        self.open_sample()
        self.app.wait_until(lambda: not self.question_is_up(), "the Open dialog to close")
        time.sleep(1.0)                                     # the load runs as a task after the dialog closes
        self.app.wait_idle()
        self.app.click("graphToggle")
        self.app.wait_idle()
        self.assertEqual("Group1", self.crumb(), "the level")
        self.assertEqual(zoom, self.app.prop("graphZoom", "text"), "the zoom")
        self.assertEqual("fill", self.app.text_of("graphNameField"), "the selected node")

    def test_ctrl_z_works_with_the_focus_still_in_a_panel_field(self):
        root = self.enter_group()
        self.type_tags(root, "hot")
        self.assertEqual("graphTagsField", self.app.focused_name(), "the test is about a key pressed while the field has focus")
        self.app.key("ctrl+z")
        self.app.wait_idle()
        self.save()
        self.assertEqual([], self.first_fill_tags())

    def test_ctrl_z_undoes_one_step_at_a_time(self):
        root = self.enter_group()
        self.type_tags(root, "hot")
        self.type_tags(root, "hot warm")
        self.app.key("ctrl+z")
        self.app.wait_idle()
        self.save()
        self.assertEqual(["hot"], self.first_fill_tags(), "only the second edit is undone")

    def test_ctrl_y_redoes_with_the_focus_in_a_panel_field(self):
        root = self.enter_group()
        self.type_tags(root, "hot")
        self.app.key("ctrl+z")
        self.app.key("ctrl+y")
        self.app.wait_idle()
        self.save()
        self.assertEqual(["hot"], self.first_fill_tags())

    def test_ctrl_shift_z_redoes_too(self):
        root = self.enter_group()
        self.type_tags(root, "hot")
        self.app.key("ctrl+z")
        self.app.key("ctrl+shift+z")
        self.app.wait_idle()
        self.save()
        self.assertEqual(["hot"], self.first_fill_tags())

    def test_ctrl_z_and_ctrl_y_with_nothing_to_do_change_nothing(self):
        root = self.enter_group()
        self.app.key("ctrl+y")                              # nothing to redo yet
        self.type_tags(root, "hot")
        self.app.key("ctrl+y")                              # still nothing to redo
        self.app.wait_idle()
        self.save()
        self.assertEqual(["hot"], self.first_fill_tags())

    def drag_holding(self, key, start, end):
        """A drag with a key held down (the driver's drag has no modifier argument)."""
        vk = {"shift": 0x10, "ctrl": 0x11}[key]
        self.app.bring_to_front()
        self.app._send([self.app._key(vk)])
        try:
            self.app.drag(start, end)
        finally:
            self.app._send([self.app._key(vk, up=True)])
        self.app.wait_idle()

    def test_dragging_from_an_output_dot_to_an_input_dot_wires_it_and_undo_unwires(self):
        root = self.enter_group()
        fill = (root.x + self.FILL[0], root.y + self.FILL[1])
        self.app.drag(fill, (fill[0], fill[1] + 100))           # the first move pins every box, so the unwired fill stays put
        self.app.wait_idle()
        input_dot = (root.x + self.FILL_INPUT[0], root.y + self.FILL_INPUT[1] + 100)
        circle_output = (root.x + self.CIRCLE_OUTPUT[0], root.y + self.CIRCLE_OUTPUT[1])
        self.app.drag(input_dot, (root.x + 300, root.y + 700))
        self.app.wait_idle()
        self.save()
        self.assertNotEqual("3", self.first_fill_inputs(), "the wire is off")
        self.app.drag(circle_output, input_dot)
        self.app.wait_idle()
        self.save()
        self.assertEqual("3", self.first_fill_inputs(), "the new wire is saved")
        self.app.key("ctrl+z")
        self.app.wait_idle()
        self.save()
        self.assertNotEqual("3", self.first_fill_inputs(), "one undo takes the new wire off")

    def test_shift_dragging_a_marquee_selects_the_boxes_it_touches(self):
        root = self.enter_group()
        self.drag_holding("shift", (root.x + 340, root.y + 380), (root.x + 530, root.y + 470))
        self.app.key("delete")
        self.app.wait_idle()
        self.save()
        self.assertEqual(3, self.fills_in_file(), "only the fill is inside the marquee")
        self.app.key("ctrl+z")
        self.app.wait_idle()
        self.save()
        self.assertEqual(4, self.fills_in_file())

    def first_fill_inputs_after_save(self):
        self.save()
        return self.first_fill_inputs()


class DataWindow(GraphViewCase):
    """The Data window is a floating tool window of its own. The driver's own typing brings the main window to the front, which would take the
    keyboard back from it, so these tests click with to_front=False and send the characters themselves."""

    def open_data_window(self):
        layer = self.open_graph()
        self.app.click_at(layer)                                # select the layer box
        self.app.wait_idle()
        self.app.click("graphDataButton")
        self.app.wait_idle()
        self.assertTrue(self.app.find("pointTableFilter").visible)

    def type_raw(self, text):
        for c in text:
            self.app._send([self.app._key(0, unicode_char=c), self.app._key(0, up=True, unicode_char=c)])
        time.sleep(0.3)

    def test_the_filter_field_takes_typing(self):
        self.open_data_window()
        self.app.click_at(self.app.find("pointTableFilter").center, to_front=False)
        self.type_raw("fill")
        self.assertEqual("fill", self.app.text_of("pointTableFilter"))
        self.app.key_raw("enter")
        time.sleep(0.3)
        self.assertEqual("fill", self.app.text_of("pointTableFilter"), "Return applies the filter and leaves the text")


class GraphView(GraphViewCase):
    def test_double_click_on_a_box_enters_it(self):
        self.open_sample()
        self.wait_until_open()
        self.app.click("graphToggle")
        self.app.wait_idle()
        nodes = self.app.find("graphNodes")
        print("\ngraphNodes x=%d y=%d w=%d h=%d visible=%s" % (nodes.x, nodes.y, nodes.w, nodes.h, nodes.visible))
        print("crumb before: %r" % self.crumb())
        self.shot("opened")
        self.assertGreater(nodes.w, 50)
        root = self.app.find("rootview")
        layer = (root.x + 330, root.y + 370)
        self.app.double_click(layer)
        self.app.wait_idle()
        self.shot("afterDoubleClick")
        print("crumb after: %r" % self.crumb())
        self.assertNotEqual("root", self.crumb())

    def test_dragging_a_node_is_one_undo_step(self):
        layer = self.open_graph()
        self.assertEqual([], self.positions_in_file() or [])
        self.app.drag(layer, (layer[0] + 60, layer[1] + 40))
        self.app.wait_idle()
        self.save()
        pinned = self.positions_in_file()
        self.assertGreater(len(pinned), 1, "the first move pins every shown node")

        self.app.key("ctrl+z")
        self.app.wait_idle()
        self.save()
        self.assertEqual([], self.positions_in_file(), "one undo takes the whole drag back")

        self.app.key("ctrl+y")
        self.app.wait_idle()
        self.save()
        self.assertEqual(pinned, self.positions_in_file(), "redo puts the same positions back")

    def test_a_click_without_a_drag_moves_nothing(self):
        layer = self.open_graph()
        self.app.click_at(layer)
        self.app.wait_idle()
        self.save()
        self.assertEqual([], self.positions_in_file())


class ColourCard(GraphViewCase):
    """The colour card of a fill in the graph panel. Link and Unlink are entries of a native popup menu; the test drives it with the keyboard (Down, Enter)."""

    def links_in_file(self):
        with open(self.path, encoding="utf-8") as f:
            return len(re.findall(r"\blink:", f.read()))

    def open_colour_card_of_the_first_fill(self):
        layer = self.open_graph()
        self.app.double_click(layer)                    # Layer1
        self.app.wait_idle()
        self.app.double_click((layer[0], layer[1] + 33))   # Group1: circle > fill > merge > xform
        self.app.wait_idle()
        root = self.app.find("rootview")
        self.app.click_at((root.x + 437, root.y + 430))    # the fill box
        self.app.wait_idle()
        self.app.click_at((root.x + 1193, root.y + 224))   # the colour swatch of the panel
        self.app.wait_idle()
        self.assertTrue(self.app.find("SwatchTiles").visible)

    def pick_from_swatch_menu(self, tile, downs):
        tiles = self.app.find("SwatchTiles")
        self.app.click_at((tiles.x + 9 + 21 * tile, tiles.y + 9), button="right", settle=False)
        time.sleep(0.6)                                  # the popup runs its own loop: nothing to wait on
        for _ in range(downs):
            self.app.key_raw("down")
        self.app.key_raw("enter")
        time.sleep(0.6)
        self.app.wait_idle()

    def tile_point(self, tile):
        tiles = self.app.find("SwatchTiles")
        return (tiles.x + 9 + 21 * tile, tiles.y + 9)

    def panel_colour(self):
        """The fill's colour as the panel's swatch paints it: no node value is readable without saving, which closes the card."""
        root = self.app.find("rootview")
        return self.app.screenshot().pixel(root.x + 1193, root.y + 224)

    def tile_colour(self, tile):
        return self.app.screenshot().pixel(*self.tile_point(tile))

    def assert_panel_shows_tile(self, tile, why):
        expected, actual = self.tile_colour(tile), self.panel_colour()
        self.assertFalse(newui_guitest.screenshot.differs(expected, actual), "%s: panel %r, tile %r" % (why, actual, expected))

    def test_number_keys_apply_the_recent_colours(self):
        self.open_colour_card_of_the_first_fill()
        black, red, green = 0, 5, 8
        for tile in (black, red, green):
            self.app.click_at(self.tile_point(tile))
            self.app.wait_idle()
        self.assert_panel_shows_tile(green, "clicking the last swatch")
        self.app.key("3")                                  # the recent list is now green, red, black
        self.app.wait_idle()
        self.assert_panel_shows_tile(black, "key 3 is the third most recent colour")
        self.app.key("2")                                  # applying black moved it to the front: black, green, red
        self.app.wait_idle()
        self.assert_panel_shows_tile(green, "key 2 after the list was reordered")
        self.app.key("8")                                  # only three colours are recent
        self.app.wait_idle()
        self.assert_panel_shows_tile(green, "a key past the end of the list changes nothing")

    def test_link_and_unlink_a_swatch_from_its_menu(self):
        self.open_colour_card_of_the_first_fill()
        self.assertEqual(0, self.links_in_file())
        self.pick_from_swatch_menu(tile=5, downs=3)      # Use, Replace with current colour, Link
        self.save()
        self.assertGreaterEqual(self.links_in_file(), 1, "the swatch is saved as linked")
        self.pick_from_swatch_menu(tile=5, downs=3)      # the same entry now reads Unlink
        self.save()
        self.assertEqual(0, self.links_in_file(), "unlinking removes it")


if __name__ == "__main__":
    newui_guitest.main()
