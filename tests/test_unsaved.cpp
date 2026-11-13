#include "testsupport.h"

using curvytest::WorkspaceTest;
using newui::UnsavedChangesChoice;

namespace {

    class UnsavedChanges : public WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Unsaved", 612.0f, 792.0f);
            window().documents().setUnsavedChangesHandler([this](newui::Document&) {
                ++asked_;
                return answer_;
            });
        }

        void edit() {
            window().document().addLayer("Edited");
        }

        int asked_ = 0;
        UnsavedChangesChoice answer_ = UnsavedChangesChoice::Cancel;
    };

}

TEST_F(UnsavedChanges, theDocumentControllerOwnsTheDocument) {
    const auto& open = window().documents().documents();
    ASSERT_EQ(open.size(), 1u);
    EXPECT_EQ(open[0], &window().document());
    EXPECT_EQ(window().documents().activeDocument(), &window().document());
}

TEST_F(UnsavedChanges, aFreshDocumentIsNotModifiedAndTheTitleHasNoStar) {
    EXPECT_FALSE(window().document().isModified());
    EXPECT_EQ(frame_.getTitle(), "curvy - Unsaved");
    EXPECT_TRUE(window().confirmDiscard());
    EXPECT_EQ(asked_, 0) << "nothing to lose, so nothing to ask";
}

TEST_F(UnsavedChanges, anEditSetsTheFlagAndTheTitleStarFollowsIt) {
    edit();
    EXPECT_TRUE(window().document().isModified());
    EXPECT_EQ(frame_.getTitle(), "curvy - Unsaved *");
}

TEST_F(UnsavedChanges, movingANodeModifiesTheDocument) {
    edit();
    const std::string file = curvytest::tempFile("curvyUnsavedMove.curvy").string();
    ASSERT_TRUE(window().document().save(file));
    window().showTitle();
    ASSERT_FALSE(window().document().isModified());
    EXPECT_EQ(frame_.getTitle(), "curvy - Unsaved");

    const curvy::NodeRef layer = window().document().childrenOf()[0];
    const jitnodes::GraphSnapshot before = window().document().snapshotGraph();
    window().document().setGraphPositionsLive(0, {{layer.id, {120.0f, 80.0f}}});
    window().document().commitGraphEdit(before, "Move Node");
    EXPECT_TRUE(window().document().isModified());
    EXPECT_EQ(frame_.getTitle(), "curvy - Unsaved *");
    std::filesystem::remove(file);
}

TEST_F(UnsavedChanges, cancelKeepsTheEditsAndDiscardLetsThemGo) {
    edit();
    answer_ = UnsavedChangesChoice::Cancel;
    EXPECT_FALSE(window().confirmDiscard());
    EXPECT_EQ(asked_, 1);
    EXPECT_TRUE(window().document().isModified());

    answer_ = UnsavedChangesChoice::Discard;
    EXPECT_TRUE(window().confirmDiscard());
    EXPECT_EQ(asked_, 2);
}

TEST_F(UnsavedChanges, saveWritesTheFileAndClearsTheFlag) {
    edit();
    const std::filesystem::path file = curvytest::tempFile("curvyUnsavedSave.curvy");
    std::filesystem::remove(file);
    window().documents().setSavePathProvider([&](newui::Document&) { return file.string(); });
    answer_ = UnsavedChangesChoice::Save;
    EXPECT_TRUE(window().confirmDiscard());
    EXPECT_TRUE(std::filesystem::exists(file));
    EXPECT_FALSE(window().document().isModified());
    std::filesystem::remove(file);
}

TEST_F(UnsavedChanges, aCancelledSaveAsKeepsTheDocumentOpen) {
    edit();
    window().documents().setSavePathProvider([](newui::Document&) { return std::string(); });
    answer_ = UnsavedChangesChoice::Save;
    EXPECT_FALSE(window().confirmDiscard());
    EXPECT_TRUE(window().document().isModified());
}

TEST_F(UnsavedChanges, closingTheWindowAsksAndCancelKeepsItOpen) {
    edit();
    answer_ = UnsavedChangesChoice::Cancel;
    ::SendMessage(frame_.frameHandle(), WM_CLOSE, 0, 0);
    EXPECT_EQ(asked_, 1);
    EXPECT_TRUE(::IsWindow(frame_.frameHandle()));
}

TEST_F(UnsavedChanges, anUneditedDocumentClosesWithoutAsking) {
    EXPECT_TRUE(window().confirmDiscard());
    EXPECT_EQ(asked_, 0);
}
