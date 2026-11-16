#include "testsupport.h"

#include "curvy/scripteditorpanel.h"

#include <filesystem>
#include <fstream>

namespace {

    class ScriptEditorPanelTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            panel_ = new curvy::ScriptEditorPanel();
            frame_.rootView().addChild(panel_);   // the view tree owns it from here
            file_ = std::filesystem::temp_directory_path() / "curvyScriptEditorTest.py";
            std::filesystem::remove(file_);
        }

        void TearDown() override {
            std::filesystem::remove(file_);
            WorkspaceTest::TearDown();
        }

        curvy::ScriptEditorPanel* panel_ = nullptr;
        std::filesystem::path file_;
    };

}

TEST_F(ScriptEditorPanelTest, theScriptTextRoundTrips) {
    panel_->setScript("a = 1\nprint(a)\n");
    EXPECT_EQ(panel_->script(), "a = 1\nprint(a)\n");
}

TEST_F(ScriptEditorPanelTest, saveThenLoadKeepsTheTextAndRemembersTheFile) {
    panel_->setScript("print('x')\n");
    std::string error;
    ASSERT_TRUE(panel_->save(file_, error)) << error;
    EXPECT_EQ(panel_->file(), file_);

    panel_->setScript("other");
    ASSERT_TRUE(panel_->load(file_, error)) << error;
    EXPECT_EQ(panel_->script(), "print('x')\n");
}

TEST_F(ScriptEditorPanelTest, loadDropsCarriageReturnsAndABom) {
    {
        std::ofstream out(file_, std::ios::binary);
        out << "\xEF\xBB\xBF" << "a = 1\r\nb = 2\r\n";
    }
    std::string error;
    ASSERT_TRUE(panel_->load(file_, error)) << error;
    EXPECT_EQ(panel_->script(), "a = 1\nb = 2\n");
}

TEST_F(ScriptEditorPanelTest, aMissingFileIsReportedAndLeavesTheTextAlone) {
    panel_->setScript("keep");
    std::string error;
    EXPECT_FALSE(panel_->load(file_, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(panel_->script(), "keep");
    EXPECT_TRUE(panel_->file().empty());
}

TEST_F(ScriptEditorPanelTest, runHandsTheTextAndTheFileNameToTheOwner) {
    std::string code;
    std::string name;
    panel_->onRun = [&](const std::string& text, const std::string& label) {
        code = text;
        name = label;
    };
    panel_->setScript("print(1)");
    panel_->run();
    EXPECT_EQ(code, "print(1)");
    EXPECT_EQ(name, "script");

    std::string error;
    ASSERT_TRUE(panel_->save(file_, error)) << error;
    panel_->run();
    EXPECT_EQ(name, "curvyScriptEditorTest.py");
}

TEST_F(ScriptEditorPanelTest, openAndSaveUseTheChosenPaths) {
    panel_->setScript("saved text");
    panel_->onChooseSave = [this](const std::filesystem::path&) { return file_; };
    panel_->saveToFile();
    EXPECT_TRUE(std::filesystem::exists(file_));

    panel_->setScript("");
    panel_->onChooseOpen = [this]() { return file_; };
    panel_->open();
    EXPECT_EQ(panel_->script(), "saved text");

    panel_->onChooseOpen = []() { return std::filesystem::path(); };
    panel_->setScript("untouched");
    panel_->open();
    EXPECT_EQ(panel_->script(), "untouched");
}
