r"""GUI checks of the Assets panel and the Asset Properties dialog (real mouse and keyboard through the automation pipe).

    python D:\code\curvy\tests\gui\test_curvy_assets.py --exe D:\code\curvy\out\build\x64-release\curvy.exe      (add -k Name, --list, --help)

The file dialogs of Save and Load are native windows the pipe cannot drive; they are not covered here.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from curvyCase import CurvyCase
import newui_guitest


class AssetsCase(CurvyCase):
    def setUp(self):
        # The Assets panel on top and open; the others collapsed so it has room.
        self.make_settings_file("{ " + CurvyCase.TALL_WINDOW + "panels: { assets: { collapsed: false, userBodyHeight: 460 }, document: { collapsed: true }, "
                                "node: { collapsed: true }, transform: { collapsed: true }, pyeditor: { collapsed: true }, "
                                "pyconsole: { collapsed: true } } }")
        super().setUp()
        self.new_a4()
        if self.app.find("rightArea").w <= 0:
            self.toggle_properties()
        self.app.wait_until(lambda: self.app.find("rightArea").w > 0, "the right dock")
        self.app.wait_until(lambda: self.app.find("assetList").w > 100, "the Assets panel to lay out")
        self.app.wait_idle()

    def click_asset(self, name):
        """The library list is a ListView: narrow it with the search field so the asset is the only row under its group heading (22 px), then click it."""
        self.app.click("assetSearch")
        self.app.key("ctrl+a")
        self.app.type(name)
        self.app.wait_idle()
        box = self.app.find("assetRows")
        self.app.click_at((box.x + 40, box.y + 22 + 13))
        self.app.wait_idle()

    def select_wobble(self):
        self.click_asset("wobbleAsset")
        self.app.wait_until(lambda: self.label("detailsName") == "wobbleAsset  v1", "the details of wobbleAsset")

    def dialog_open(self):
        try:
            return self.app.find("propsCancel").w > 0
        except newui_guitest.GuiTestError:
            return False

    def open_dialog(self, button):
        self.app.click(button)
        self.app.wait_until(self.dialog_open, "the Asset Properties dialog to open")
        self.app.wait_idle()

    def close_dialog(self):
        self.app.click("propsCancel")
        self.app.wait_until(lambda: not self.dialog_open(), "the dialog to close")

    def label(self, name):
        return self.app.prop(name, "text")      # a Label does not report its text through getText

    def footer(self):
        return self.label("propsFooterNote")


class AssetsPanel(AssetsCase):
    def test_the_bundled_asset_is_listed_and_selecting_it_shows_its_details(self):
        self.assertEqual("No asset selected", self.label("detailsName"))
        self.select_wobble()
        self.assertEqual("Wobble", self.label("detailsLabel"))
        self.assertEqual("1", self.label("detailsInputs"))
        self.assertEqual("amount, speed", self.label("detailsParams"))
        self.assertEqual("0 in this document", self.label("detailsInstances"))
        self.assertIn("wobble.asset.json", self.label("detailsSource"))

    def test_add_puts_an_instance_in_the_graph_and_undo_takes_it_out(self):
        self.select_wobble()
        self.app.click("addInstanceButton")
        self.app.wait_until(lambda: self.label("detailsInstances") == "1 in this document", "the instance count to follow Add")
        self.assertTrue(self.app.prop("unlockButton", "enabled"), "an instance in the graph can be unlocked")
        self.app.key("ctrl+z")
        self.app.wait_until(lambda: self.label("detailsInstances") == "0 in this document", "the count to follow undo")

    def test_unlock_turns_the_new_instance_into_a_subnet(self):
        self.select_wobble()
        self.app.click("addInstanceButton")
        self.app.wait_until(lambda: self.label("detailsInstances") == "1 in this document", "the instance count to follow Add")
        self.app.click("unlockButton")
        self.app.wait_until(lambda: self.label("detailsInstances") == "0 in this document", "the instance to become a subnet")


class AssetProperties(AssetsCase):
    def test_edit_opens_the_dialog_on_the_selected_asset(self):
        self.select_wobble()
        self.open_dialog("editButton")
        self.assertEqual("wobbleAsset", self.app.text_of("typeName"))
        self.assertEqual("Wobble", self.app.text_of("typeLabel"))
        self.assertEqual("wobbleAsset", self.label("propsTitle"))
        self.assertEqual("1 (Save writes 2)", self.label("versionText"))
        self.assertEqual("No changes", self.footer())
        self.close_dialog()

    def test_changing_the_label_and_pressing_apply_updates_the_panel(self):
        self.select_wobble()
        self.open_dialog("editButton")
        self.app.click("typeLabel")
        self.app.key("ctrl+a")
        self.app.type("Wobbler")
        self.app.wait_until(lambda: self.footer().startswith("Unsaved changes"), "the footer to notice the edit")
        self.app.click("propsApply")
        self.app.wait_until(lambda: self.footer() == "No changes", "Apply to take the change")
        self.close_dialog()
        self.app.wait_until(lambda: self.label("detailsLabel") == "Wobbler", "the panel to show the new label")

    def test_the_parameters_page_edits_the_selected_parameter(self):
        self.select_wobble()
        self.open_dialog("editButton")
        self.app.click("tabParameters")
        self.app.wait_until(lambda: self.app.find("paramName").w > 0, "the Parameters page")
        self.assertEqual("amount", self.app.text_of("paramName"))
        self.assertEqual("10", self.app.text_of("paramMax"))
        self.click_row("paramList", 1, 26)
        self.app.wait_until(lambda: self.app.text_of("paramName") == "speed", "the second parameter to be selected")
        self.app.click("paramAdd")
        self.app.wait_until(lambda: self.app.text_of("paramName") == "param1", "the new parameter")
        self.assertTrue(self.footer().startswith("Unsaved changes"))
        self.app.click("paramRemove")
        self.app.wait_until(lambda: self.app.text_of("paramName") == "speed", "the new parameter to go")
        self.close_dialog()

    def click_row(self, name, index, row_height):
        """A ListView's rows are not views, so click where row `index` is."""
        box = self.app.find(name)
        self.app.click_at((box.x + 40, box.y + index * row_height + row_height // 2))
        self.app.wait_idle()

    EVENT_ROWS = ["#", "module", "#", "onCreated", "onVersionUpdated", "onInputChanged", "onParamChanged", "onFrameChanged", "onDeleted", "#",
                  "onInstalled"]                                  # "#" is a group heading (26 px); events are 24 px

    def click_event(self, name):
        box = self.app.find("eventList")
        top = sum(26 if row == "#" else 24 for row in self.EVENT_ROWS[:self.EVENT_ROWS.index(name)])
        self.app.click_at((box.x + 40, box.y + top + 12))
        self.app.wait_idle()

    def click_heading(self, index):
        """The index-th heading's centre (0 = SHARED CODE)."""
        box = self.app.find("eventList")
        top = 0
        seen = -1
        for row in self.EVENT_ROWS:
            if row == "#":
                seen += 1
                if seen == index:
                    break
            top += 26 if row == "#" else 24
        self.app.click_at((box.x + 40, box.y + top + 13))
        self.app.wait_idle()

    def test_a_script_that_changes_the_asset_from_onframechanged_blocks_apply(self):
        self.select_wobble()
        self.open_dialog("editButton")
        self.app.click("tabPython")
        self.app.wait_until(lambda: self.app.find("eventList").w > 0, "the Python page")
        self.click_event("onFrameChanged")
        self.app.wait_until(lambda: self.label("eventTitle") == "onFrameChanged", "the event to be selected")
        self.click_event("onVersionUpdated")                       # a second click must move the selection, not add to it
        self.app.wait_until(lambda: self.label("eventTitle") == "onVersionUpdated", "the second event to be selected")
        self.click_heading(2)                                      # LIBRARY: a heading cannot be chosen, the next event below it is
        self.app.wait_until(lambda: self.label("eventTitle") == "onInstalled", "the event under the heading")
        self.click_event("onFrameChanged")
        self.assertIn("The playhead moved", self.label("eventWhen"))
        self.assertEqual("node, frame, playing", self.label("eventArgs"))
        self.app.click("assetScriptText")
        self.app.type("node.set_float('amount', 1)")
        self.app.wait_until(lambda: "cannot change this asset" in self.footer(), "the commit check to name the problem")
        self.assertFalse(self.app.prop("propsApply", "enabled"))
        self.assertFalse(self.app.prop("propsSave", "enabled"))
        self.app.key("ctrl+a")
        self.app.key("delete")
        self.app.type("print(node.get_float('amount'))")
        self.app.wait_until(lambda: self.app.prop("propsApply", "enabled"), "Apply to come back once the script is fine")
        self.close_dialog()

    def test_plus_with_nothing_selected_opens_a_blank_asset_that_apply_adds_to_the_list(self):
        self.open_dialog("assetNewButton")
        self.assertEqual("New asset", self.label("propsTitle"))
        self.assertEqual("Create", self.app.prop("propsSave", "text"))
        self.assertFalse(self.app.prop("propsSave", "enabled"), "a nameless asset cannot be created")
        self.app.click("typeName")
        self.app.type("guiTestAsset")
        self.app.wait_until(lambda: self.app.prop("propsSave", "enabled"), "Create to be enabled once it has a name")
        self.app.click("propsApply")
        self.app.wait_until(lambda: self.footer() == "No changes", "Apply to register the asset")
        self.close_dialog()
        self.click_asset("guiTestAsset")
        self.app.wait_until(lambda: self.label("detailsSource") == "Embedded in this document", "it to be embedded until it is saved")
        self.assertTrue(self.app.prop("saveToLibraryButton", "enabled"))


if __name__ == "__main__":
    newui_guitest.main()
