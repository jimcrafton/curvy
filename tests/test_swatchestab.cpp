#include "testsupport.h"

namespace {

    class SwatchesTabTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Tab", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            layer_ = window().document().addLayer();
            tab().refresh();
        }

        curvy::SwatchesTab& tab() { return window().swatchesTab(); }
        curvy::SketchDocument& document() { return window().document(); }
        void choose(const std::vector<curvy::PanelMenuEntry>& menu, const std::string& text) {
            for (const curvy::PanelMenuEntry& entry : menu) {
                if (entry.text == text) {
                    entry.run();
                    return;
                }
            }
            FAIL() << "no menu line \"" << text << "\"";
        }
        bool has(const std::vector<curvy::PanelMenuEntry>& menu, const std::string& text) {
            for (const curvy::PanelMenuEntry& entry : menu) {
                if (entry.text == text) {
                    return true;
                }
            }
            return false;
        }

        curvy::NodeRef layer_;
    };

}

TEST_F(SwatchesTabTest, showsEveryPaletteAsARowWithRecentLastAndTheOpenOnesTiles) {
    const std::size_t defaults = document().palettes()[0].swatches.size();
    EXPECT_EQ(tab().paletteRowCount(), 2u) << "the default palette and Recent";
    EXPECT_EQ(tab().activeRow(), 0u);
    EXPECT_EQ(tab().paletteName(), document().palettes()[0].name);
    EXPECT_EQ(tab().tileCount(), defaults);
    EXPECT_EQ(tab().selectedIndex(), 0) << "the first swatch is selected";
    EXPECT_EQ(tab().nameText(), document().palettes()[0].swatches[0].name);
    EXPECT_EQ(tab().hexText(), "#000000");
}

TEST_F(SwatchesTabTest, everyColourIsReallyDrawnNotJustItsLabel) {
    document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(0.123f, 0.456f, 0.789f, 1.0f));
    tab().refresh();
    tab().layoutFor(268.0f, 400.0f);
    ASSERT_GT(tab().tileCount(), 0u);
    for (std::size_t i = 0; i < tab().tileCount(); ++i) {
        EXPECT_TRUE(tab().tileDrawn(i)) << "tile " << i;
    }
    EXPECT_TRUE(tab().paletteStripDrawn(0));
    document().noteRecentColor(curvy::Swatch{0.1f, 0.2f, 0.3f, 1.0f, ""});
    tab().refresh();
    tab().layoutFor(268.0f, 400.0f);
    EXPECT_TRUE(tab().paletteStripDrawn(1)) << "Recent's strip";
    ASSERT_GT(tab().inUseCount(), 0u);
    EXPECT_TRUE(tab().inUseChipDrawn(0));
}

TEST_F(SwatchesTabTest, withNoDocumentOpenItSaysSoInsteadOfShowingAPalette) {
    tab().setDocumentOpen(false);
    tab().layoutFor(268.0f, 400.0f);
    EXPECT_TRUE(tab().emptyNoteShown());
    EXPECT_EQ(tab().emptyNoteText(), "Nothing is open. The swatches of a document show here.");
    EXPECT_FALSE(tab().tileDrawn(0));
    EXPECT_FALSE(tab().paletteStripDrawn(0));
    EXPECT_LT(tab().contentHeight(), 100.0f);

    tab().setDocumentOpen(true);
    tab().layoutFor(268.0f, 400.0f);
    EXPECT_FALSE(tab().emptyNoteShown());
    EXPECT_TRUE(tab().tileDrawn(0));
}

TEST_F(SwatchesTabTest, aClickOnAPaletteRowOpensItAndRecentShowsTheRecentColours) {
    const std::size_t second = document().addPalette("Sunset");
    document().addSwatch(second, curvy::Swatch{0.9f, 0.3f, 0.2f, 1.0f, "Ember"});
    EXPECT_EQ(tab().paletteRowCount(), 3u) << "the tab follows the document's palettes by itself";

    tab().selectPalette(second);
    EXPECT_EQ(document().activePalette(), second);
    EXPECT_EQ(tab().paletteName(), "Sunset");
    EXPECT_EQ(tab().tileCount(), 1u);
    EXPECT_EQ(tab().nameText(), "Ember");

    document().noteRecentColor(curvy::Swatch{0.1f, 0.2f, 0.3f, 1.0f, ""});
    tab().selectPalette(document().palettes().size());   // Recent is the row after the palettes
    EXPECT_EQ(tab().paletteName(), "Recent");
    EXPECT_EQ(tab().activeRow(), document().palettes().size());
    EXPECT_EQ(tab().tileCount(), 1u);
    EXPECT_EQ(tab().usedByText(), "A recent colour: save it to a palette to keep it");
    EXPECT_FALSE(tab().selectThemEnabled());
}

TEST_F(SwatchesTabTest, selectingASwatchShowsItsDetailsAndHowItIsUsed) {
    const std::size_t index = 1;
    tab().selectSwatch(index);
    const curvy::Swatch& white = document().palettes()[0].swatches[index];
    EXPECT_EQ(tab().nameText(), white.name);
    EXPECT_EQ(tab().usedByText(), "Not used by any shape");
    EXPECT_FALSE(tab().selectThemEnabled());

    document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(white.r, white.g, white.b, 1.0f));
    tab().selectSwatch(0);
    tab().selectSwatch(index);
    EXPECT_EQ(tab().usedByText(), "Used 1 time in 1 group");
    EXPECT_TRUE(tab().selectThemEnabled());
}

TEST_F(SwatchesTabTest, aTypedHexRecoloursTheSwatchAsOneUndoStep) {
    tab().selectSwatch(2);
    const curvy::Swatch before = document().palettes()[0].swatches[2];
    tab().setHex("#336699");
    const curvy::Swatch& after = document().palettes()[0].swatches[2];
    EXPECT_NEAR(after.r, 0x33 / 255.0f, 0.003f);
    EXPECT_NEAR(after.b, 0x99 / 255.0f, 0.003f);
    EXPECT_EQ(after.name, before.name) << "the name stays";
    EXPECT_EQ(tab().hexText(), "#336699");
    document().undoStack().undo();
    EXPECT_TRUE(document().palettes()[0].swatches[2].sameColor(before));
}

TEST_F(SwatchesTabTest, textThatIsNotAColourIsRefusedAndChangesNothing) {
    tab().selectSwatch(2);
    const curvy::Swatch before = document().palettes()[0].swatches[2];
    tab().setHex("blue-ish");
    EXPECT_TRUE(document().palettes()[0].swatches[2].sameColor(before));
    EXPECT_NE(tab().statusText().find("is not a colour"), std::string::npos);
    EXPECT_EQ(tab().hexText(), curvy::NodePanel::colorText(before.r, before.g, before.b));
}

TEST_F(SwatchesTabTest, aTypedNameRenamesTheSwatchAndUndoPutsItBack) {
    tab().selectSwatch(3);
    const std::string before = document().palettes()[0].swatches[3].name;
    tab().setName("Dusk");
    EXPECT_EQ(document().palettes()[0].swatches[3].name, "Dusk");
    document().undoStack().undo();
    EXPECT_EQ(document().palettes()[0].swatches[3].name, before);
}

TEST_F(SwatchesTabTest, theLinkedToggleLinksAndUnlinks) {
    tab().selectSwatch(2);
    EXPECT_FALSE(tab().linkedChecked());
    tab().setLinked(true);
    EXPECT_NE(document().palettes()[0].swatches[2].link, 0u);
    EXPECT_TRUE(tab().linkedChecked());
    tab().setLinked(false);
    EXPECT_EQ(document().palettes()[0].swatches[2].link, 0u);
}

TEST_F(SwatchesTabTest, coloursInUseAreListedAndAddedOneAtATimeOrAllAtOnce) {
    document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(0.123f, 0.456f, 0.789f, 1.0f));
    document().addCircle(layer_, 300.0f, 100.0f, 30.0f, newui::Color(0.321f, 0.654f, 0.987f, 1.0f));
    tab().refresh();
    ASSERT_EQ(tab().inUseCount(), 2u);
    const std::size_t before = document().palettes()[0].swatches.size();

    tab().addInUse(0);
    EXPECT_EQ(document().palettes()[0].swatches.size(), before + 1);
    EXPECT_EQ(tab().inUseCount(), 1u);
    tab().addAllInUse();
    EXPECT_EQ(document().palettes()[0].swatches.size(), before + 2);
    EXPECT_EQ(tab().inUseCount(), 0u);
}

TEST_F(SwatchesTabTest, thePaletteMenusMakeDuplicateRenameAndDeletePalettes) {
    const std::size_t before = document().palettes().size();
    choose(tab().paletteMoreMenu(), "New palette");
    ASSERT_EQ(document().palettes().size(), before + 1);
    EXPECT_EQ(document().activePalette(), before);
    EXPECT_EQ(tab().tileCount(), 0u);

    tab().renameActivePalette("Mine");
    EXPECT_EQ(document().palettes()[before].name, "Mine");
    EXPECT_EQ(tab().paletteName(), "Mine");

    choose(tab().paletteMoreMenu(), "Duplicate");
    EXPECT_EQ(document().palettes().size(), before + 2);

    choose(tab().paletteMoreMenu(), "Delete");
    EXPECT_EQ(document().palettes().size(), before + 1);
    document().undoStack().undo();
    EXPECT_EQ(document().palettes().size(), before + 2) << "undo brings the palette back";
}

TEST_F(SwatchesTabTest, theOnlyPaletteCannotBeDeletedAndRecentOffersNoEdits) {
    EXPECT_FALSE(has(tab().paletteMoreMenu(), "Delete"));
    tab().selectPalette(document().palettes().size());
    const auto more = tab().paletteMoreMenu();
    EXPECT_FALSE(has(more, "Duplicate"));
    EXPECT_FALSE(has(more, "Rename..."));
    EXPECT_FALSE(has(more, "Add colours in use"));
    EXPECT_TRUE(has(more, "New palette from image..."));
}

TEST_F(SwatchesTabTest, thePaletteListMenuTicksTheOpenPaletteAndSwitches) {
    document().addPalette("Sunset");
    const auto list = tab().paletteListMenu();
    ASSERT_GE(list.size(), 4u);
    EXPECT_TRUE(list[0].checked);
    EXPECT_FALSE(list[1].checked);
    list[1].run();
    EXPECT_EQ(document().activePalette(), 1u);
    EXPECT_TRUE(tab().paletteListMenu()[1].checked);
}

TEST_F(SwatchesTabTest, useAsFillReportsTheSwatchToTheOwner) {
    curvy::Swatch used;
    tab().onUse = [&](const curvy::Swatch& swatch) { used = swatch; };
    tab().selectSwatch(2);
    tab().useSelected();
    EXPECT_TRUE(used.sameColor(document().palettes()[0].swatches[2]));
}

TEST_F(SwatchesTabTest, useAsFillRecoloursTheSelectedShapesFillAsOneUndoStep) {
    document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    window().graph().selectionModel().selectShapeKey(std::string(document().artwork()->shapes().front().key));
    const curvy::NodeRef fill = window().graph().selectionModel().fill();
    ASSERT_TRUE(fill.valid());
    tab().selectSwatch(1);   // white
    tab().useSelected();
    EXPECT_NEAR(document().node(fill).getFloat("r"), document().palettes()[0].swatches[1].r, 1e-4f);
    document().undoStack().undo();
    EXPECT_NEAR(document().node(fill).getFloat("r"), 1.0f, 1e-4f);
}

TEST_F(SwatchesTabTest, useAsFillWithNothingSelectedAsksForAShape) {
    tab().selectSwatch(1);
    tab().useSelected();
    EXPECT_EQ(window().workspace().hintKeys(), "Select a shape first, then use the swatch on its fill");
}

TEST_F(SwatchesTabTest, theContentGrowsWithTheColoursAndFitsTheWidth) {
    tab().layoutFor(268.0f, 400.0f);
    const float compact = tab().contentHeight();
    EXPECT_GT(compact, 100.0f);
    std::vector<curvy::Swatch> many;
    for (int i = 0; i < 40; ++i) {
        many.push_back(curvy::Swatch{float(i) / 40.0f, 0.5f, 0.5f, 1.0f, ""});
    }
    document().addSwatches(0, many);
    tab().layoutFor(268.0f, 400.0f);
    EXPECT_GT(tab().contentHeight(), compact) << "more tiles, more rows";
    const float narrow = tab().contentHeight();
    tab().layoutFor(150.0f, 400.0f);
    EXPECT_GT(tab().contentHeight(), narrow) << "a narrower tab wraps the tiles onto more rows";
}

TEST_F(SwatchesTabTest, theScrollRangeFollowsTheContentWhenColoursAreAddedWithoutAResize) {
    tab().layoutFor(268.0f, 400.0f);
    std::vector<curvy::Swatch> many;
    for (int i = 0; i < 200; ++i) {
        many.push_back(curvy::Swatch{float(i) / 200.0f, 0.5f, 0.5f, 1.0f, ""});
    }
    document().addSwatches(0, many);
    tab().refresh();
    EXPECT_GT(tab().contentHeight(), 400.0f);
    EXPECT_GE(tab().scrollableHeight(), tab().contentHeight()) << "an import must be scrollable to its last row";
}

TEST_F(SwatchesTabTest, theTabHasASizeAsSoonAsItsPageIsChosenNotOnlyAfterTheDockIsResized) {
    window().docks().setCollapsed(curvy::DockSide::Right, false);
    frame_.rootView().updateLayout();
    window().documentPanel().tabs().selectTab(0);   // the Document tab shows first, as at start up
    window().documentPanel().tabs().selectTab(1);
    EXPECT_TRUE(tab().hasSize()) << "a blank tab: the page was never laid out when it was shown";
    EXPECT_TRUE(tab().tileDrawn(0));
}

TEST_F(SwatchesTabTest, choosingTheSwatchesTabReadsWhatIsInUseAgain) {
    window().docks().setCollapsed(curvy::DockSide::Right, false);
    document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(0.123f, 0.456f, 0.789f, 1.0f));
    EXPECT_EQ(tab().inUseCount(), 0u) << "shapes drawn after the last read do not change the list by themselves";
    window().documentPanel().tabs().selectTab(1);
    EXPECT_EQ(tab().inUseCount(), 1u);
}

TEST(TextFieldSelectAll, selectsTheWholeTextWithTheCaretAtTheEnd) {
    newui::TextField field;
    field.setText(L"Ember");
    EXPECT_TRUE(field.selection().isEmpty());
    field.selectAll();
    EXPECT_FALSE(field.selection().isEmpty());
    field.setText(L"");
    field.selectAll();
    EXPECT_TRUE(field.selection().isEmpty()) << "nothing to select in an empty field";

    newui::TextControl control;
    control.setText(L"two\nlines");
    control.selectAll();
    EXPECT_FALSE(control.selection().isEmpty());
}

TEST_F(SwatchesTabTest, renamingASwatchFromTheMenuSelectsItsNameReadyToTypeOver) {
    tab().selectSwatch(3);
    choose(tab().swatchMenu(3), "Rename...");
    auto* field = dynamic_cast<newui::TextField*>(window().documentPanel().findView("detailName"));
    ASSERT_NE(field, nullptr);
    EXPECT_FALSE(field->selection().isEmpty());
}

TEST_F(SwatchesTabTest, theMenusAskTheOwnerToShowThemWhereTheButtonsAre) {
    std::size_t shown = 0;
    window().presentMenu = [&](const std::vector<curvy::PanelMenuEntry>& entries, const newui::Point&) { shown = entries.size(); };
    auto* more = dynamic_cast<newui::Button*>(window().documentPanel().findView("moreButton"));
    ASSERT_NE(more, nullptr);
    more->onClick(*more);
    EXPECT_GE(shown, 4u);
}

TEST_F(SwatchesTabTest, draggingATileOntoAnotherMovesTheSwatchAsOneUndoStepAndKeepsItSelected) {
    const std::vector<curvy::Swatch> before = document().palettes()[0].swatches;
    ASSERT_GE(before.size(), 4u);
    tab().selectSwatch(0);
    tab().pressTile(0, tab().tileCenter(0));
    tab().dragTileTo(tab().tileCenter(3));
    EXPECT_TRUE(tab().tileDragging());
    EXPECT_EQ(tab().dropTarget(), 3);
    tab().releaseTileAt(tab().tileCenter(3));

    const std::vector<curvy::Swatch>& after = document().palettes()[0].swatches;
    EXPECT_EQ(after[3].name, before[0].name);
    EXPECT_EQ(after[0].name, before[1].name);
    EXPECT_EQ(tab().selectedIndex(), 3);
    EXPECT_FALSE(tab().tileDragging());

    document().undoStack().undo();
    EXPECT_EQ(document().palettes()[0].swatches[0].name, before[0].name);
}

TEST_F(SwatchesTabTest, aPressThatBarelyMovesIsAClickNotADrag) {
    const std::vector<curvy::Swatch> before = document().palettes()[0].swatches;
    tab().pressTile(1, tab().tileCenter(1));
    tab().dragTileTo(newui::Point(tab().tileCenter(1).x + 1.0f, tab().tileCenter(1).y));
    EXPECT_FALSE(tab().tileDragging());
    tab().releaseTileAt(tab().tileCenter(2));
    EXPECT_EQ(document().palettes()[0].swatches[1].name, before[1].name);
}

TEST_F(SwatchesTabTest, aDragReleasedOffTheTilesOrInRecentChangesNothing) {
    const std::vector<curvy::Swatch> before = document().palettes()[0].swatches;
    tab().pressTile(0, tab().tileCenter(0));
    tab().dragTileTo(newui::Point(-50.0f, -50.0f));
    tab().releaseTileAt(newui::Point(-50.0f, -50.0f));
    EXPECT_EQ(document().palettes()[0].swatches[0].name, before[0].name);

    tab().selectPalette(document().palettes().size());
    tab().pressTile(0, tab().tileCenter(0));
    tab().dragTileTo(tab().tileCenter(1));
    EXPECT_FALSE(tab().tileDragging());
    tab().releaseTileAt(tab().tileCenter(1));
    EXPECT_EQ(document().palettes()[0].swatches[0].name, before[0].name);
}
