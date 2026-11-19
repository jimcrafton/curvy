#include "curvy/guides.h"
#include "curvy/sketchdocument.h"

#include "testsupport.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using curvy::Guide;

namespace {

    std::string readFile(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

}

TEST(Guides, anAddedGuideIsKeptInOrderWithItsAxis) {
    curvy::SketchDocument document;
    document.newBlank("Guides", 612.0f, 792.0f);
    EXPECT_TRUE(document.guides().empty());
    EXPECT_EQ(0u, document.addGuide(true, 120.5));
    EXPECT_EQ(1u, document.addGuide(false, 40.0));
    ASSERT_EQ(2u, document.guides().size());
    EXPECT_EQ((Guide{true, 120.5}), document.guides()[0]);
    EXPECT_EQ((Guide{false, 40.0}), document.guides()[1]);
}

TEST(Guides, everyEditIsOneUndoStepAndRedoRepeatsIt) {
    curvy::SketchDocument document;
    document.newBlank("Guides", 612.0f, 792.0f);
    document.addGuide(true, 10.0);
    document.addGuide(false, 20.0);
    document.moveGuide(0, 15.0);
    EXPECT_EQ(15.0, document.guides()[0].position);
    document.undoStack().undo();
    EXPECT_EQ(10.0, document.guides()[0].position);
    document.undoStack().redo();
    EXPECT_EQ(15.0, document.guides()[0].position);

    document.removeGuide(0);
    ASSERT_EQ(1u, document.guides().size());
    EXPECT_EQ((Guide{false, 20.0}), document.guides()[0]);
    document.undoStack().undo();
    ASSERT_EQ(2u, document.guides().size());
    EXPECT_EQ((Guide{true, 15.0}), document.guides()[0]) << "undo puts it back at its place";

    EXPECT_TRUE(document.clearGuides());
    EXPECT_TRUE(document.guides().empty());
    EXPECT_FALSE(document.clearGuides()) << "nothing to clear is not a step";
    document.undoStack().undo();
    EXPECT_EQ(2u, document.guides().size());
}

TEST(Guides, aMoveToTheSamePlaceIsNotAStep) {
    curvy::SketchDocument document;
    document.newBlank("Guides", 612.0f, 792.0f);
    document.addGuide(true, 10.0);
    const std::string last = document.undoStack().undoDescription();
    document.moveGuide(0, 10.0);
    EXPECT_EQ(last, document.undoStack().undoDescription());
}

TEST(Guides, badArgumentsAreRefused) {
    curvy::SketchDocument document;
    document.newBlank("Guides", 612.0f, 792.0f);
    document.addGuide(true, 10.0);
    EXPECT_THROW(document.moveGuide(5, 1.0), std::runtime_error);
    EXPECT_THROW(document.removeGuide(5), std::runtime_error);
    EXPECT_THROW(document.addGuide(true, std::nan("")), std::runtime_error);
    EXPECT_THROW(document.moveGuide(0, HUGE_VAL), std::runtime_error);
    ASSERT_EQ(1u, document.guides().size());
    EXPECT_EQ(10.0, document.guides()[0].position);
}

TEST(Guides, lockedGuidesCannotBeMovedRemovedOrClearedButCanBeAdded) {
    curvy::SketchDocument document;
    document.newBlank("Guides", 612.0f, 792.0f);
    document.addGuide(true, 10.0);
    document.setGuidesLocked(true);
    EXPECT_THROW(document.moveGuide(0, 5.0), std::runtime_error);
    EXPECT_THROW(document.removeGuide(0), std::runtime_error);
    EXPECT_THROW(document.clearGuides(), std::runtime_error);
    EXPECT_NO_THROW(document.addGuide(false, 30.0));
    EXPECT_EQ(2u, document.guides().size());
    document.setGuidesLocked(false);
    EXPECT_NO_THROW(document.moveGuide(0, 5.0));
}

TEST(Guides, lockAndVisibilityAreNotEditsButTheDocumentTellsListeners) {
    curvy::SketchDocument document;
    document.newBlank("Guides", 612.0f, 792.0f);
    int calls = 0;
    document.onGuidesChanged += [&calls](curvy::SketchDocument&) {
        ++calls;
        return newui::SyncReturn::Handled;
    };
    const std::string last = document.undoStack().canUndo() ? document.undoStack().undoDescription() : "";
    document.setGuidesLocked(true);
    document.setGuidesVisible(false);
    document.setGuidesVisible(false);   // unchanged: silent
    EXPECT_EQ(2, calls);
    EXPECT_EQ(last, document.undoStack().canUndo() ? document.undoStack().undoDescription() : "");
    document.addGuide(true, 1.0);
    EXPECT_EQ(3, calls);
    document.undoStack().undo();
    EXPECT_EQ(4, calls);
}

TEST(Guides, aNewDocumentStartsWithoutGuides) {
    curvy::SketchDocument document;
    document.newBlank("Guides", 612.0f, 792.0f);
    document.addGuide(true, 10.0);
    document.setGuidesLocked(true);
    document.setGuidesVisible(false);
    document.newBlank("Fresh", 100.0f, 100.0f);
    EXPECT_TRUE(document.guides().empty());
    EXPECT_FALSE(document.guidesLocked());
    EXPECT_TRUE(document.guidesVisible());
}

TEST(GuidesFile, areSavedWithTheDocumentAndReadBack) {
    const std::filesystem::path path = curvytest::tempFile("curvy_test_guides.curvy");
    std::filesystem::remove(path);
    curvy::SketchDocument saved;
    saved.newBlank("Guides", 612.0f, 792.0f);
    saved.addGuide(true, 120.5);
    saved.addGuide(false, 40.0);
    saved.setGuidesLocked(true);
    saved.setGuidesVisible(false);
    ASSERT_TRUE(saved.save(path.u8string()));

    curvy::SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path.u8string())) << loaded.lastError();
    ASSERT_EQ(2u, loaded.guides().size());
    EXPECT_EQ((Guide{true, 120.5}), loaded.guides()[0]);
    EXPECT_EQ((Guide{false, 40.0}), loaded.guides()[1]);
    EXPECT_TRUE(loaded.guidesLocked());
    EXPECT_FALSE(loaded.guidesVisible());
    EXPECT_FALSE(loaded.isModified());
    EXPECT_FALSE(loaded.undoStack().canUndo());
    std::filesystem::remove(path);
}

TEST(GuidesFile, aDocumentWithoutGuidesWritesNoSection) {
    const std::filesystem::path path = curvytest::tempFile("curvy_test_noguides.curvy");
    std::filesystem::remove(path);
    curvy::SketchDocument saved;
    saved.newBlank("Plain", 100.0f, 100.0f);
    ASSERT_TRUE(saved.save(path.u8string()));
    EXPECT_EQ(std::string::npos, readFile(path).find("guides"));
    std::filesystem::remove(path);
}

TEST(GuidesFile, malformedEntriesAreDroppedAndTheFileStillOpens) {
    const std::filesystem::path path = curvytest::tempFile("curvy_test_badguides.curvy");
    std::filesystem::remove(path);
    curvy::SketchDocument saved;
    saved.newBlank("Bad", 100.0f, 100.0f);
    saved.addGuide(true, 11.0);
    ASSERT_TRUE(saved.save(path.u8string()));
    std::string text = readFile(path);
    const std::size_t section = text.find("guides");
    ASSERT_NE(std::string::npos, section) << text;
    const std::size_t at = text.find("list", section);
    ASSERT_NE(std::string::npos, at) << text;
    const std::size_t open = text.find('[', at);
    text.insert(open + 1, "[\"x\", 5], [\"v\"], [\"h\", \"far\"], 7, [\"h\", 22], ");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
    curvy::SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path.u8string())) << loaded.lastError();
    ASSERT_EQ(2u, loaded.guides().size());
    EXPECT_EQ((Guide{false, 22.0}), loaded.guides()[0]);
    EXPECT_EQ((Guide{true, 11.0}), loaded.guides()[1]);
    std::filesystem::remove(path);
}

TEST(Guides, theRulerOriginIsSetupSavedWithTheDocumentAndResetByNew) {
    curvy::SketchDocument document;
    document.newBlank("Origin", 612.0f, 792.0f);
    EXPECT_EQ((curvy::RulerOrigin{0.0, 0.0}), document.rulerOrigin());
    int changes = 0;
    document.onGuidesChanged += [&changes](curvy::SketchDocument&) {
        ++changes;
        return newui::SyncReturn::Ignored;
    };
    const std::size_t steps = document.undoStack().depth();
    document.setRulerOrigin(curvy::RulerOrigin{36.0, 72.5});
    EXPECT_EQ(steps, document.undoStack().depth()) << "setup, not an undo step";
    EXPECT_EQ(1, changes);
    document.setRulerOrigin(curvy::RulerOrigin{36.0, 72.5});
    EXPECT_EQ(1, changes) << "the same origin is no change";
    EXPECT_THROW(document.setRulerOrigin(curvy::RulerOrigin{std::nan(""), 0.0}), std::runtime_error);

    const std::filesystem::path path = curvytest::tempFile("curvy_test_origin.curvy");
    std::filesystem::remove(path);
    ASSERT_TRUE(document.save(path.u8string()));
    curvy::SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path.u8string())) << loaded.lastError();
    EXPECT_EQ((curvy::RulerOrigin{36.0, 72.5}), loaded.rulerOrigin());
    loaded.newBlank("Fresh", 100.0f, 100.0f);
    EXPECT_EQ((curvy::RulerOrigin{0.0, 0.0}), loaded.rulerOrigin());
    std::filesystem::remove(path);
}
