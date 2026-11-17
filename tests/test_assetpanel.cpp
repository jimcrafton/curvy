#include "testsupport.h"

#include "pythonconsole.h"

#include "curvy/assetpropertiesdialogsession.h"

#include <jitnodes/graph/assetLibrary.h>
#include <jitnodes/graph/serialize.h>

namespace {

    using jitnodes::AssetDefinition;
    using jitnodes::AssetLibrary;

    std::string assetText(const std::string& name) {
        return "{ format: 'jitnodes-asset', version: 1, type: '" + name + "', inputs: 1, promote: { amount: 1 },\n"
               "  graph: { display: 2, nodes: [\n"
               "    { id: 1, type: 'subnetInput', name: 'in', floats: { index: 0 }, inputs: [] },\n"
               "    { id: 2, type: 'transform', name: 'xf', inputs: [1] } ] } }";
    }

    class AssetPanelTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Assets", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
        }

        void TearDown() override {
            for (const std::string& name : names_) {
                if (AssetLibrary::instance().has(name)) {
                    AssetLibrary::instance().uninstall(name);
                }
            }
            WorkspaceTest::TearDown();
        }

        void addEmbedded(const std::string& name) {
            AssetLibrary::instance().apply(AssetDefinition::fromText(assetText(name)));
            names_.push_back(name);
        }

        curvy::AssetManagerPanel& panel() { return window().assetPanel(); }
        curvy::SketchDocument& document() { return window().document(); }
        curvy::SelectionModel& selection() { return window().graph().selectionModel(); }
        std::size_t countOf(const std::string& type) {
            std::size_t count = 0;
            for (jitnodes::NodeId id : document().network().nodeIds()) {
                count += document().network().node(id).typeName() == type ? 1 : 0;
            }
            return count;
        }

        std::vector<std::string> names_;
    };

}

TEST_F(AssetPanelTest, thePanelIsInTheRightDockCollapsedAndListsTheBundledAssets) {
    EXPECT_EQ(panel().id(), "assets");
    EXPECT_EQ(window().rightDock().panel("assets"), &panel());
    ASSERT_NE(window().panelLayout().find("assets"), nullptr);
    EXPECT_TRUE(window().panelLayout().find("assets")->collapsed);
    const auto names = panel().shownNames();
    EXPECT_NE(std::find(names.begin(), names.end(), "wobbleAsset"), names.end());
    const auto titles = panel().shownTitles();
    EXPECT_NE(std::find(titles.begin(), titles.end(), "DEFORM"), titles.end());
    EXPECT_EQ(window().userAssetsFolder().filename().string(), "assets");
}

TEST_F(AssetPanelTest, selectingARowShowsTheDetailsAndEnablesWhatAppliesToIt) {
    EXPECT_EQ(panel().detail("name"), "No asset selected");
    EXPECT_FALSE(panel().buttonEnabled("addInstanceButton"));
    panel().clickRow("wobbleAsset");
    EXPECT_EQ(panel().detail("name"), "wobbleAsset  v1");
    EXPECT_EQ(panel().detail("label"), "Wobble");
    EXPECT_EQ(panel().detail("inputs"), "1");
    EXPECT_EQ(panel().detail("params"), "amount, speed");
    EXPECT_EQ(panel().detail("instances"), "0 in this document");
    EXPECT_NE(panel().detail("file").find("wobble.asset.json"), std::string::npos);
    EXPECT_TRUE(panel().buttonEnabled("addInstanceButton"));
    EXPECT_TRUE(panel().buttonEnabled("editButton"));
    EXPECT_TRUE(panel().buttonEnabled("reloadButton"));            // it has a file
    EXPECT_FALSE(panel().buttonEnabled("saveToLibraryButton"));    // and is not only embedded
    EXPECT_FALSE(panel().buttonEnabled("unlockButton"));           // no instance yet
    EXPECT_FALSE(panel().bannerShown());

    addEmbedded("apTestEmbedded");
    panel().clickRow("apTestEmbedded");
    EXPECT_EQ(panel().detail("file"), "Embedded in this document");
    EXPECT_TRUE(panel().buttonEnabled("saveToLibraryButton"));
    EXPECT_FALSE(panel().buttonEnabled("reloadButton"));
    const auto titles = panel().shownTitles();
    EXPECT_EQ(titles.back(), "EMBEDDED IN THIS DOCUMENT");
}

TEST_F(AssetPanelTest, searchAndCategoryNarrowTheList) {
    addEmbedded("apTestOther");
    panel().typeSearch("apTestOther");
    EXPECT_EQ(panel().shownNames(), (std::vector<std::string>{"apTestOther"}));
    panel().typeSearch("");
    EXPECT_GT(panel().shownNames().size(), 1u);
    panel().chooseCategory("Deform");
    const auto names = panel().shownNames();
    EXPECT_NE(std::find(names.begin(), names.end(), "wobbleAsset"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), "apTestOther"), names.end());
    panel().chooseCategory("All");
    panel().typeSearch("no such asset anywhere");
    EXPECT_TRUE(panel().shownNames().empty());
}

TEST_F(AssetPanelTest, addPutsAnInstanceInTheGraphWiredFromTheSelectedNodeAsOneUndoStep) {
    const curvy::NodeRef line = document().addGraphNode("line");
    selection().set(line);
    panel().clickRow("wobbleAsset");
    const std::size_t depth = document().undoStack().depth();
    panel().clickButton("addInstanceButton");
    EXPECT_EQ(document().undoStack().depth(), depth + 1);
    ASSERT_EQ(countOf("wobbleAsset"), 1u);
    const curvy::NodeRef added = selection().primary();
    EXPECT_EQ(document().node(added).typeName(), "wobbleAsset");
    EXPECT_EQ(document().network().inputOf(added.id, 0), line.id);
    EXPECT_EQ(panel().detail("instances"), "1 in this document");
    EXPECT_TRUE(panel().buttonEnabled("unlockButton"));

    document().undoStack().undo();
    EXPECT_EQ(countOf("wobbleAsset"), 0u);
    EXPECT_EQ(panel().detail("instances"), "0 in this document");
}

TEST_F(AssetPanelTest, anOutdatedInstanceShowsTheBannerAndUpdateRebuildsItAsOneUndoStep) {
    panel().setCollapsed(false);   // a collapsed panel does not follow the graph until it is opened
    panel().clickRow("wobbleAsset");
    panel().clickButton("addInstanceButton");
    ASSERT_EQ(countOf("wobbleAsset"), 1u);
    EXPECT_FALSE(panel().bannerShown());

    AssetDefinition edited = AssetLibrary::instance().definition("wobbleAsset");
    edited.params[0].defaultValue = 0.9f;
    edited.source.clear();
    AssetLibrary::instance().apply(edited);   // another version of the same asset, in memory
    EXPECT_TRUE(panel().bannerShown());
    EXPECT_EQ(panel().bannerText(), "1 instance is older than v1");
    EXPECT_EQ(panel().badgesOf("wobbleAsset"), (std::vector<std::string>{"Outdated 1"}));
    EXPECT_NE(panel().statusText().find("1 outdated"), std::string::npos);

    const std::size_t depth = document().undoStack().depth();
    panel().clickButton("updateInstancesButton");
    EXPECT_EQ(document().undoStack().depth(), depth + 1);
    EXPECT_FALSE(panel().bannerShown());
    ASSERT_EQ(countOf("wobbleAsset"), 1u);
    document().undoStack().undo();
    EXPECT_EQ(countOf("wobbleAsset"), 1u);
    EXPECT_FALSE(panel().bannerShown());   // an instance that comes back from undo is built from the registered definition, so it is not older
    AssetLibrary::instance().reload("wobbleAsset");   // the bundled definition again, for the tests after this one
}

TEST_F(AssetPanelTest, unlockTurnsTheSelectedInstanceIntoAnEditableSubnet) {
    panel().clickRow("wobbleAsset");
    panel().clickButton("addInstanceButton");
    const curvy::NodeRef instance = selection().primary();
    ASSERT_TRUE(document().node(instance).isAssetInstance());
    const std::size_t depth = document().undoStack().depth();
    panel().clickButton("unlockButton");
    EXPECT_EQ(document().undoStack().depth(), depth + 1);
    EXPECT_FALSE(document().node(instance).isAssetInstance());
    EXPECT_NE(document().node(instance).childNetwork(), nullptr);
    EXPECT_EQ(panel().detail("instances"), "0 in this document");
}

namespace {

    // The Asset Properties dialog loaded from Resources/assetProperties.newui and driven without showing it.
    class AssetDialogTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            curvytest::sharedConsole();
        }

        void TearDown() override {
            ui_.reset();
            state_.reset();
            for (const std::string& name : names_) {
                if (AssetLibrary::instance().has(name)) {
                    AssetLibrary::instance().uninstall(name);
                }
            }
            WorkspaceTest::TearDown();
        }

        void open(curvy::AssetPropertiesSession session) {
            state_ = std::make_unique<curvy::AssetPropertiesSession>(std::move(session));
            dialog_.setName("assetProperties");
            ASSERT_TRUE(newui::Bundle::instance().loadDialog(dialog_));
            curvy::assetpropsdialog::Callbacks callbacks;
            callbacks.chooseSavePath = [this](const std::string& suggested) {
                suggested_ = suggested;
                return savePath_;
            };
            callbacks.changed = [this]() { ++changed_; };
            ui_ = std::make_unique<curvy::assetpropsdialog::Session>(dialog_.rootView(), *state_, callbacks);
            ui_->wire([this](newui::DialogResult result) { (result == newui::DialogResult::Ok ? closedOk_ : closedCancel_)++; });
            ui_->start();
            dialog_.rootView().updateLayout();
        }

        template <typename T>
        T* find(const char* name) {
            return dynamic_cast<T*>(dialog_.rootView().findView(name));
        }

        void type(const char* field, const std::string& text) {
            find<newui::TextField>(field)->setText(curvy::assetpropsdialog::widen(text));
        }

        std::string textOf(const char* field) { return curvy::assetpropsdialog::narrow(find<newui::TextField>(field)->text()); }
        void click(const char* button) { find<newui::Button>(button)->onClick(*find<newui::Button>(button)); }

        newui::Dialog dialog_;
        std::unique_ptr<curvy::AssetPropertiesSession> state_;
        std::unique_ptr<curvy::assetpropsdialog::Session> ui_;
        std::vector<std::string> names_;
        std::string savePath_, suggested_;
        int changed_ = 0, closedOk_ = 0, closedCancel_ = 0;
    };

}

TEST_F(AssetDialogTest, aBlankDialogNeedsANameAndCreatesTheAsset) {
    open(curvy::AssetPropertiesSession::blank());
    EXPECT_EQ(ui_->page(), 0u);
    EXPECT_TRUE(find<newui::View>("pageBasic")->isVisible());
    EXPECT_FALSE(find<newui::View>("pageParameters")->isVisible());
    EXPECT_EQ(find<newui::Label>("propsTitle")->text(), "New asset");
    EXPECT_EQ(find<newui::Label>("versionText")->text(), "1 (new)");
    EXPECT_NE(ui_->footerText().find("type name"), std::string::npos);
    EXPECT_FALSE(find<newui::Button>("propsSave")->isEnabled());
    EXPECT_EQ(find<newui::Button>("propsSave")->text(), "Create");

    type("typeName", "adTestBlank");
    names_.push_back("adTestBlank");
    type("typeLabel", "Blank");
    EXPECT_EQ(state_->working().name, "adTestBlank");
    EXPECT_EQ(state_->working().label, "Blank");
    EXPECT_EQ(ui_->footerText(), "Unsaved changes");
    EXPECT_TRUE(find<newui::Button>("propsSave")->isEnabled());

    click("propsApply");
    EXPECT_TRUE(AssetLibrary::instance().has("adTestBlank"));
    EXPECT_EQ(changed_, 1);
    EXPECT_EQ(ui_->footerText(), "No changes");
    EXPECT_EQ(find<newui::Button>("propsSave")->text(), "Save");

    type("typeAuthor", "Someone");
    click("propsRevert");
    EXPECT_EQ(textOf("typeAuthor"), "");
    EXPECT_EQ(closedOk_ + closedCancel_, 0);
    click("propsCancel");
    EXPECT_EQ(closedCancel_, 1);
}

TEST_F(AssetDialogTest, saveAsksForTheFileWritesItAndClosesTheDialog) {
    open(curvy::AssetPropertiesSession::blank());
    type("typeName", "adTestSave");
    names_.push_back("adTestSave");
    const std::filesystem::path path = curvytest::tempFile("adTestSave.asset.json");
    std::filesystem::remove(path);

    savePath_.clear();   // the user cancels the file dialog
    click("propsSave");
    EXPECT_EQ(suggested_, "adTestSave.asset.json");
    EXPECT_EQ(closedOk_, 0);
    EXPECT_FALSE(AssetLibrary::instance().has("adTestSave"));

    savePath_ = path.string();
    click("propsSave");
    EXPECT_EQ(closedOk_, 1);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(AssetLibrary::instance().source("adTestSave"), path.string());
    std::filesystem::remove(path);
}

TEST_F(AssetDialogTest, parametersAreAddedSelectedEditedAndRenamed) {
    open(curvy::AssetPropertiesSession::forAsset("wobbleAsset"));
    ui_->showPage(1);
    EXPECT_TRUE(find<newui::View>("pageParameters")->isVisible());
    EXPECT_EQ(ui_->selectedParam(), 0);
    EXPECT_EQ(textOf("paramName"), "amount");
    EXPECT_EQ(textOf("paramMin"), "0");
    EXPECT_EQ(textOf("paramMax"), "10");
    EXPECT_EQ(textOf("paramTooltip"), "How far points move");

    ui_->selectParam(1);
    EXPECT_EQ(textOf("paramName"), "speed");
    EXPECT_EQ(textOf("paramMax"), "");
    type("paramLabel", "Rate");
    type("paramMax", "20");
    EXPECT_EQ(state_->param(1).label, "Rate");
    ASSERT_TRUE(state_->param(1).maximum.has_value());
    EXPECT_EQ(*state_->param(1).maximum, 20.0f);
    type("paramMax", "");
    EXPECT_FALSE(state_->param(1).maximum.has_value());
    type("paramDefault", "not a number");
    EXPECT_EQ(state_->param(1).defaultValue, 3.0f);   // an invalid number is ignored

    type("paramName", "tempo");
    ASSERT_NE(find<newui::TextField>("paramName"), nullptr);
    find<newui::TextField>("paramName")->onReturnPressed(*find<newui::TextField>("paramName"));
    EXPECT_EQ(state_->param(1).name, "tempo");
    EXPECT_EQ(state_->working().renames.at("speed"), "tempo");   // the registered version had it
    EXPECT_NE(state_->working().graphText.find("parent.tempo"), std::string::npos);

    click("paramAdd");
    EXPECT_EQ(state_->paramCount(), 3u);
    EXPECT_EQ(ui_->selectedParam(), 2);
    EXPECT_EQ(textOf("paramName"), "param1");
    click("paramRemove");
    EXPECT_EQ(state_->paramCount(), 2u);
    EXPECT_EQ(ui_->selectedParam(), 1);
}

TEST_F(AssetDialogTest, thePythonPageShowsWhenAnEventRunsAndBlocksAScriptThatBreaksTheRules) {
    open(curvy::AssetPropertiesSession::forAsset("wobbleAsset"));
    ui_->showPage(3);
    EXPECT_GE(ui_->eventRowCount(), 8u);
    EXPECT_EQ(ui_->selectedEvent(), "module");
    EXPECT_NE(ui_->eventWhen().find("Once per session"), std::string::npos);
    ui_->selectEvent("onFrameChanged");
    EXPECT_NE(ui_->eventWhen().find("The playhead moved"), std::string::npos);
    EXPECT_EQ(find<newui::Label>("eventTitle")->text(), "onFrameChanged");
    EXPECT_EQ(find<newui::Label>("eventArgs")->text(), "node, frame, playing");
    EXPECT_FALSE(ui_->eventHasScriptMark("onFrameChanged"));

    ui_->typeScript("x = 1\nnode.set_float('amount', x)");
    EXPECT_TRUE(state_->hasScript("onFrameChanged"));
    EXPECT_TRUE(ui_->eventHasScriptMark("onFrameChanged"));
    EXPECT_NE(find<newui::Label>("pythonProblem")->text().find("2:1 cannot change this asset"), std::string::npos);
    EXPECT_NE(ui_->footerText().find("onFrameChanged 2:1"), std::string::npos);
    EXPECT_FALSE(find<newui::Button>("propsApply")->isEnabled());
    EXPECT_FALSE(find<newui::Button>("propsSave")->isEnabled());

    ui_->typeScript("print(node.get_float('amount'))");
    EXPECT_TRUE(find<newui::Button>("propsApply")->isEnabled());
    EXPECT_EQ(find<newui::Label>("pythonProblem")->text(), "");
    ui_->selectEvent("onCreated");
    EXPECT_FALSE(find<newui::Label>("eventTitle")->text() == "onFrameChanged");
    EXPECT_FALSE(state_->hasScript("onCreated"));
}

TEST_F(AssetDialogTest, theContentsPageNamesTheNodeCountAndTheBrushNoteAppearsForABrush) {
    open(curvy::AssetPropertiesSession::forAsset("wobbleAsset"));
    ui_->showPage(2);
    EXPECT_EQ(find<newui::Label>("contentsInfo")->text(), "2 nodes. Instances cannot be entered.");
    EXPECT_EQ(find<newui::Label>("brushNote")->text(), "");
    EXPECT_FALSE(find<newui::Button>("matchCurrent")->isEnabled());   // no callback: nothing to match against
    state_->setName("adTestBrush");
    jitnodes::AssetParam a;
    a.name = "referenceWidth";
    jitnodes::AssetParam b;
    b.name = "guideLength";
    state_->setParam(state_->addParam(), a);
    state_->setParam(state_->addParam(), b);
    ui_->refresh();
    EXPECT_NE(find<newui::Label>("brushNote")->text().find("brush"), std::string::npos);
}
