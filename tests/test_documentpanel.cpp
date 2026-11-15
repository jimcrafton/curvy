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

TEST_F(DocumentPanelTest, SwatchesTabEditsTheSharedPalettes) {
    window().document().newBlank("Poster", 400.0f, 300.0f);
    panel().refresh();
    const std::size_t before = window().document().palettes().size();
    panel().strip().onNewPalette();
    EXPECT_EQ(before + 1, window().document().palettes().size());
    EXPECT_EQ(window().document().palettes().size() + 1, panel().strip().palettes().size());   // Recent is listed first
}
