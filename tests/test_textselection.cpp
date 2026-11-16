#include "testsupport.h"

namespace {

    class TextSelectionTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            first_ = new newui::TextField();
            second_ = new newui::TextField();
            first_->setBounds(newui::Rect(0.0f, 0.0f, 100.0f, 24.0f));
            second_->setBounds(newui::Rect(0.0f, 30.0f, 100.0f, 24.0f));
            first_->setVisible(true);
            second_->setVisible(true);
            first_->setText(L"64");
            second_->setText(L"0");
            frame_.rootView().addChild(first_);   // the view tree owns them from here
            frame_.rootView().addChild(second_);
        }

        void focus(newui::TextField* field) { frame_.rootView().setFocusedSubView(field); }

        newui::TextField* first_ = nullptr;
        newui::TextField* second_ = nullptr;
    };

}

TEST_F(TextSelectionTest, aSelectionIsShownOnlyWhileItsFieldHasTheFocus) {
    focus(first_);
    first_->selectAll();
    EXPECT_FALSE(first_->selection().isEmpty());
    EXPECT_TRUE(first_->controller().showsSelection());

    focus(second_);
    EXPECT_FALSE(first_->controller().showsSelection()) << "the old field must not keep looking selected";
    EXPECT_FALSE(first_->selection().isEmpty()) << "its selection is still there, only not highlighted";
}

TEST_F(TextSelectionTest, theSelectionIsShownAgainWhenTheFocusComesBack) {
    focus(first_);
    first_->selectAll();
    focus(second_);
    focus(first_);
    EXPECT_TRUE(first_->controller().showsSelection());
}

TEST_F(TextSelectionTest, aFieldNobodySelectedInShowsNothing) {
    focus(first_);
    EXPECT_FALSE(first_->controller().showsSelection());
}

TEST_F(TextSelectionTest, aMultilineControlBehavesTheSame) {
    auto* control = new newui::TextControl();
    control->setBounds(newui::Rect(0.0f, 60.0f, 100.0f, 60.0f));
    control->setVisible(true);
    control->setText(L"one\ntwo");
    frame_.rootView().addChild(control);
    frame_.rootView().setFocusedSubView(control);
    control->selectAll();
    EXPECT_TRUE(control->controller().showsSelection());
    focus(first_);
    EXPECT_FALSE(control->controller().showsSelection());
}
