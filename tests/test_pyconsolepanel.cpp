#include "pythonconsole.h"
#include "testsupport.h"

#include "curvy/pyconsolepanel.h"
#include "curvy/sketchdocument.h"

namespace {

    class PyConsolePanelTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            document_.newBlank("Panel", 612.0f, 792.0f);
            panel_ = new curvy::PyConsolePanel(curvytest::sharedConsole(), &document_);
            frame_.rootView().addChild(panel_);   // the view tree owns it from here
        }

        curvy::SketchDocument document_;
        curvy::PyConsolePanel* panel_ = nullptr;
    };

}

TEST_F(PyConsolePanelTest, executeEchoesTheLineAndShowsItsOutput) {
    panel_->execute("print('hi')");
    EXPECT_EQ(panel_->transcript(), L">>> print('hi')\nhi\n");
}

TEST_F(PyConsolePanelTest, anErrorShowsTheTraceback) {
    panel_->execute("1/0");
    EXPECT_NE(panel_->transcript().find(L"ZeroDivisionError"), std::wstring::npos);
}

TEST_F(PyConsolePanelTest, blankInputDoesNothing) {
    panel_->execute("  \n");
    EXPECT_TRUE(panel_->transcript().empty());
}

TEST_F(PyConsolePanelTest, scriptsEditTheDocumentAndTheOwnerIsTold) {
    int executed = 0;
    panel_->onExecuted = [&executed]() { ++executed; };
    panel_->execute("doc.set_page_size(50, 60)");
    EXPECT_FLOAT_EQ(document_.pageWidth(), 50.0f);
    EXPECT_EQ(executed, 1);
}

TEST_F(PyConsolePanelTest, theTranscriptSaysTheScriptIsRunningUntilItReturns) {
    std::wstring seen;
    panel_->onBeforeRun = [this, &seen]() { seen = panel_->transcript(); };
    panel_->execute("x = 1");
    EXPECT_EQ(seen, L">>> x = 1\nrunning... (Esc cancels)\n");
    EXPECT_EQ(panel_->transcript(), L">>> x = 1\n");
}

TEST_F(PyConsolePanelTest, upAndDownWalkTheHistory) {
    panel_->execute("a = 1");
    panel_->execute("b = 2");
    panel_->recall(-1);
    EXPECT_EQ(panel_->input(), "b = 2");
    panel_->recall(-1);
    EXPECT_EQ(panel_->input(), "a = 1");
    panel_->recall(1);
    EXPECT_EQ(panel_->input(), "b = 2");
    panel_->recall(1);
    EXPECT_EQ(panel_->input(), "");
}

TEST_F(PyConsolePanelTest, theTranscriptStaysScrolledToItsLastLine) {
    auto* output = panel_->findView<newui::TextControl>("pyConsoleOutput");
    ASSERT_NE(output, nullptr);
    for (int i = 0; i < 30; ++i) {
        panel_->execute("print('line')");
    }
    panel_->execute("doc.color_mode = 'lab'");
    output->controller().ensureLayoutUpToDate();
    const float visible = output->bounds().height();
    const float expected = output->controller().contentHeight() - visible;
    ASSERT_GT(expected, 0.0f) << "the transcript should overflow the output view";
    EXPECT_NEAR(output->controller().scrollOffsetY(), expected, 1.0f);
    EXPECT_NE(panel_->transcript().find(L"color_mode is"), std::wstring::npos);

    auto* scroll = panel_->findView<newui::ScrollView>("pyConsoleScroll");
    ASSERT_NE(scroll, nullptr);
    EXPECT_TRUE(scroll->vBar()->isVisible()) << "a transcript taller than the view gets a vertical scroll bar";
    EXPECT_NEAR(scroll->vBar()->value(), scroll->vBar()->maxValue() - scroll->vBar()->pageSize(), 1.0f);
}

TEST_F(PyConsolePanelTest, aShortTranscriptHasNoScrollBar) {
    panel_->execute("print('one line')");
    auto* scroll = panel_->findView<newui::ScrollView>("pyConsoleScroll");
    ASSERT_NE(scroll, nullptr);
    EXPECT_FALSE(scroll->vBar()->isVisible());
}

TEST_F(PyConsolePanelTest, scrollingBackThenRunningSomethingJumpsToTheEnd) {
    auto* scroll = panel_->findView<newui::ScrollView>("pyConsoleScroll");
    ASSERT_NE(scroll, nullptr);
    for (int i = 0; i < 30; ++i) {
        panel_->execute("print('line')");
    }
    scroll->vBar()->setValue(0.0f);
    EXPECT_FLOAT_EQ(scroll->vBar()->value(), 0.0f);
    panel_->execute("print('newest')");
    EXPECT_NEAR(scroll->vBar()->value(), scroll->vBar()->maxValue() - scroll->vBar()->pageSize(), 1.0f);
}

TEST_F(PyConsolePanelTest, clearEmptiesTheTranscript) {
    panel_->execute("print(1)");
    panel_->clear();
    EXPECT_TRUE(panel_->transcript().empty());
}
