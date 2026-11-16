#include "curvy/documentpanel.h"

#include "testsupport.h"

using curvy::DockSide;

namespace {

    class DocumentPanelTest : public curvytest::WorkspaceTest {
    protected:
        curvy::DocumentPanel& panel() { return window().documentPanel(); }
    };
}

TEST_F(DocumentPanelTest, LivesInTheRightDock) {
    EXPECT_EQ(&window().rightDock(), panel().parent());
    EXPECT_EQ(window().docks().area(DockSide::Right), panel().parent()->parent()->parent());
    EXPECT_EQ(2u, panel().tabs().tabCount());
    EXPECT_EQ("Document", panel().titleText());
}

TEST_F(DocumentPanelTest, DocumentTabShowsTheDocument) {
    window().document().newBlank("Poster", 400.0f, 300.0f);
    panel().refresh();
    EXPECT_EQ("Poster", panel().nameText());
    EXPECT_EQ("400 x 300 pt", panel().pageText());
}

TEST_F(DocumentPanelTest, thePanelAsksForTheHeightOfTheTabThatShows) {
    window().newDocument("Poster", 400.0f, 300.0f);
    window().docks().setCollapsed(DockSide::Right, false);
    panel().tabs().selectTab(0);
    const float documentTab = panel().bodyHeight();
    panel().tabs().selectTab(1);
    EXPECT_GT(panel().bodyHeight(), documentTab) << "the Swatches page has the palettes, the tiles, the details and the colours in use";
    EXPECT_FALSE(panel().userResized());

    const float before = panel().bodyHeight();
    std::vector<curvy::Swatch> many;
    for (int i = 0; i < 60; ++i) {
        many.push_back(curvy::Swatch{float(i) / 60.0f, 0.5f, 0.5f, 1.0f, ""});
    }
    window().document().addSwatches(0, many);
    EXPECT_GT(panel().bodyHeight(), before) << "more tiles, more rows, a taller panel";
}

TEST_F(DocumentPanelTest, aHeightTheUserDraggedIsNotChangedByTheContent) {
    window().newDocument("Poster", 400.0f, 300.0f);
    panel().restoreBodyHeight(200.0f);
    panel().tabs().selectTab(1);
    EXPECT_FLOAT_EQ(200.0f, panel().bodyHeight());
}

TEST_F(DocumentPanelTest, SwatchesTabEditsTheSharedPalettes) {
    window().document().newBlank("Poster", 400.0f, 300.0f);
    panel().refresh();
    const std::size_t before = window().document().palettes().size();
    for (const curvy::PanelMenuEntry& entry : window().swatchesTab().paletteMoreMenu()) {
        if (entry.text == "New palette") {
            entry.run();
        }
    }
    EXPECT_EQ(before + 1, window().document().palettes().size());
    EXPECT_EQ(window().document().palettes().size() + 1, window().swatchesTab().paletteRowCount());   // Recent is the last row
}
