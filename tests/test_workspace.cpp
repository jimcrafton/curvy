#include "testsupport.h"

using curvytest::WorkspaceTest;

TEST_F(WorkspaceTest, StartsWithNothingOpen) {
    EXPECT_FALSE(window().workspace().documentOpen());
    EXPECT_EQ(nullptr, window().canvas().document()) << "the canvas shows no page at start";
}

TEST_F(WorkspaceTest, TheNewRingStartsADocument) {
    window().openRing(false);
    ASSERT_TRUE(window().menu().isOpen());
    EXPECT_EQ("New", window().menu().ring().title);
    EXPECT_TRUE(window().menu().handleKey("2")) << "slot key 2 is taken";
    EXPECT_TRUE(window().workspace().documentOpen());
    EXPECT_EQ(&window().document(), window().canvas().document());
    EXPECT_EQ("A4", window().document().name());
    EXPECT_EQ(595.0f, window().document().pageWidth());
    EXPECT_EQ(842.0f, window().document().pageHeight());
    EXPECT_FALSE(window().menu().isOpen());
}

TEST_F(WorkspaceTest, TheDocumentRingLeadsBackToTheNewRingAndReplacesTheDocument) {
    window().newDocument("A4", 595.0f, 842.0f);
    window().openRing(false);
    ASSERT_TRUE(window().menu().isOpen());
    EXPECT_EQ("Document", window().menu().ring().title);
    window().runCommand("ring.new");
    ASSERT_TRUE(window().menu().isOpen());
    EXPECT_EQ("New", window().menu().ring().title);
    EXPECT_TRUE(window().menu().handleKey("3"));
    EXPECT_EQ("Full HD", window().document().name());
    EXPECT_EQ(1920.0f, window().document().pageWidth());
}
