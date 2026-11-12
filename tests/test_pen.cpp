#include "testsupport.h"

#include <newui/mouse_constants.h>
#include <newui/pen.h>

using curvy::Settings;

namespace {

    class PenTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Pen", 595.0f, 842.0f);
            barrel_.inRange = barrel_.barrel = true;
            eraser_.inRange = eraser_.eraser = true;
        }

        // Closes the ring the way the controller does when a key closes it.
        void closeRing() {
            if (window().menu().isOpen()) {
                window().menu().handleEscape();
            }
            window().workspace().setRing(std::string());
        }

        newui::PenState mouse_, barrel_, eraser_;
        const newui::Point at_{300.0f, 300.0f};
    };
}

TEST_F(PenTest, AMouseRightClickOpensTheRingsWhateverTheBarrelSettingIs) {
    settings_.barrelButton = Settings::PenButton::Nothing;
    EXPECT_TRUE(window().pressed(mouse_, at_, newui::mbmRightButton, 0));
    EXPECT_TRUE(window().menu().isOpen());
}

TEST_F(PenTest, TheBarrelButtonOpensTheRingsWhenSetTo) {
    settings_.barrelButton = Settings::PenButton::Rings;
    EXPECT_TRUE(window().pressed(barrel_, at_, newui::mbmRightButton, 0));
    EXPECT_TRUE(window().menu().isOpen());
}

TEST_F(PenTest, TheBarrelButtonDoesNothingWhenSetTo) {
    settings_.barrelButton = Settings::PenButton::Nothing;
    EXPECT_TRUE(window().pressed(barrel_, at_, newui::mbmRightButton, 0));
    EXPECT_FALSE(window().menu().isOpen());
}

TEST_F(PenTest, TheBarrelButtonPansWhileHeldWhenSetTo) {
    settings_.barrelButton = Settings::PenButton::Pan;
    curvy::CanvasView& canvas = window().canvas();
    EXPECT_TRUE(window().pressed(barrel_, at_, newui::mbmRightButton, 0));
    EXPECT_FALSE(window().menu().isOpen());
    const float x0 = canvas.panX(), y0 = canvas.panY();
    canvas.onMouseMove(canvas, newui::Point(310.0f, 325.0f), newui::mbmRightButton, 0);
    canvas.onMouseMove(canvas, newui::Point(330.0f, 320.0f), newui::mbmRightButton, 0);
    EXPECT_EQ(30.0f, canvas.panX() - x0);
    EXPECT_EQ(20.0f, canvas.panY() - y0);
    canvas.onMouseUp(canvas, newui::Point(330.0f, 320.0f), newui::mbmRightButton, 0);
    canvas.onMouseMove(canvas, newui::Point(400.0f, 400.0f), 0, 0);
    EXPECT_EQ(30.0f, canvas.panX() - x0) << "letting go ends the pan";
    EXPECT_EQ(20.0f, canvas.panY() - y0);
}

TEST_F(PenTest, TheEraserEndDoesNothingWhenSetTo) {
    window().document().addGraphNode("attribCreate");
    settings_.eraserEnd = Settings::EraserAction::Nothing;
    EXPECT_TRUE(window().pressed(eraser_, at_, newui::mbmLeftButton, 0));
    EXPECT_TRUE(window().document().undoStack().canUndo());
}

TEST_F(PenTest, TheEraserEndUndoesWhenSetTo) {
    window().document().addGraphNode("attribCreate");
    settings_.eraserEnd = Settings::EraserAction::Undo;
    EXPECT_TRUE(window().pressed(eraser_, at_, newui::mbmLeftButton, 0));
    EXPECT_FALSE(window().document().undoStack().canUndo());
    EXPECT_TRUE(window().pressed(eraser_, at_, newui::mbmLeftButton, 0));
    EXPECT_NE(std::string::npos, window().workspace().hintKeys().find("Nothing to undo"));
}

TEST_F(PenTest, EraseSaysItIsNotWiredYet) {
    settings_.eraserEnd = Settings::EraserAction::Erase;
    window().pressed(eraser_, at_, newui::mbmLeftButton, 0);
    EXPECT_NE(std::string::npos, window().workspace().hintKeys().find("erase"));
}
