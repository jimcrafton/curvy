#include "curvy/swatchpalettecontroller.h"
#include "curvy/swatchstrip.h"

#include "testsupport.h"

#include <gtest/gtest.h>

using curvy::SketchDocument;
using curvy::StripMenuEntry;
using curvy::StripPalette;
using curvy::StripSwatch;
using curvy::Swatch;
using curvy::SwatchPaletteController;
using curvy::SwatchStrip;
using curvy::SwatchTiles;

namespace {
    std::vector<StripSwatch> swatches(std::size_t count) {
        std::vector<StripSwatch> list;
        for (std::size_t i = 0; i < count; ++i) {
            list.push_back(StripSwatch{float(i) / 20.0f, 0.5f, 0.25f, 1.0f, i == 1 ? "Second" : ""});
        }
        return list;
    }

    // Runs the menu line with this text.
    void choose(const std::vector<StripMenuEntry>& menu, const std::string& text) {
        for (const StripMenuEntry& entry : menu) {
            if (entry.text == text) {
                ASSERT_TRUE(entry.enabled) << text << " is disabled";
                entry.run();
                return;
            }
        }
        FAIL() << "no menu line \"" << text << "\"";
    }

    const StripMenuEntry* find(const std::vector<StripMenuEntry>& menu, const std::string& text) {
        for (const StripMenuEntry& entry : menu) {
            if (entry.text == text) {
                return &entry;
            }
        }
        return nullptr;
    }
}

TEST(SwatchTilesTest, tilesWrapAtTheWidthAndTheAddTileComesLast) {
    SwatchTiles tiles;
    tiles.setBounds(newui::Rect(0.0f, 0.0f, 214.0f, 60.0f));
    tiles.setSwatches(swatches(13));
    EXPECT_EQ(tiles.columnsFor(214.0f), 10u);   // 18 + 3 per tile
    EXPECT_EQ(tiles.tileCount(), 14u);          // 13 swatches and the "+" tile
    EXPECT_FLOAT_EQ(tiles.heightFor(214.0f), 39.0f);   // two rows

    EXPECT_EQ(tiles.tileAt(tiles.tileCenter(0)), 0);
    EXPECT_EQ(tiles.tileAt(tiles.tileCenter(10)), 10);   // first of the second row
    EXPECT_EQ(tiles.tileAt(tiles.tileCenter(13)), 13);   // the "+" tile
    EXPECT_EQ(tiles.tileAt(newui::Point(150.0f, 30.0f)), SwatchTiles::kNone);   // empty space after it
    EXPECT_EQ(tiles.tileAt(newui::Point(-5.0f, 5.0f)), SwatchTiles::kNone);
}

TEST(SwatchTilesTest, clickingUsesASwatchThePlusTileAddsAndARightClickAsksForTheMenu) {
    SwatchTiles tiles;
    tiles.setBounds(newui::Rect(0.0f, 0.0f, 214.0f, 60.0f));
    tiles.setSwatches(swatches(4));
    int used = -1, added = 0, menu = -1;
    tiles.onUse = [&](std::size_t index) { used = int(index); };
    tiles.onAdd = [&]() { ++added; };
    tiles.onMenu = [&](std::size_t index) { menu = int(index); };

    const newui::Point two = tiles.tileCenter(2), plus = tiles.tileCenter(4);
    tiles.pressTile(tiles.tileAt(two), false, two);
    tiles.releaseAt(two);
    EXPECT_EQ(used, 2);

    tiles.pressTile(tiles.tileAt(plus), false, plus);
    tiles.releaseAt(plus);
    EXPECT_EQ(added, 1);

    tiles.pressTile(tiles.tileAt(two), true, two);
    EXPECT_EQ(menu, 2);
    tiles.pressTile(tiles.tileAt(plus), true, plus);   // the "+" tile has no menu
    EXPECT_EQ(menu, 2);

    used = -1;   // pressing on one tile and letting go on another is not a click
    tiles.pressTile(tiles.tileAt(two), false, two);
    tiles.releaseAt(tiles.tileCenter(3));
    EXPECT_EQ(used, -1);

    tiles.setEditable(false);   // an automatic palette: the "+" does nothing
    tiles.pressTile(tiles.tileAt(plus), false, plus);
    tiles.releaseAt(plus);
    EXPECT_EQ(added, 1);
}

TEST(SwatchTilesTest, draggingASwatchOntoAnotherMovesItButNotInAnAutomaticPalette) {
    SwatchTiles tiles;
    tiles.setBounds(newui::Rect(0.0f, 0.0f, 214.0f, 60.0f));
    tiles.setSwatches(swatches(5));
    int from = -1, to = -1, used = 0;
    tiles.onMove = [&](std::size_t a, std::size_t b) { from = int(a); to = int(b); };
    tiles.onUse = [&](std::size_t) { ++used; };

    const newui::Point start = tiles.tileCenter(1);
    tiles.pressTile(tiles.tileAt(start), false, start);
    tiles.moveTo(newui::Point(start.x + 2.0f, start.y), true);   // a wobble is not a drag
    tiles.moveTo(tiles.tileCenter(3), true);
    tiles.releaseAt(tiles.tileCenter(3));
    EXPECT_EQ(from, 1);
    EXPECT_EQ(to, 3);
    EXPECT_EQ(used, 0);   // a drag is not also a click

    from = to = -1;
    tiles.pressTile(tiles.tileAt(start), false, start);
    tiles.moveTo(tiles.tileCenter(5), true);   // over the "+" tile: the end
    tiles.releaseAt(tiles.tileCenter(5));
    EXPECT_EQ(from, 1);
    EXPECT_EQ(to, 4);

    from = -1;
    tiles.setEditable(false);
    tiles.pressTile(tiles.tileAt(start), false, start);
    tiles.moveTo(tiles.tileCenter(3), true);
    tiles.releaseAt(tiles.tileCenter(3));
    EXPECT_EQ(from, -1);
}

TEST(SwatchTilesTest, theHoverReportsTheSwatchUnderThePointer) {
    SwatchTiles tiles;
    tiles.setBounds(newui::Rect(0.0f, 0.0f, 214.0f, 60.0f));
    tiles.setSwatches(swatches(3));
    int hovered = -2;
    tiles.onHover = [&](int index) { hovered = index; };
    tiles.moveTo(tiles.tileCenter(1), false);
    EXPECT_EQ(hovered, 1);
    tiles.moveTo(tiles.tileCenter(3), false);   // the "+" tile is not a swatch
    EXPECT_EQ(hovered, SwatchTiles::kNone);
}

TEST(SwatchStripTest, theMenusFollowWhetherThePaletteIsAutomatic) {
    SwatchStrip strip;
    strip.setPalettes({{"Recent", true}, {"Sunset", false}, {"Greys", false}}, 1);
    strip.setSwatches(swatches(3));

    const auto list = strip.paletteListMenu();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0].text, "Recent (automatic)");
    EXPECT_TRUE(list[1].checked);
    EXPECT_FALSE(list[0].checked);

    const auto more = strip.paletteMoreMenu();
    EXPECT_TRUE(find(more, "New palette")->enabled);
    EXPECT_TRUE(find(more, "Duplicate")->enabled);
    EXPECT_TRUE(find(more, "Delete...")->enabled);   // two real palettes

    const auto swatchLines = strip.swatchMenu(0);
    EXPECT_NE(find(swatchLines, "Replace with current colour"), nullptr);
    EXPECT_NE(find(swatchLines, "Rename..."), nullptr);
    EXPECT_NE(find(swatchLines, "Delete"), nullptr);
    EXPECT_EQ(find(swatchLines, "Save to palette"), nullptr);

    strip.setPalettes({{"Recent", true}, {"Sunset", false}, {"Greys", false}}, 0);   // Recent is active
    const auto recentMore = strip.paletteMoreMenu();
    EXPECT_TRUE(find(recentMore, "New palette")->enabled);
    EXPECT_FALSE(find(recentMore, "Duplicate")->enabled);
    EXPECT_FALSE(find(recentMore, "Rename...")->enabled);
    EXPECT_FALSE(find(recentMore, "Delete...")->enabled);
    const auto recentSwatch = strip.swatchMenu(0);
    EXPECT_NE(find(recentSwatch, "Save to palette"), nullptr);
    EXPECT_EQ(find(recentSwatch, "Delete"), nullptr);
    EXPECT_EQ(find(recentSwatch, "Replace with current colour"), nullptr);

    strip.setPalettes({{"Recent", true}, {"Only", false}}, 1);   // the last real palette cannot be deleted
    EXPECT_FALSE(find(strip.paletteMoreMenu(), "Delete...")->enabled);
}

TEST(SwatchStripTest, theInlineBarAsksToConfirmOrToTypeAndGrowsTheStrip) {
    SwatchStrip strip;
    strip.setPalettes({{"Recent", true}, {"Sunset", false}}, 1);
    strip.setSwatches(swatches(3));
    int heightChanges = 0;
    strip.onHeightChanged = [&]() { ++heightChanges; };
    const float closed = strip.layoutFor(214.0f);

    int yes = 0;
    strip.askConfirm("Delete it?", [&]() { ++yes; });
    EXPECT_TRUE(strip.asking());
    EXPECT_EQ(strip.question(), "Delete it?");
    EXPECT_GT(strip.layoutFor(214.0f), closed);
    EXPECT_GE(heightChanges, 1);
    strip.answer(false);
    EXPECT_FALSE(strip.asking());
    EXPECT_EQ(yes, 0);
    EXPECT_FLOAT_EQ(strip.layoutFor(214.0f), closed);

    strip.askConfirm("Delete it?", [&]() { ++yes; });
    strip.answer(true);
    EXPECT_EQ(yes, 1);

    std::string typed;
    strip.askText("Name?", "old", [&](const std::string& text) { typed = text; });
    strip.answer(true, "new name");
    EXPECT_EQ(typed, "new name");
    strip.answer(true, "ignored");   // nothing is being asked now
    EXPECT_EQ(typed, "new name");
}

TEST(SwatchPaletteControllerTest, theStripShowsRecentThenTheDocumentsPalettes) {
    SketchDocument document;
    SwatchStrip strip;
    SwatchPaletteController controller(document, strip);
    ASSERT_EQ(strip.palettes().size(), 2u);
    EXPECT_EQ(strip.palettes()[0].name, "Recent");
    EXPECT_TRUE(strip.palettes()[0].automatic);
    EXPECT_EQ(strip.palettes()[1].name, "Swatches");
    EXPECT_EQ(strip.activePalette(), 1u);   // the document's active palette, not Recent
    EXPECT_EQ(strip.tiles().swatches().size(), 13u);

    strip.onSelectPalette(0);
    EXPECT_TRUE(controller.showingRecent());
    EXPECT_EQ(strip.tiles().swatches().size(), 0u);
    document.noteRecentColor(Swatch{1.0f, 0.0f, 0.0f, 1.0f, ""});
    EXPECT_EQ(strip.tiles().swatches().size(), 1u);   // Recent follows what the document notes

    strip.onAdd();   // nothing saves into Recent
    EXPECT_EQ(document.palettes()[0].swatches.size(), 13u);

    strip.onSelectPalette(1);
    EXPECT_FALSE(controller.showingRecent());
    EXPECT_EQ(strip.tiles().swatches().size(), 13u);
}

TEST(SwatchPaletteControllerTest, theStripsRequestsAreDocumentEditsThatUndo) {
    SketchDocument document;
    SwatchStrip strip;
    SwatchPaletteController controller(document, strip);
    std::string message;
    controller.onMessage = [&](const std::string& text) { message = text; };
    controller.setCurrent(Swatch{0.1f, 0.2f, 0.3f, 1.0f, ""});

    strip.onAdd();
    ASSERT_EQ(document.palettes()[0].swatches.size(), 14u);
    EXPECT_FLOAT_EQ(document.palettes()[0].swatches[13].g, 0.2f);
    EXPECT_EQ(strip.tiles().swatches().size(), 14u);
    document.undoStack().undo();
    EXPECT_EQ(strip.tiles().swatches().size(), 13u);   // undo shows in the strip at once

    controller.setCurrent(Swatch{0.9f, 0.8f, 0.7f, 1.0f, ""});
    strip.onReplace(0);
    EXPECT_FLOAT_EQ(document.palettes()[0].swatches[0].r, 0.9f);
    EXPECT_EQ(document.palettes()[0].swatches[0].name, "Black");   // the name stays

    strip.onMove(0, 2);
    EXPECT_EQ(document.palettes()[0].swatches[2].name, "Black");
    EXPECT_EQ(strip.tiles().swatches()[2].name, "Black");

    choose(strip.swatchMenu(1), "Rename...");   // opens the bar; the answer renames
    ASSERT_TRUE(strip.asking());
    strip.answer(true, "Renamed");
    EXPECT_EQ(document.palettes()[0].swatches[1].name, "Renamed");

    choose(strip.swatchMenu(1), "Delete");
    EXPECT_EQ(document.palettes()[0].swatches.size(), 12u);

    Swatch used;
    controller.onUse = [&](const Swatch& swatch) { used = swatch; };
    strip.onUse(0);
    EXPECT_EQ(used, document.palettes()[0].swatches[0]);
    EXPECT_TRUE(message.empty());
}

TEST(SwatchPaletteControllerTest, palettesAreMadeRenamedDuplicatedAndDeletedFromTheMenu) {
    SketchDocument document;
    SwatchStrip strip;
    SwatchPaletteController controller(document, strip);
    std::string message;
    controller.onMessage = [&](const std::string& text) { message = text; };

    choose(strip.paletteMoreMenu(), "New palette");
    ASSERT_EQ(document.palettes().size(), 2u);
    EXPECT_EQ(document.activePalette(), 1u);   // the new one is shown
    EXPECT_EQ(strip.tiles().swatches().size(), 0u);
    EXPECT_EQ(strip.activePalette(), 2u);      // after Recent and "Swatches"

    choose(strip.paletteMoreMenu(), "Rename...");
    strip.answer(true, "Mine");
    EXPECT_EQ(document.palettes()[1].name, "Mine");
    EXPECT_EQ(strip.paletteButtonText(), "Mine");
    choose(strip.paletteMoreMenu(), "Rename...");
    strip.answer(true, "Swatches");   // taken: refused, nothing changes
    EXPECT_EQ(document.palettes()[1].name, "Mine");
    EXPECT_NE(message.find("already a palette"), std::string::npos) << message;

    choose(strip.paletteMoreMenu(), "Duplicate");
    EXPECT_EQ(document.palettes()[2].name, "Mine copy");
    EXPECT_EQ(document.activePalette(), 2u);

    choose(strip.paletteMoreMenu(), "Delete...");   // asks first
    ASSERT_TRUE(strip.asking());
    EXPECT_NE(strip.question().find("Mine copy"), std::string::npos);
    EXPECT_EQ(document.palettes().size(), 3u);
    strip.answer(false);
    EXPECT_EQ(document.palettes().size(), 3u);
    choose(strip.paletteMoreMenu(), "Delete...");
    strip.answer(true);
    EXPECT_EQ(document.palettes().size(), 2u);
    document.undoStack().undo();   // deleting a palette undoes
    EXPECT_EQ(document.palettes().size(), 3u);
    EXPECT_EQ(strip.palettes().size(), 4u);
}

TEST(SwatchPaletteControllerTest, aRecentColourCanBeSavedIntoTheActivePalette) {
    SketchDocument document;
    SwatchStrip strip;
    SwatchPaletteController controller(document, strip);
    std::string message;
    controller.onMessage = [&](const std::string& text) { message = text; };
    document.noteRecentColor(Swatch{0.4f, 0.5f, 0.6f, 1.0f, ""});
    strip.onSelectPalette(0);
    choose(strip.swatchMenu(0), "Save to palette");
    ASSERT_EQ(document.palettes()[0].swatches.size(), 14u);
    EXPECT_FLOAT_EQ(document.palettes()[0].swatches[13].b, 0.6f);
    EXPECT_NE(message.find("Saved to the palette Swatches"), std::string::npos) << message;
    EXPECT_TRUE(controller.showingRecent());   // still looking at Recent
}
