#include "pythonconsole.h"

#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>

namespace {

    curvy::ScriptResult runOn(curvy::SketchDocument& document, const std::string& code) {
        return curvytest::sharedConsole().run(code, &document);
    }

}

TEST(ScriptModule, pageSettings) {
    curvy::SketchDocument document;
    document.newBlank("Page", 612.0f, 792.0f);
    auto result = runOn(document, "doc.ppi = 144\ndoc.page_color = (1, 0, 0, 1)\ndoc.color_mode = 'cmyk'\ndoc.name = 'Renamed'\n"
                                  "print(doc.ppi, doc.page_color, doc.color_mode, doc.name)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "144.0 (1.0, 0.0, 0.0, 1.0) cmyk Renamed\n");
    EXPECT_FLOAT_EQ(document.ppi(), 144.0f);
    EXPECT_EQ(document.colorMode(), curvy::ColorSpace::CMYK);
    EXPECT_FALSE(runOn(document, "doc.color_mode = 'lab'").ok);
}

TEST(ScriptModule, layersAndGroups) {
    curvy::SketchDocument document;
    document.newBlank("Objects", 100.0f, 100.0f);
    auto result = runOn(document,
                        "a = doc.add_layer('Back')\n"
                        "b = doc.add_layer('Front')\n"
                        "g = b.add_group('Inner')\n"
                        "print([l.name for l in doc.layers], [x.name for x in b.groups], a.is_layer, g.is_layer)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "['Back', 'Front'] ['Inner'] True False\n");
    EXPECT_EQ(document.childrenOf().size(), 2u);

    result = runOn(document, "doc.reorder_layers([b, a])\nprint([l.name for l in doc.layers])");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "['Front', 'Back']\n");

    result = runOn(document, "a.name = 'Bottom'\nprint(a.name)\ndoc.undo()\nprint(a.name)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "Bottom\nBack\n");
}

TEST(ScriptModule, addCircleAndArtwork) {
    curvy::SketchDocument document;
    document.newBlank("Art", 100.0f, 100.0f);
    auto result = runOn(document,
                        "layer = doc.add_layer()\n"
                        "fill = layer.add_circle(50, 50, 10, (0, 1, 0, 1))\n"
                        "art = doc.artwork()\n"
                        "print(art is not None, fill.name())");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output.substr(0, 5), "True ");
}

TEST(ScriptModule, nodesInsideAnObject) {
    curvy::SketchDocument document;
    document.newBlank("Inner", 100.0f, 100.0f);
    auto result = runOn(document,
                        "layer = doc.add_layer()\n"
                        "c = doc.add_node('circle', 'inner', parent=layer)\n"
                        "doc.set_param(c, 'radius', 7)\n"
                        "print(c.name(), len(layer.network) > 1)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "inner True\n");
}

TEST(ScriptModule, palettesAndSwatches) {
    curvy::SketchDocument document;
    document.newBlank("Swatches", 100.0f, 100.0f);
    const std::size_t before = document.palettes().size();
    auto result = runOn(document,
                        "i = doc.add_palette('Mine')\n"
                        "doc.add_swatch(i, curvy.Swatch(1, 0, 0, 1, 'red'))\n"
                        "doc.active_palette = i\n"
                        "p = doc.palettes[i]\n"
                        "print(p.name, len(p), p.swatches[0].name, p.swatches[0].r)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "Mine 1 red 1.0\n");
    EXPECT_EQ(document.palettes().size(), before + 1);
    EXPECT_FALSE(runOn(document, "doc.active_palette = 99").ok);
}

TEST(ScriptModule, aGoneObjectRaises) {
    curvy::SketchDocument document;
    document.newBlank("Gone", 100.0f, 100.0f);
    auto result = runOn(document, "layer = doc.add_layer('Temp')\ndoc.undo()\nlayer.name");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("no longer exists"), std::string::npos) << result.error;
}
