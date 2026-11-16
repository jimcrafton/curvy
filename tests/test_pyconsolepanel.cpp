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

TEST_F(PyConsolePanelTest, clearEmptiesTheTranscript) {
    panel_->execute("print(1)");
    panel_->clear();
    EXPECT_TRUE(panel_->transcript().empty());
}
