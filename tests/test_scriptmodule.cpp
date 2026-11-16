#include "pythonconsole.h"

#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

    curvy::ScriptResult runOn(curvy::SketchDocument& document, const std::string& code) {
        return curvytest::sharedConsole().run(code, &document);
    }

}

TEST(ScriptModule, everySampleScriptRunsAndDrawsSomething) {
    const std::filesystem::path folder = std::filesystem::path(__FILE__).parent_path().parent_path() / "Resources" / "samples" / "scripts";
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() != ".py") {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        const std::string code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        curvy::SketchDocument document;
        document.newBlank("Sample", 612.0f, 792.0f);
        const std::size_t before = document.undoStack().depth();
        auto result = runOn(document, code);
        EXPECT_TRUE(result.ok) << entry.path().filename().string() << ": " << result.error;
        EXPECT_EQ(document.undoStack().depth(), before + 1) << entry.path().filename().string() << " is not one undo step";
        EXPECT_NE(document.artwork(), nullptr) << entry.path().filename().string();
        ++count;
    }
    EXPECT_GE(count, 4);
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

TEST(ScriptModule, aScriptIsOneUndoStep) {
    curvy::SketchDocument document;
    document.newBlank("Steps", 100.0f, 100.0f);
    const std::size_t before = document.undoStack().depth();
    auto result = runOn(document, "a = doc.add_layer('A')\nb = doc.add_layer('B')\na.add_circle(5, 5, 2)\ndoc.name = 'Changed'");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(document.undoStack().depth(), before + 1);
    EXPECT_EQ(document.undoStack().undoDescription(), "Script");
    EXPECT_EQ(document.childrenOf().size(), 2u);

    document.undoStack().undo();
    EXPECT_EQ(document.childrenOf().size(), 0u);
    document.undoStack().redo();
    EXPECT_EQ(document.childrenOf().size(), 2u);
}

TEST(ScriptModule, aScriptThatFailsMidwayStillMakesOneStep) {
    curvy::SketchDocument document;
    document.newBlank("Fails", 100.0f, 100.0f);
    const std::size_t before = document.undoStack().depth();
    auto result = runOn(document, "doc.add_layer('A')\ndoc.add_layer('B')\nraise ValueError('boom')");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(document.undoStack().depth(), before + 1);
    EXPECT_EQ(document.childrenOf().size(), 2u);
}

TEST(ScriptModule, aScriptThatChangesNothingAddsNoStep) {
    curvy::SketchDocument document;
    document.newBlank("Quiet", 100.0f, 100.0f);
    const std::size_t before = document.undoStack().depth();
    EXPECT_TRUE(runOn(document, "print(doc.name, len(doc.layers))").ok);
    EXPECT_EQ(document.undoStack().depth(), before);
}

TEST(ScriptModule, undoInsideAScriptClosesTheStepSoFar) {
    curvy::SketchDocument document;
    document.newBlank("Inside", 100.0f, 100.0f);
    EXPECT_TRUE(runOn(document, "doc.add_layer('A')").ok);
    const std::size_t before = document.undoStack().depth();
    auto result = runOn(document, "doc.add_layer('B')\ndoc.add_layer('C')\ndoc.undo()\ndoc.add_layer('D')\nprint([l.name for l in doc.layers])");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "['A', 'D']\n");
    EXPECT_EQ(document.undoStack().depth(), before + 1);
    document.undoStack().undo();
    EXPECT_EQ(document.childrenOf().size(), 1u);
}

TEST(ScriptModule, newBlankInsideAScriptKeepsLaterEditsAsOneStep) {
    curvy::SketchDocument document;
    document.newBlank("Fresh", 100.0f, 100.0f);
    EXPECT_TRUE(runOn(document, "doc.add_layer('Old')").ok);
    auto result = runOn(document, "doc.new_blank('New', 50, 50)\ndoc.add_layer('X')\ndoc.add_layer('Y')");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(document.undoStack().depth(), 1u);
    EXPECT_EQ(document.childrenOf().size(), 2u);
}

TEST(ScriptModule, aTransactionBlockThatRaisesTakesItsEditsBack) {
    curvy::SketchDocument document;
    document.newBlank("Block", 100.0f, 100.0f);
    auto result = runOn(document,
                        "doc.add_layer('Kept')\n"
                        "try:\n"
                        "    with doc.transaction('Risky'):\n"
                        "        doc.add_layer('Lost 1')\n"
                        "        doc.add_layer('Lost 2')\n"
                        "        raise ValueError('boom')\n"
                        "except ValueError as e:\n"
                        "    print('caught', e)\n"
                        "print([l.name for l in doc.layers])\n"
                        "doc.undo()\n"
                        "print(len(doc.layers))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "caught boom\n['Kept']\n0\n");
}

TEST(ScriptModule, aTransactionBlockThatEndsNormallyKeepsItsEditsAsOneStep) {
    curvy::SketchDocument document;
    document.newBlank("Block", 100.0f, 100.0f);
    const std::size_t before = document.undoStack().depth();
    auto result = runOn(document,
                        "with doc.transaction('Pair'):\n"
                        "    doc.add_layer('A')\n"
                        "    doc.add_layer('B')\n"
                        "print(len(doc.layers))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "2\n");
    EXPECT_EQ(document.undoStack().depth(), before + 1);
    EXPECT_FALSE(document.undoStack().canRedo());
}

TEST(ScriptModule, aFailedTransactionBlockDoesNotTouchEarlierEditsOrLeaveRedoSteps) {
    curvy::SketchDocument document;
    document.newBlank("Block", 100.0f, 100.0f);
    auto result = runOn(document,
                        "with doc.transaction():\n"
                        "    doc.add_layer('Only')\n"
                        "    1 / 0\n");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("ZeroDivisionError"), std::string::npos);
    EXPECT_EQ(document.childrenOf().size(), 0u);
    EXPECT_FALSE(document.undoStack().canRedo());
}
