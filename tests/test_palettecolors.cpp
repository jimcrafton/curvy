#include "curvy/sketchdocument.h"

#include "curvy/objectnode.h"
#include "testsupport.h"

#include <algorithm>
#include <stdexcept>

using curvy::ColorUse;
using curvy::NodeRef;
using curvy::SketchDocument;
using curvy::Swatch;

namespace {

    const newui::Color teal(0.1f, 0.6f, 0.5f, 1.0f);
    const newui::Color purple(0.4f, 0.1f, 0.7f, 1.0f);
    const newui::Color paletteRed(0.90f, 0.20f, 0.20f, 1.0f);   // the "Red" swatch of the default palette

    const ColorUse* find(const std::vector<ColorUse>& uses, const newui::Color& color) {
        for (const ColorUse& use : uses) {
            if (std::abs(use.color.r - color.r) < 0.003f && std::abs(use.color.g - color.g) < 0.003f && std::abs(use.color.b - color.b) < 0.003f) {
                return &use;
            }
        }
        return nullptr;
    }

    // Layer 1 holds two teal circles and a group with a third; layer 2 holds one purple circle.
    struct Colored {
        Colored() {
            layer1 = document.addLayer();
            layer2 = document.addLayer();
            group = document.addGroup(layer1);
            tealA = document.addCircle(layer1, 0.0f, 0.0f, 5.0f, teal);
            tealB = document.addCircle(layer1, 10.0f, 0.0f, 5.0f, teal);
            tealC = document.addCircle(group, 20.0f, 0.0f, 5.0f, teal);
            purpleA = document.addCircle(layer2, 0.0f, 20.0f, 5.0f, purple);
        }

        SketchDocument document;
        NodeRef layer1, layer2, group, tealA, tealB, tealC, purpleA;
    };

}

TEST(PaletteColors, colorsInUseCountsRowsAndGroupsMostUsedFirst) {
    Colored f;
    const std::vector<ColorUse> uses = f.document.colorsInUse();
    const ColorUse* t = find(uses, teal);
    const ColorUse* p = find(uses, purple);
    ASSERT_NE(t, nullptr);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(t->rows, 3u);
    EXPECT_EQ(t->groups, 2u) << "two in the layer, one in the group";
    EXPECT_EQ(p->rows, 1u);
    EXPECT_EQ(p->groups, 1u);
    EXPECT_LT(t, p) << "most used first";
    for (std::size_t i = 1; i < uses.size(); ++i) {
        EXPECT_GE(uses[i - 1].rows, uses[i].rows);
    }
}

TEST(PaletteColors, colorsInNoPaletteLeavesOutTheOnesASwatchHas) {
    Colored f;
    f.document.addCircle(f.layer2, 30.0f, 0.0f, 5.0f, paletteRed);
    EXPECT_NE(find(f.document.colorsInUse(false), paletteRed), nullptr);
    EXPECT_EQ(find(f.document.colorsInUse(true), paletteRed), nullptr);
    EXPECT_NE(find(f.document.colorsInUse(true), teal), nullptr);

    f.document.addSwatch(0, Swatch{teal.r, teal.g, teal.b, 1.0f, "Teal"});
    EXPECT_EQ(find(f.document.colorsInUse(true), teal), nullptr) << "now a swatch has it";
    EXPECT_NE(find(f.document.colorsInUse(true), purple), nullptr);
}

TEST(PaletteColors, readingTheColoursChangesNothing) {
    Colored f;
    const std::string last = f.document.undoStack().undoDescription();
    f.document.colorsInUse();
    f.document.colorsInUse(true);
    f.document.swatchUse(0, 0);
    EXPECT_EQ(f.document.undoStack().undoDescription(), last);
    EXPECT_FALSE(f.document.undoStack().canRedo());
}

TEST(PaletteColors, aPlainSwatchIsUsedByTheRowsWithItsColour) {
    Colored f;
    const std::size_t index = f.document.addSwatch(0, Swatch{teal.r, teal.g, teal.b, 1.0f, "Teal"});
    std::vector<NodeRef> users;
    const ColorUse use = f.document.swatchUse(0, index, &users);
    EXPECT_EQ(use.rows, 3u);
    EXPECT_EQ(use.groups, 2u);
    ASSERT_EQ(users.size(), 3u);
    EXPECT_NE(std::find(users.begin(), users.end(), f.tealA), users.end());
    EXPECT_NE(std::find(users.begin(), users.end(), f.tealC), users.end());
    EXPECT_EQ(std::find(users.begin(), users.end(), f.purpleA), users.end());

    const std::size_t unused = f.document.addSwatch(0, Swatch{0.0f, 1.0f, 0.0f, 1.0f, ""});
    EXPECT_EQ(f.document.swatchUse(0, unused).rows, 0u);
}

TEST(PaletteColors, aLinkedSwatchIsUsedByTheRowsTiedToIt) {
    Colored f;
    const std::size_t index = f.document.addSwatch(0, Swatch{teal.r, teal.g, teal.b, 1.0f, "Teal"});
    f.document.setSwatchLinked(0, index, true);
    EXPECT_EQ(f.document.swatchUse(0, index).rows, 0u) << "linked, but nothing is tied to it yet";

    f.document.node(f.tealB).addTag(curvy::SwatchLinks::tag(f.document.palettes()[0].swatches[index].link, "color"));
    std::vector<NodeRef> users;
    const ColorUse use = f.document.swatchUse(0, index, &users);
    EXPECT_EQ(use.rows, 1u);
    ASSERT_EQ(users.size(), 1u);
    EXPECT_EQ(users[0], f.tealB);
}

TEST(PaletteColors, aPaletteFromColoursIsOneUndoStepAndBecomesActive) {
    SketchDocument document;
    const std::vector<Swatch> colours = {Swatch{0.9f, 0.3f, 0.2f, 1.0f, "", 7}, Swatch{0.1f, 0.2f, 0.4f, 1.0f, "Named"}};
    const std::size_t index = document.addPaletteFrom("Harbour", colours);
    EXPECT_EQ(index, 1u);
    EXPECT_EQ(document.activePalette(), 1u);
    EXPECT_EQ(document.palettes()[1].name, "Harbour");
    ASSERT_EQ(document.palettes()[1].swatches.size(), 2u);
    EXPECT_EQ(document.palettes()[1].swatches[0].link, 0u) << "plain swatches";
    EXPECT_EQ(document.palettes()[1].swatches[1].name, "Named");
    EXPECT_EQ(document.undoStack().undoDescription(), "Add Palette From Image");

    EXPECT_EQ(document.addPaletteFrom("Harbour", colours), 2u);
    EXPECT_EQ(document.palettes()[2].name, "Harbour 2") << "the name is made unique";

    document.undoStack().undo();
    document.undoStack().undo();
    EXPECT_EQ(document.palettes().size(), 1u);
    EXPECT_EQ(document.activePalette(), 0u);
    document.undoStack().redo();
    EXPECT_EQ(document.palettes().size(), 2u);
    EXPECT_EQ(document.palettes()[1].name, "Harbour");
}

TEST(PaletteColors, aPaletteFromColoursNeedsANameAndColours) {
    SketchDocument document;
    EXPECT_THROW(document.addPaletteFrom("", {Swatch{}}), std::runtime_error);
    EXPECT_THROW(document.addPaletteFrom("Empty", {}), std::runtime_error);
    EXPECT_EQ(document.palettes().size(), 1u);
}
