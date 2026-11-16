#include "testsupport.h"

namespace {

    class SwatchActionsTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Swatches", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            layer_ = window().document().addLayer();
        }

        curvy::SwatchesTab& tab() { return window().swatchesTab(); }
        curvy::SelectionModel& selection() { return window().graph().selectionModel(); }
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
        std::size_t activeSize() { return window().document().palettes()[window().document().activePalette()].swatches.size(); }
        std::size_t indexOfRed(float red) {
            const auto& swatches = window().document().palettes()[window().document().activePalette()].swatches;
            for (std::size_t i = 0; i < swatches.size(); ++i) {
                if (std::abs(swatches[i].r - red) < 0.005f) {
                    return i;
                }
            }
            return swatches.size();
        }

        curvy::NodeRef layer_;
    };

}

TEST_F(SwatchActionsTest, theMenusOfferTheLines) {
    const auto more = tab().paletteMoreMenu();
    EXPECT_TRUE(has(more, "New palette"));
    EXPECT_TRUE(has(more, "Import palette (.ase, .aco)..."));
    EXPECT_TRUE(has(more, "New palette from image..."));
    EXPECT_TRUE(has(more, "Add colours in use"));
    const auto swatch = tab().swatchMenu(0);
    EXPECT_TRUE(has(swatch, "Use"));
    EXPECT_TRUE(has(swatch, "Select shapes using this"));
    EXPECT_TRUE(has(swatch, "Link"));
}

TEST_F(SwatchActionsTest, addColoursInUseAddsOnlyWhatNoPaletteHasAsOneUndoStep) {
    window().document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(0.123f, 0.456f, 0.789f, 1.0f));
    window().document().addCircle(layer_, 200.0f, 100.0f, 30.0f, newui::Color(0.123f, 0.456f, 0.789f, 1.0f));   // the same colour: one swatch
    tab().refresh();
    EXPECT_GE(tab().inUseCount(), 1u);
    const std::size_t before = activeSize();
    choose(tab().paletteMoreMenu(), "Add colours in use");
    const std::size_t added = activeSize();
    ASSERT_GT(added, before);
    EXPECT_LT(indexOfRed(0.123f), added);
    EXPECT_EQ(tab().inUseCount(), 0u) << "the list is read again after the edit";

    choose(tab().paletteMoreMenu(), "Add colours in use");
    EXPECT_EQ(activeSize(), added) << "now every colour in use is in a palette, and the twin circle did not add it twice";
    EXPECT_EQ(window().workspace().hintKeys(), "Every colour in use is already in a palette");

    window().document().undoStack().undo();
    EXPECT_EQ(activeSize(), before);
}

TEST_F(SwatchActionsTest, selectShapesUsingASwatchSelectsTheShapesThatCarryItsColour) {
    const curvy::NodeRef first = window().document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(0.123f, 0.456f, 0.789f, 1.0f));
    window().document().addCircle(layer_, 300.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    tab().refresh();
    choose(tab().paletteMoreMenu(), "Add colours in use");
    const std::size_t index = indexOfRed(0.123f);
    ASSERT_LT(index, activeSize());
    ASSERT_TRUE(selection().empty());

    choose(tab().swatchMenu(index), "Select shapes using this");
    ASSERT_EQ(selection().items().size(), 1u);
    EXPECT_EQ(window().document().node(selection().primary()).typeName(), "circle");
    EXPECT_EQ(selection().fill(), first) << "the circle whose fill has the colour, not the red one";
}

TEST_F(SwatchActionsTest, aColourNothingUsesSaysSoAndSelectsNothing) {
    choose(tab().swatchMenu(0), "Select shapes using this");   // black: no shape has it
    EXPECT_TRUE(selection().empty());
    EXPECT_EQ(window().workspace().hintKeys(), "No shape uses this colour");
}
