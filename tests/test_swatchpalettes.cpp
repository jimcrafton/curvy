#include "curvy/sketchdocument.h"

#include "curvy/objectnode.h"
#include "testsupport.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using curvy::Palette;
using curvy::SketchDocument;
using curvy::Swatch;

namespace {
    Swatch swatch(float r, float g, float b, const char* name = "") {
        return Swatch{r, g, b, 1.0f, name};
    }

    std::vector<std::string> paletteNames(const SketchDocument& document) {
        std::vector<std::string> names;
        for (const Palette& palette : document.palettes()) {
            names.push_back(palette.name);
        }
        return names;
    }
}

TEST(Palettes, aNewDocumentHasOneDefaultPaletteWithThirteenSwatches) {
    SketchDocument document;
    ASSERT_EQ(document.palettes().size(), 1u);
    EXPECT_EQ(document.activePalette(), 0u);
    EXPECT_EQ(document.palettes()[0].name, "Swatches");
    EXPECT_EQ(document.palettes()[0].swatches.size(), 13u);
    EXPECT_EQ(document.palettes()[0].swatches[0].name, "Black");
}

TEST(Palettes, addingDuplicatingRenamingAndDeletingPalettesEachUndoes) {
    SketchDocument document;
    const std::size_t sunset = document.addPalette("Sunset");
    EXPECT_EQ(sunset, 1u);
    EXPECT_EQ(document.addPalette("Sunset"), 2u);
    EXPECT_EQ(paletteNames(document), (std::vector<std::string>{"Swatches", "Sunset", "Sunset 2"}));   // names stay unique
    EXPECT_THROW(document.addPalette(""), std::runtime_error);

    EXPECT_EQ(document.duplicatePalette(0), 1u);   // the copy sits right after the original
    EXPECT_EQ(paletteNames(document)[1], "Swatches copy");
    EXPECT_EQ(document.palettes()[1].swatches.size(), 13u);

    document.renamePalette(1, "Mine");
    EXPECT_EQ(document.palettes()[1].name, "Mine");
    EXPECT_THROW(document.renamePalette(1, "Sunset"), std::runtime_error);
    EXPECT_THROW(document.renamePalette(1, ""), std::runtime_error);
    EXPECT_THROW(document.renamePalette(9, "x"), std::runtime_error);

    document.deletePalette(1);
    EXPECT_EQ(paletteNames(document), (std::vector<std::string>{"Swatches", "Sunset", "Sunset 2"}));
    document.undoStack().undo();   // delete
    EXPECT_EQ(document.palettes()[1].name, "Mine");
    document.undoStack().undo();   // rename
    EXPECT_EQ(document.palettes()[1].name, "Swatches copy");
    document.undoStack().undo();   // duplicate
    EXPECT_EQ(document.palettes().size(), 3u);
    document.undoStack().redo();
    EXPECT_EQ(document.palettes().size(), 4u);
}

TEST(Palettes, theLastPaletteCannotBeDeletedAndTheActiveOneFollowsDeletions) {
    SketchDocument document;
    EXPECT_THROW(document.deletePalette(0), std::runtime_error);
    document.addPalette("B");
    document.addPalette("C");
    document.setActivePalette(2);
    document.deletePalette(0);   // the active one moves down with its palette
    EXPECT_EQ(document.activePalette(), 1u);
    EXPECT_EQ(document.palettes()[document.activePalette()].name, "C");
    document.deletePalette(1);   // the active one itself: the next (here the previous) takes over
    EXPECT_EQ(document.activePalette(), 0u);
    document.undoStack().undo();
    EXPECT_LT(document.activePalette(), document.palettes().size());
    EXPECT_THROW(document.setActivePalette(5), std::runtime_error);
}

TEST(Palettes, choosingTheActivePaletteIsNotAnUndoStepOrAChange) {
    SketchDocument document;
    document.addPalette("B");
    document.undoStack().clear();
    document.setActivePalette(1);
    EXPECT_EQ(document.activePalette(), 1u);
    EXPECT_FALSE(document.undoStack().canUndo());
}

TEST(Palettes, swatchEditsEachUndoAndKeepTheOrderTheyMake) {
    SketchDocument document;
    document.addPalette("Work");
    document.deletePalette(0);   // leave only "Work"
    ASSERT_EQ(document.palettes().size(), 1u);
    EXPECT_EQ(document.addSwatch(0, swatch(1, 0, 0, "Red")), 0u);
    document.addSwatch(0, swatch(0, 1, 0));
    document.addSwatch(0, swatch(0, 0, 1, "Blue"));

    document.replaceSwatch(0, 0, swatch(0.5f, 0.5f, 0.5f));   // no name given: the old one stays
    EXPECT_EQ(document.palettes()[0].swatches[0].name, "Red");
    EXPECT_FLOAT_EQ(document.palettes()[0].swatches[0].g, 0.5f);

    document.renameSwatch(0, 1, "Green");
    EXPECT_EQ(document.palettes()[0].swatches[1].name, "Green");

    document.moveSwatch(0, 2, 0);   // Blue to the front
    EXPECT_EQ(document.palettes()[0].swatches[0].name, "Blue");
    EXPECT_EQ(document.palettes()[0].swatches[2].name, "Green");
    document.undoStack().undo();
    EXPECT_EQ(document.palettes()[0].swatches[0].name, "Red");

    document.deleteSwatch(0, 0);
    EXPECT_EQ(document.palettes()[0].swatches.size(), 2u);
    document.undoStack().undo();
    EXPECT_EQ(document.palettes()[0].swatches.size(), 3u);

    EXPECT_THROW(document.deleteSwatch(0, 7), std::runtime_error);
    EXPECT_THROW(document.addSwatch(4, swatch(0, 0, 0)), std::runtime_error);
    EXPECT_TRUE(document.isModified());
}

TEST(Palettes, recentColoursAreNewestFirstWithoutRepeatsAndNotAChange) {
    SketchDocument document;
    document.undoStack().clear();
    document.noteRecentColor(swatch(1, 0, 0, "ignored name"));
    document.noteRecentColor(swatch(0, 1, 0));
    document.noteRecentColor(swatch(1, 0, 0));
    ASSERT_EQ(document.recentColors().size(), 2u);
    EXPECT_FLOAT_EQ(document.recentColors()[0].r, 1.0f);   // red moved back to the front
    EXPECT_TRUE(document.recentColors()[0].name.empty());
    for (int i = 0; i < 40; ++i) {
        document.noteRecentColor(swatch(float(i) / 100.0f, 0.5f, 0.5f));
    }
    EXPECT_EQ(document.recentColors().size(), SketchDocument::kRecentLimit);
    EXPECT_FALSE(document.undoStack().canUndo());
    EXPECT_FALSE(document.isModified());
    document.newBlank("Fresh", 100.0f, 100.0f);
    EXPECT_TRUE(document.recentColors().empty());
}

namespace {
    struct LinkedFixture {
        LinkedFixture() {
            const curvy::NodeRef layer = document.addLayer();
            fill = document.addCircle(layer, 0.0f, 0.0f, 5.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
            index = 5;   // "Red"
        }
        void tie() { document.node(fill).addTag(curvy::SwatchLinks::tag(document.palettes()[0].swatches[index].link, "color")); }
        float red() { return document.node(fill).getFloat("r"); }
        bool tied() { return std::any_of(document.node(fill).tags().begin(), document.node(fill).tags().end(), [](const std::string& tag) { return tag.rfind("swatch:", 0) == 0; }); }

        SketchDocument document;
        curvy::NodeRef fill;
        std::size_t index = 0;
    };
}

TEST(LinkedSwatches, linkingGivesANumberAndUndoes) {
    LinkedFixture f;
    EXPECT_EQ(f.document.palettes()[0].swatches[f.index].link, 0u);
    f.document.setSwatchLinked(0, f.index, true);
    EXPECT_NE(f.document.palettes()[0].swatches[f.index].link, 0u);
    EXPECT_EQ(f.document.undoStack().undoDescription(), "Link Swatch");
    f.document.setSwatchLinked(0, 6, true);
    EXPECT_NE(f.document.palettes()[0].swatches[6].link, f.document.palettes()[0].swatches[f.index].link);
    f.document.undoStack().undo();
    f.document.undoStack().undo();
    EXPECT_EQ(f.document.palettes()[0].swatches[f.index].link, 0u);
}

TEST(LinkedSwatches, replacingALinkedSwatchRecoloursItsRowsInOneUndoStep) {
    LinkedFixture f;
    f.document.setSwatchLinked(0, f.index, true);
    f.tie();
    const float before = f.red();
    f.document.replaceSwatch(0, f.index, Swatch{0.1f, 0.2f, 0.3f, 1.0f, ""});
    EXPECT_FLOAT_EQ(f.red(), 0.1f);
    EXPECT_FLOAT_EQ(f.document.node(f.fill).getFloat("g"), 0.2f);
    EXPECT_FLOAT_EQ(f.document.node(f.fill).getFloat("b"), 0.3f);
    EXPECT_NE(f.document.palettes()[0].swatches[f.index].link, 0u);   // the link and the name stay
    EXPECT_EQ(f.document.palettes()[0].swatches[f.index].name, "Red");

    f.document.undoStack().undo();
    EXPECT_FLOAT_EQ(f.red(), before);
    EXPECT_FLOAT_EQ(f.document.palettes()[0].swatches[f.index].r, 0.90f);
    f.document.undoStack().redo();
    EXPECT_FLOAT_EQ(f.red(), 0.1f);
    EXPECT_FLOAT_EQ(f.document.palettes()[0].swatches[f.index].r, 0.1f);
}

TEST(LinkedSwatches, aPlainSwatchOrAnUntiedRowIsLeftAlone) {
    LinkedFixture f;
    const float before = f.red();
    f.document.replaceSwatch(0, f.index, Swatch{0.1f, 0.2f, 0.3f, 1.0f, ""});   // plain: nothing follows
    EXPECT_FLOAT_EQ(f.red(), before);
    f.document.setSwatchLinked(0, f.index, true);
    f.document.replaceSwatch(0, f.index, Swatch{0.4f, 0.4f, 0.4f, 1.0f, ""});   // linked, but no row is tied to it
    EXPECT_FLOAT_EQ(f.red(), before);
}

TEST(LinkedSwatches, unlinkingAndDeletingKeepTheRowsColoursAndDropTheTie) {
    LinkedFixture f;
    f.document.setSwatchLinked(0, f.index, true);
    f.tie();
    f.document.replaceSwatch(0, f.index, Swatch{0.1f, 0.2f, 0.3f, 1.0f, ""});
    f.document.setSwatchLinked(0, f.index, false);
    EXPECT_FALSE(f.tied());
    EXPECT_FLOAT_EQ(f.red(), 0.1f);
    f.document.undoStack().undo();
    EXPECT_TRUE(f.tied());

    f.document.deleteSwatch(0, f.index);
    EXPECT_FALSE(f.tied());
    EXPECT_FLOAT_EQ(f.red(), 0.1f);
    f.document.undoStack().undo();
    EXPECT_TRUE(f.tied());
    EXPECT_EQ(f.document.palettes()[0].swatches.size(), 13u);
}

TEST(LinkedSwatches, copiesAndSavedRecentColoursArePlain) {
    SketchDocument document;
    document.setSwatchLinked(0, 5, true);
    EXPECT_EQ(document.addSwatch(0, document.palettes()[0].swatches[5]), 13u);
    EXPECT_EQ(document.palettes()[0].swatches[13].link, 0u);
    const std::size_t copy = document.duplicatePalette(0);
    for (const Swatch& swatch : document.palettes()[copy].swatches) {
        EXPECT_EQ(swatch.link, 0u);
    }
    Swatch linked = document.palettes()[0].swatches[5];
    document.noteRecentColor(linked);
    EXPECT_EQ(document.recentColors()[0].link, 0u);
}

namespace {
    class PaletteFile : public ::testing::Test {
    protected:
        void SetUp() override {
            path_ = curvytest::tempFile("curvy_test_palettes.curvy");
            std::filesystem::remove(path_);
        }
        void TearDown() override { std::filesystem::remove(path_); }

        std::string text() const {
            std::ifstream in(path_, std::ios::binary);
            std::ostringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }
        void write(const std::string& content) const {
            std::ofstream out(path_, std::ios::binary | std::ios::trunc);
            out << content;
        }

        std::filesystem::path path_;
    };
}

TEST_F(PaletteFile, palettesAndTheActiveOneSurviveASaveAndLoad) {
    SketchDocument saved;
    saved.addPalette("Alpha");
    saved.addSwatch(1, Swatch{0.25f, 0.5f, 0.75f, 0.5f, "Slate"});
    saved.addSwatch(1, swatch(1, 1, 0));
    saved.setActivePalette(1);
    ASSERT_TRUE(saved.save(path_.u8string()));

    SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path_.u8string())) << loaded.lastError();
    ASSERT_EQ(loaded.palettes().size(), 2u);
    EXPECT_EQ(loaded.activePalette(), 1u);
    EXPECT_EQ(loaded.palettes()[0], saved.palettes()[0]);
    EXPECT_EQ(loaded.palettes()[1], saved.palettes()[1]);
    EXPECT_FLOAT_EQ(loaded.palettes()[1].swatches[0].a, 0.5f);
    EXPECT_EQ(loaded.palettes()[1].swatches[0].name, "Slate");
    EXPECT_TRUE(loaded.palettes()[1].swatches[1].name.empty());
    EXPECT_FALSE(loaded.undoStack().canUndo());
}

TEST_F(PaletteFile, theLinkOfASwatchSurvivesASaveAndLoad) {
    SketchDocument saved;
    saved.setSwatchLinked(0, 5, true);
    ASSERT_TRUE(saved.save(path_.u8string()));
    SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path_.u8string())) << loaded.lastError();
    EXPECT_EQ(loaded.palettes()[0].swatches[5].link, saved.palettes()[0].swatches[5].link);
    EXPECT_NE(loaded.palettes()[0].swatches[5].link, 0u);
    EXPECT_EQ(loaded.palettes()[0].swatches[4].link, 0u);
}

TEST_F(PaletteFile, aFileFromBeforeSwatchesGetsTheDefaultPalette) {
    SketchDocument saved;
    ASSERT_TRUE(saved.save(path_.u8string()));
    std::string content = text();
    const std::size_t at = content.find("palettes");
    ASSERT_NE(at, std::string::npos);
    content.replace(at, 8, "xalettes");   // the reader ignores a key it does not know
    write(content);

    SketchDocument loaded;
    loaded.addPalette("Leftover");
    ASSERT_TRUE(loaded.load(path_.u8string())) << loaded.lastError();
    ASSERT_EQ(loaded.palettes().size(), 1u);
    EXPECT_EQ(loaded.palettes()[0].name, "Swatches");
}

TEST_F(PaletteFile, aFileWithTwoPalettesOfOneNameIsRefusedAndTheDocumentIsUntouched) {
    SketchDocument saved;
    saved.addPalette("Alpha");
    saved.addPalette("Beta");
    ASSERT_TRUE(saved.save(path_.u8string()));
    std::string content = text();
    const std::size_t at = content.find("Beta");
    ASSERT_NE(at, std::string::npos);
    content.replace(at, 4, "Alpha");
    write(content);

    SketchDocument loaded;
    loaded.addPalette("Mine");
    EXPECT_FALSE(loaded.load(path_.u8string()));
    EXPECT_NE(loaded.lastError().find("used twice"), std::string::npos) << loaded.lastError();
    EXPECT_EQ(paletteNames(loaded), (std::vector<std::string>{"Swatches", "Mine"}));
}
