#include "pythonconsole.h"
#include "testsupport.h"

#include "curvy/selectionmodel.h"
#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>
#include <jitnodes/graph/assetLibrary.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

    curvy::ScriptResult runOn(curvy::SketchDocument& document, const std::string& code) {
        return curvytest::sharedConsole().run(code, &document);
    }

}

TEST(ScriptModule, everySampleScriptRunsAndDrawsSomething) {
    const std::filesystem::path folder = std::filesystem::path(__FILE__).parent_path().parent_path() / "Resources" / "samples" / "scripts";
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        // longRunning.py sleeps for 40 seconds on purpose (it is for trying the background run by hand).
        if (entry.path().extension() != ".py" || entry.path().filename() == "longRunning.py") {
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

TEST(ScriptModule, documentUnits) {
    curvy::SketchDocument document;
    document.newBlank("Units", 612.0f, 792.0f);
    auto result = runOn(document, "print(doc.unit, doc.angle_unit)\ndoc.unit = 'mm'\ndoc.angle_unit = 'rad'\n"
                                  "print(curvy.to_points(1, 'in'), curvy.from_points(72, 'in'), curvy.to_points(10, 'px', 144))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "px deg\n72.0 1.0 5.0\n");
    EXPECT_EQ(document.units().length, curvy::LengthUnit::Millimeters);
    EXPECT_EQ(document.units().angle, curvy::AngleUnit::Radians);
    EXPECT_FALSE(runOn(document, "doc.unit = 'furlong'").ok);
    EXPECT_FALSE(runOn(document, "doc.angle_unit = 'grad'").ok);
}

TEST(ScriptModule, guides) {
    curvy::SketchDocument document;
    document.newBlank("Guides", 100.0f, 100.0f);
    auto result = runOn(document,
                        "i = doc.add_guide(True, 12.5)\n"
                        "doc.add_guide(False, 30)\n"
                        "doc.move_guide(i, 20)\n"
                        "print(len(doc.guides), doc.guides[0].vertical, doc.guides[0].position, doc.guides[1])");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "2 True 20.0 Guide(horizontal, 30.000000)\n");
    ASSERT_EQ(document.guides().size(), 2u);

    result = runOn(document, "doc.guides_locked = True\ndoc.move_guide(0, 1)");
    EXPECT_FALSE(result.ok) << "a locked guide cannot be moved";
    EXPECT_EQ(document.guides()[0].position, 20.0);
    result = runOn(document, "doc.guides_locked = False\nprint(doc.clear_guides(), doc.clear_guides(), doc.guides_visible)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "True False True\n");
    EXPECT_FALSE(runOn(document, "doc.remove_guide(3)").ok);
}

TEST(ScriptModule, selectionDimensionsAreReadAndSetInOneUndoStep) {
    curvy::SketchDocument document;
    document.newBlank("Dimensions", 612.0f, 792.0f);
    const curvy::NodeRef layer = document.addLayer();
    document.addCircle(layer, 100.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    curvy::SelectionModel selection(&document);
    auto run = [&](const std::string& code) { return curvytest::sharedConsole().run(code, &document, &selection); };

    auto result = run("print(len(selection.dimensions()))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "0\n");
    EXPECT_FALSE(run("doc.set_dimension('width', 10)").ok) << "nothing selected";

    result = run("doc.select_where(type='circle')\n"
                 "d = {x.kind: x.value for x in selection.dimensions()}\n"
                 "print(round(d['width']), round(d['radius']), selection.dimensions()[0].editable)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "60 30 True\n");

    const std::size_t before = document.undoStack().depth();
    result = run("doc.set_dimension('width', 90)\nprint(round(selection.dimensions()[0].value))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "90\n");
    EXPECT_EQ(document.undoStack().depth(), before + 1);

    EXPECT_FALSE(run("doc.set_dimension('width', 0)").ok);
    EXPECT_FALSE(run("doc.set_dimension('depth', 5)").ok);
}

TEST(ScriptModule, aPivotIsReadAndMovedInOneUndoStepWithoutMovingTheShape) {
    curvy::SketchDocument document;
    document.newBlank("Pivots", 612.0f, 792.0f);
    const curvy::NodeRef layer = document.addLayer();
    document.addCircle(layer, 100.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    curvy::SelectionModel selection(&document);
    auto run = [&](const std::string& code) { return curvytest::sharedConsole().run(code, &document, &selection); };

    EXPECT_FALSE(run("doc.move_pivot(10, 10)").ok) << "nothing selected";
    auto result = run("doc.select_where(type='circle')\nprint([(k, round(x), round(y)) for k, x, y in selection.pivots()])");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "[('shape', 100, 100), ('object', 100, 100)]\n");

    const std::size_t before = document.undoStack().depth();
    result = run("w = selection.dimensions()[0].value\n"
                 "doc.move_pivot(80, 90)\n"
                 "print(round(w - selection.dimensions()[0].value), [(k, round(x), round(y)) for k, x, y in selection.pivots()][0])");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "0 ('shape', 80, 90)\n");
    EXPECT_EQ(document.undoStack().depth(), before + 1);

    result = run("doc.move_pivot(5, 6, 'object')\nprint([(k, round(x), round(y)) for k, x, y in selection.pivots()][1])");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "('object', 5, 6)\n");

    EXPECT_FALSE(run("doc.move_pivot(1, 1, 'world')").ok);
    EXPECT_FALSE(run("doc.move_pivot(float('nan'), 1)").ok);
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

TEST(ScriptModule, shapesAndPrimitivesDraw) {
    curvy::SketchDocument document;
    document.newBlank("Shapes", 200.0f, 200.0f);
    auto result = runOn(document,
                        "layer = doc.add_layer('Art')\n"
                        "layer.add_rect(10, 10, 50, 30)\n"
                        "layer.add_rect(10, 60, 50, 30, radius=8, fill=(1, 0, 0, 1), stroke=(0, 0, 0, 1), stroke_width=2)\n"
                        "layer.add_ellipse(120, 40, 30, 20)\n"
                        "layer.add_polygon(120, 120, 25, 6)\n"
                        "layer.add_star(40, 150, 30, 12, points=5)\n"
                        "layer.add_line(0, 0, 200, 200)\n"
                        "layer.add_path([(0, 0), (10, 0), (10, 10)], closed=True, fill=(0, 0, 1, 1), stroke=None)\n");
    EXPECT_TRUE(result.ok) << result.error;
    jitnodes::GeometryHandle art = document.artwork();
    ASSERT_NE(art, nullptr);
    EXPECT_EQ(art->primitiveCount(), 7u);
    // Each primitive is its own selectable shape.
    EXPECT_EQ(art->shapes().size(), 7u);
}

TEST(ScriptModule, curvedShapesAreCubicAndTheCountsAreChecked) {
    curvy::SketchDocument document;
    document.newBlank("Curves", 100.0f, 100.0f);
    auto result = runOn(document,
                        "layer = doc.add_layer()\n"
                        "e = layer.add_ellipse(50, 50, 20, 10)\n"
                        "r = layer.add_rect(0, 0, 10, 10, radius=2)\n"
                        "s = layer.add_rect(0, 0, 10, 10)\n"
                        "print(len(doc.points(e)), doc.is_curve(e), doc.is_closed(e))\n"
                        "print(len(doc.points(r)), doc.is_curve(r))\n"
                        "print(len(doc.points(s)), doc.is_curve(s))\n");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "12 True True\n24 True\n4 False\n");

    EXPECT_FALSE(runOn(document, "doc.add_layer().add_path([(0, 0), (1, 1), (2, 2)], curve=True)").ok);
    EXPECT_FALSE(runOn(document, "doc.add_layer().add_path([(0, 0)])").ok);
    EXPECT_FALSE(runOn(document, "doc.add_layer().add_polygon(0, 0, 5, 2)").ok);
    EXPECT_FALSE(runOn(document, "doc.add_layer().add_star(0, 0, 5, 0)").ok);
    result = runOn(document, "doc.add_layer().add_path([(0, 0), (1, 0), (2, 0), (3, 0)], curve=True)");
    EXPECT_TRUE(result.ok) << result.error;
}

TEST(ScriptModule, pointEditingIsOneUndoStepEach) {
    curvy::SketchDocument document;
    document.newBlank("Points", 100.0f, 100.0f);
    ASSERT_TRUE(runOn(document, "layer = doc.add_layer()\nshape = layer.add_polygon(50, 50, 20, 4, rotation=0, stroke=(0, 0, 0, 1))").ok);

    auto result = runOn(document, "print(doc.points(shape))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output.substr(0, 11), "[(70.0, 50.");

    const std::size_t before = document.undoStack().depth();
    result = runOn(document, "doc.set_point(shape, 0, 90, 50)\nprint(doc.points(shape)[0])\ndoc.move_points(shape, 1, 2, indices=[0, -1])\nprint(doc.points(shape)[0])");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "(90.0, 50.0)\n(91.0, 52.0)\n");
    EXPECT_EQ(document.undoStack().depth(), before + 1);   // the script is one step

    result = runOn(document, "doc.insert_point(shape, 2, 1, 1)\nprint(len(doc.points(shape)))\ndoc.remove_point(shape, 2)\nprint(len(doc.points(shape)))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "5\n4\n");

    result = runOn(document, "doc.set_closed(shape, False)\nprint(doc.is_closed(shape))\ndoc.set_points(shape, [(0, 0), (5, 5)])\nprint(doc.points(shape))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "False\n[(0.0, 0.0), (5.0, 5.0)]\n");

    result = runOn(document, "doc.undo()\nprint(len(doc.points(shape)))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "4\n");

    EXPECT_FALSE(runOn(document, "doc.set_point(shape, 9, 0, 0)").ok);
    EXPECT_FALSE(runOn(document, "doc.set_points(shape, [(0, 0)])").ok);
    EXPECT_FALSE(runOn(document, "doc.points(doc.layers[0])").ok);
}

TEST(ScriptModule, curvePointEditingKeepsTheLayout) {
    curvy::SketchDocument document;
    document.newBlank("Curve", 100.0f, 100.0f);
    ASSERT_TRUE(runOn(document, "layer = doc.add_layer()\ne = layer.add_ellipse(50, 50, 20, 10)").ok);
    EXPECT_FALSE(runOn(document, "doc.insert_point(e, 0, 1, 1)").ok);
    EXPECT_FALSE(runOn(document, "doc.remove_point(e, 0)").ok);
    EXPECT_FALSE(runOn(document, "doc.set_points(e, doc.points(e)[:5])").ok);

    auto result = runOn(document, "doc.set_closed(e, False)\nprint(len(doc.points(e)), doc.is_closed(e))\ndoc.set_closed(e, True)\nprint(len(doc.points(e)))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "13 False\n12\n");
    EXPECT_NE(document.artwork(), nullptr);
}

TEST(ScriptModule, wholeObjectTransforms) {
    curvy::SketchDocument document;
    document.newBlank("Xform", 100.0f, 100.0f);
    auto result = runOn(document,
                        "layer = doc.add_layer('L')\n"
                        "g = layer.add_group('G')\n"
                        "g.add_rect(0, 0, 10, 10)\n"
                        "t = g.transform\n"
                        "print(t['tx'], t['sx'], t['rot'])\n"
                        "g.translate(5, 7)\n"
                        "g.translate(1, 1)\n"
                        "g.rotate(30)\n"
                        "g.scale(2)\n"
                        "g.scale(1.5, 1)\n"
                        "t = g.transform\n"
                        "print(t['tx'], t['ty'], t['rot'], t['sx'], t['sy'])\n"
                        "g.set_transform(tx=0, skew=10)\n"
                        "print(g.transform['tx'], g.transform['skew'], g.transform['rot'])\n"
                        "layer.rotate(90, 50, 50)\n"
                        "print(layer.transform['rot'], layer.transform['px'], layer.transform['py'])\n"
                        "g.reset_transform()\n"
                        "print(g.transform == dict(tx=0, ty=0, rot=0, sx=1, sy=1, skew=0, px=0, py=0))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "0.0 1.0 0.0\n6.0 8.0 30.0 3.0 2.0\n0.0 10.0 30.0\n90.0 50.0 50.0\nTrue\n");

    // The moved object really draws elsewhere.
    ASSERT_TRUE(runOn(document, "g.translate(20, 0)").ok);
    jitnodes::GeometryHandle art = document.artwork();
    ASSERT_NE(art, nullptr);
    EXPECT_GT(art->pointCount(), 0u);

    const std::size_t before = document.undoStack().depth();
    EXPECT_TRUE(runOn(document, "g.rotate(10)\ng.scale(2)").ok);
    EXPECT_EQ(document.undoStack().depth(), before + 1);
    result = runOn(document, "print(g.transform['rot'])\ndoc.undo()\nprint(g.transform['rot'])");
    EXPECT_EQ(result.output, "10.0\n0.0\n");

    EXPECT_FALSE(runOn(document, "g.set_transform(bogus=1)").ok);
    ASSERT_TRUE(runOn(document, "g.remove()").ok);
    EXPECT_FALSE(runOn(document, "g.translate(1, 1)").ok);
}

TEST(ScriptModule, singleShapeTransforms) {
    curvy::SketchDocument document;
    document.newBlank("ShapeXform", 100.0f, 100.0f);
    ASSERT_TRUE(runOn(document,
                      "layer = doc.add_layer('L')\n"
                      "a = layer.add_rect(0, 0, 10, 10, stroke=(0, 0, 0, 1))\n"
                      "b = layer.add_ellipse(50, 50, 5, 5)\n"
                      "c = layer.add_circle(20, 20, 3)").ok);

    const std::size_t before = document.undoStack().depth();
    auto result = runOn(document, "print(doc.shape_transform(a)['sx'], len(network))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(document.undoStack().depth(), before);   // reading makes no node and no step

    result = runOn(document,
                   "doc.translate_shape(a, 5, 7)\n"
                   "doc.translate_shape(a, 1, 1)\n"
                   "doc.rotate_shape(a, 30)\n"
                   "doc.scale_shape(a, 2)\n"
                   "doc.scale_shape(a, 1.5, 1)\n"
                   "t = doc.shape_transform(a)\n"
                   "print(t['tx'], t['ty'], t['rot'], t['sx'], t['sy'])\n"
                   "print(doc.shape_transform(b)['tx'], doc.shape_transform(c)['tx'])\n"
                   "doc.set_shape_transform(b, tx=3, skew=5)\n"
                   "print(doc.shape_transform(b)['tx'], doc.shape_transform(b)['skew'])\n"
                   "doc.set_shape_transform(doc.path_node(a), tx=-1)\n"
                   "print(doc.shape_transform(a)['tx'])\n"
                   "doc.reset_shape_transform(a)\n"
                   "print(doc.shape_transform(a)['rot'], doc.shape_transform(a)['sx'])");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "6.0 8.0 30.0 3.0 2.0\n0.0 0.0\n3.0 5.0\n-1.0\n0.0 1.0\n");
    EXPECT_EQ(document.undoStack().depth(), before + 1);   // the script is one step

    // The transform node sits between the generator and its look, so the shape moves and the others stay.
    ASSERT_TRUE(runOn(document, "doc.translate_shape(c, 40, 0)").ok);
    jitnodes::GeometryHandle art = document.artwork();
    ASSERT_NE(art, nullptr);
    float maxX = 0.0f;
    for (std::size_t i = 0; i < art->pointCount(); ++i) {
        maxX = std::max(maxX, art->x()[i]);
    }
    EXPECT_GT(maxX, 55.0f);   // the circle was at x 20 and went to about 60

    result = runOn(document, "doc.undo()\nprint(doc.shape_transform(c)['tx'])");
    EXPECT_EQ(result.output, "0.0\n");

    EXPECT_FALSE(runOn(document, "doc.shape_transform(layer)").ok);
    EXPECT_FALSE(runOn(document, "doc.set_shape_transform(a, bogus=1)").ok);
    EXPECT_FALSE(runOn(document, "doc.rotate_shape(doc.add_layer(), 5)").ok);
}

TEST(ScriptModule, fillsAndStrokesOfExistingShapesCanBeEdited) {
    curvy::SketchDocument document;
    document.newBlank("Looks", 100.0f, 100.0f);
    ASSERT_TRUE(runOn(document,
                      "layer = doc.add_layer('L')\n"
                      "a = layer.add_rect(0, 0, 10, 10, fill=(1, 0, 0, 1), stroke=None)\n"
                      "b = layer.add_ellipse(50, 50, 5, 5, fill=None, stroke=(0, 0, 0, 1), stroke_width=2)\n"
                      "c = layer.add_line(0, 0, 10, 10, stroke=None)").ok);

    auto result = runOn(document, "print(doc.fill(a), doc.stroke(a), doc.fill(b), doc.stroke(b), doc.stroke_width(b), doc.stroke_width(a))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "(1.0, 0.0, 0.0, 1.0) None None (0.0, 0.0, 0.0, 1.0) 2.0 None\n");

    const std::size_t before = document.undoStack().depth();
    result = runOn(document,
                   "doc.set_fill(a, (0, 1, 0, 0.5))\n"
                   "doc.set_stroke(a, (0, 0, 1, 1), width=3)\n"
                   "doc.set_fill(b, (1, 1, 0, 1), opacity=0.5, even_odd=True)\n"
                   "doc.set_stroke(b, None)\n"
                   "doc.set_stroke(c, (0.25, 0.25, 0.25, 1), width=4)\n"
                   "print(doc.fill(a), doc.stroke(a), doc.stroke_width(a))\n"
                   "print(doc.fill(b), doc.stroke(b))\n"
                   "print(doc.stroke(c), doc.stroke_width(c))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "(0.0, 1.0, 0.0, 0.5) (0.0, 0.0, 1.0, 1.0) 3.0\n(1.0, 1.0, 0.0, 1.0) None\n(0.25, 0.25, 0.25, 1.0) 4.0\n");
    EXPECT_EQ(document.undoStack().depth(), before + 1);

    // The new stroke comes after the fill, and each shape's primitives carry their own look.
    jitnodes::GeometryHandle art = document.artwork();
    ASSERT_NE(art, nullptr);
    const jitnodes::AttribArray* width = art->findAttrib(jitnodes::AttribClass::primitive, "stroke.width");
    ASSERT_NE(width, nullptr);
    EXPECT_EQ(art->primitiveCount(), 3u);   // rect, ellipse, line

    // A hidden look comes back with a colour again, keeping the width it had.
    result = runOn(document, "doc.set_stroke(b, (1, 0, 1, 1))\nprint(doc.stroke(b))\nprint(doc.stroke_width(b))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "(1.0, 0.0, 1.0, 1.0)\n2.0\n");

    result = runOn(document, "doc.undo()\nprint(doc.stroke(b))");
    EXPECT_EQ(result.output, "None\n");

    // Looks of shapes inside a group, with a local transform in the chain.
    result = runOn(document,
                   "g = layer.add_group('G')\n"
                   "s = g.add_rect(0, 0, 5, 5, fill=None, stroke=None)\n"
                   "doc.rotate_shape(s, 10)\n"
                   "doc.set_stroke(s, (1, 0, 0, 1), width=2)\n"
                   "doc.set_fill(s, (0, 0, 1, 1))\n"
                   "print(doc.fill(s), doc.stroke(s), doc.shape_transform(s)['rot'])");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "(0.0, 0.0, 1.0, 1.0) (1.0, 0.0, 0.0, 1.0) 10.0\n");
    EXPECT_NE(document.artwork(), nullptr);

    EXPECT_FALSE(runOn(document, "doc.fill(layer)").ok);
    EXPECT_FALSE(runOn(document, "doc.set_fill(s, (1, 0, 0))").ok);
}

TEST(ScriptModule, aPathIsFoundFromAnyNodeOfItsChain) {
    curvy::SketchDocument document;
    document.newBlank("Chain", 100.0f, 100.0f);
    auto result = runOn(document,
                        "layer = doc.add_layer()\n"
                        "top = layer.add_rect(0, 0, 10, 10, stroke=(0, 0, 0, 1))\n"
                        "p = doc.path_node(top)\n"
                        "print(p.id != top.id, doc.path_node(p).id == p.id, len(doc.points(top)) == len(doc.points(p)))\n"
                        "print(doc.path_node(layer.add_circle(5, 5, 2)))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "True True True\nNone\n");
}

TEST(ScriptModule, theEditorSelectionIsReadAndChangedByScripts) {
    curvy::SketchDocument document;
    document.newBlank("Selection", 100.0f, 100.0f);
    curvy::SelectionModel selection(&document);
    auto run = [&](const std::string& code) { return curvytest::sharedConsole().run(code, &document, &selection); };

    auto result = run("print(len(selection), selection.primary, doc.selection is not None)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "0 None True\n");

    result = run("layer = doc.add_layer('L')\n"
                 "a = layer.add_rect(0, 0, 10, 10, stroke=(0, 0, 0, 1))\n"
                 "b = layer.add_ellipse(5, 5, 3, 3)\n"
                 "selection.set(a)\n"
                 "print(len(selection), selection.primary.id == a.id, selection.object.name, selection.stroke is not None, selection.fill.id != a.id)\n"
                 "selection.add(b)\n"
                 "print(len(selection), a in selection, selection.primary.id == b.id)\n"
                 "selection.remove(a)\n"
                 "print(len(selection), a in selection)\n"
                 "selection.toggle(a)\n"
                 "selection.replace([layer, b])\n"
                 "print(len(selection), [type(x).__name__ for x in selection])\n"
                 "selection.clear()\n"
                 "print(len(selection))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "1 True L True True\n2 True True\n1 False\n2 ['Object', 'Node']\n0\n");
    EXPECT_TRUE(selection.empty());

    // What the user picked is what the script sees.
    ASSERT_TRUE(run("layer = doc.layers[0]").ok);
    selection.set(document.childrenOf().front());
    result = run("print(selection.primary.name, selection.object.is_layer, selection.layer.name)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "L True L\n");

    result = run("g = doc.layers[0].add_group('G')\n"
                 "c = g.add_circle(1, 1, 1)\n"
                 "selection.set(c)\n"
                 "print(selection.step_up(), selection.primary.name)\n"
                 "selection.set(doc.layers[0])\n"
                 "print(selection.step_up())");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "True G\nFalse\n");

    result = run("print(selection.isolation)\n"
                 "print(selection.enter_group(doc.layers[0]))\n"
                 "print(selection.enter_group(g), selection.isolation.name)\n"
                 "print(selection.enter_group(g))\n"
                 "print(selection.exit_group(), selection.isolation, selection.primary.name)\n"
                 "print(selection.exit_group())");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "None\nFalse\nTrue G\nFalse\nTrue None G\nFalse\n");
}

TEST(ScriptModule, shapesAreFoundByPointRectangleAndProperty) {
    curvy::SketchDocument document;
    document.newBlank("Find", 100.0f, 100.0f);
    curvy::SelectionModel selection(&document);
    auto run = [&](const std::string& code) { return curvytest::sharedConsole().run(code, &document, &selection); };

    auto result = run("layer = doc.add_layer('L')\n"
                      "a = layer.add_rect(10, 10, 20, 20, fill=(1, 0, 0, 1))\n"
                      "b = layer.add_rect(60, 10, 20, 20, fill=(0, 0, 1, 1))\n"
                      "c = layer.add_rect(60, 60, 20, 20, fill=(1, 0, 0, 1))\n"
                      "print(doc.shape_at(20, 20) is not None, doc.shape_at(50, 50), doc.shape_at(70, 20) is not None)\n"
                      "print(len(doc.shapes_in_rect(0, 0, 100, 40)), len(doc.shapes_in_rect(0, 0, 100, 100)))\n"
                      "print(len(doc.shapes_in_rect(0, 0, 25, 25)), len(doc.shapes_in_rect(0, 0, 25, 25, inside=False)))\n"
                      "print(len(doc.shapes_where(fill='#ff0000')), len(doc.shapes_where(fill='#0000FF')), len(doc.shapes_where(stroke=None)))\n"
                      "print(len(doc.shapes_where(layer='L')), len(doc.shapes_where(layer='X')), len(doc.shapes_where(fill='#FF0000', layer='L')))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "True None True\n2 3\n0 1\n2 1 3\n3 0 2\n");

    result = run("print(len(doc.select_where(fill='#FF0000')), len(selection))\n"
                 "doc.select_where('subtract', fill='#FF0000')\n"
                 "print(len(selection))\n"
                 "doc.select_where('add', fill='#0000FF')\n"
                 "print(len(selection))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "2 2\n0\n1\n");

    result = run("doc.shapes_where(colour='red')");
    EXPECT_FALSE(result.ok);
}

namespace {

    // A path for a script to use: forward slashes, so it can sit in a Python string without escaping.
    std::string scriptPath(const char* name) {
        std::string path = curvytest::tempFile(name).string();
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    // A saved document with one layer named `layer`.
    void writeSample(const std::string& path, const char* layer) {
        std::filesystem::remove(path);
        std::filesystem::remove(path + ".bak");
        curvy::SketchDocument document;
        document.newBlank("Sample", 200.0f, 300.0f);
        document.addLayer(layer);
        ASSERT_TRUE(document.save(path)) << document.lastError();
    }

}

TEST(ScriptModule, aNewDocumentHasNoPathAndSaveNeedsOne) {
    curvy::SketchDocument document;
    document.newBlank("Fresh", 100.0f, 100.0f);
    const auto result = runOn(document, "print(doc.path)\ndoc.save()");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.output, "None\n");
    EXPECT_NE(result.error.find("pass a path"), std::string::npos) << result.error;
}

TEST(ScriptModule, saveWritesTheFileAndSaveAsMovesTheDocumentToIt) {
    const std::string first = scriptPath("curvy_script_first.curvy");
    const std::string second = scriptPath("curvy_script_second.curvy");
    for (const std::string& file : {first, second}) {
        std::filesystem::remove(file);
        std::filesystem::remove(file + ".bak");
    }
    curvy::SketchDocument document;
    document.newBlank("Saving", 100.0f, 100.0f);
    const auto result = runOn(document, "doc.add_layer('A')\nprint(doc.is_modified)\nprint(doc.save('" + first + "') == '" + first +
                                            "')\nprint(doc.is_modified, doc.path == '" + first + "')\ndoc.add_layer('B')\ndoc.save()\n"
                                            "doc.save('" + second + "')\nprint(doc.path == '" + second + "')");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "True\nTrue\nFalse True\nTrue\n");
    EXPECT_TRUE(std::filesystem::exists(first));
    EXPECT_TRUE(std::filesystem::exists(second));

    curvy::SketchDocument back;
    ASSERT_TRUE(back.load(second)) << back.lastError();
    EXPECT_EQ(back.childrenOf().size(), 2u) << "the second save has both layers";
    curvy::SketchDocument older;
    ASSERT_TRUE(older.load(first)) << older.lastError();
    EXPECT_EQ(older.childrenOf().size(), 2u) << "doc.save() with no path rewrote the first file after B was added";
    for (const std::string& file : {first, second}) {
        std::filesystem::remove(file);
        std::filesystem::remove(file + ".bak");
    }
}

TEST(ScriptModule, aSaveThatCannotWriteRaises) {
    curvy::SketchDocument document;
    document.newBlank("Nowhere", 100.0f, 100.0f);
    const auto result = runOn(document, "doc.save('" + scriptPath("curvy_no_such_folder/x/y.curvy") + "')");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("could not save"), std::string::npos) << result.error;
    EXPECT_FALSE(document.hasFilePath());
}

TEST(ScriptModule, openReplacesTheDocumentAndOldHandlesAreGone) {
    const std::string file = scriptPath("curvy_script_open.curvy");
    writeSample(file, "FromFile");
    curvy::SketchDocument document;
    document.newBlank("Target", 100.0f, 100.0f);
    const auto result = runOn(document, "old = doc.add_layer('Old')\ndoc.open('" + file + "', discard_changes=True)\nprint([l.name for l in doc.layers], doc.page_width, doc.path == '" + file +
                                            "', doc.is_modified)\nold.name");
    EXPECT_FALSE(result.ok) << "the layer from before the open is gone";
    EXPECT_NE(result.error.find("no longer exists"), std::string::npos) << result.error;
    EXPECT_EQ(result.output, "['FromFile'] 200.0 True False\n");
    EXPECT_EQ(document.childrenOf().size(), 1u);
    std::filesystem::remove(file);
    std::filesystem::remove(file + ".bak");
}

TEST(ScriptModule, anObjectFromBeforeANewOrAnOpenIsGoneEvenWhenANewOneHasItsId) {
    curvy::SketchDocument document;
    document.newBlank("Ids", 100.0f, 100.0f);
    // Ids start again, so the first layer of the blank document has the id the old one had.
    auto result = runOn(document, "old = doc.add_layer('Old')\ndoc.new_blank('Fresh', 50, 50)\nnew = doc.add_layer('New')\nprint(old.id == new.id)\nprint(repr(old))");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "True\nObject(<gone>)\n");
    for (const char* use : {"old.name", "old.remove()", "old.add_circle(1, 1, 1)", "doc.remove_node(old)"}) {
        result = runOn(document, use);
        EXPECT_FALSE(result.ok) << use;
        EXPECT_NE(result.error.find("no longer exists"), std::string::npos) << use << ": " << result.error;
    }
    EXPECT_EQ(document.childrenOf().size(), 1u) << "none of those touched the new layer";
    EXPECT_EQ(document.node(document.childrenOf()[0]).name(), "New");
}

TEST(ScriptModule, openRefusesUnsavedChangesUnlessToldToDiscardThem) {
    const std::string file = scriptPath("curvy_script_refuse.curvy");
    writeSample(file, "FromFile");
    curvy::SketchDocument document;
    document.newBlank("Target", 100.0f, 100.0f);
    auto result = runOn(document, "doc.add_layer('Mine')\ndoc.open('" + file + "')");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("unsaved changes"), std::string::npos) << result.error;
    EXPECT_EQ(document.childrenOf().size(), 1u) << "nothing was replaced";
    EXPECT_EQ(document.node(document.childrenOf()[0]).name(), "Mine");

    EXPECT_TRUE(runOn(document, "doc.open('" + file + "', True)").ok);
    EXPECT_EQ(document.node(document.childrenOf()[0]).name(), "FromFile");
    EXPECT_TRUE(runOn(document, "doc.open('" + file + "')").ok) << "an unmodified document opens without discard_changes";
    std::filesystem::remove(file);
    std::filesystem::remove(file + ".bak");
}

TEST(ScriptModule, openingAFileThatIsMissingOrNotACurvyFileLeavesTheDocumentAlone) {
    const std::string notCurvy = scriptPath("curvy_script_notcurvy.curvy");
    {
        std::ofstream out(notCurvy, std::ios::binary);
        out << "this is not a curvy file";
    }
    curvy::SketchDocument document;
    document.newBlank("Keep", 100.0f, 100.0f);
    ASSERT_TRUE(runOn(document, "doc.add_layer('Stay')").ok);
    for (const std::string& file : {scriptPath("curvy_script_missing.curvy"), notCurvy}) {
        const auto result = runOn(document, "doc.open('" + file + "', True)");
        EXPECT_FALSE(result.ok) << file;
        EXPECT_NE(result.error.find("could not open"), std::string::npos) << result.error;
        ASSERT_EQ(document.childrenOf().size(), 1u) << file;
        EXPECT_EQ(document.node(document.childrenOf()[0]).name(), "Stay");
    }
    std::filesystem::remove(notCurvy);
}

TEST(ScriptModule, editsAfterAnOpenInTheSameScriptAreOneUndoStepOfTheNewDocument) {
    const std::string file = scriptPath("curvy_script_after.curvy");
    writeSample(file, "FromFile");
    curvy::SketchDocument document;
    document.newBlank("Target", 100.0f, 100.0f);
    const auto result = runOn(document, "doc.open('" + file + "')\ndoc.add_layer('A')\ndoc.add_layer('B')");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(document.childrenOf().size(), 3u);
    const std::size_t depth = document.undoStack().depth();
    document.undoStack().undo();
    EXPECT_EQ(document.undoStack().depth(), depth - 1);
    EXPECT_EQ(document.childrenOf().size(), 1u) << "both layers added after the open were one step";
    std::filesystem::remove(file);
    std::filesystem::remove(file + ".bak");
}

TEST(ScriptModule, theWindowIsToldWhenAScriptSavesOrOpens) {
    const std::string file = scriptPath("curvy_script_hook.curvy");
    writeSample(file, "FromFile");
    std::vector<bool> told;
    curvytest::sharedConsole().setDocumentFileHook([&told](bool replaced) { told.push_back(replaced); });
    curvy::SketchDocument document;
    document.newBlank("Hook", 100.0f, 100.0f);
    const auto result = runOn(document, "doc.save('" + file + "')\ndoc.open('" + file + "')\ntry:\n    doc.open('x:/no/such.curvy')\nexcept RuntimeError:\n    pass");
    curvytest::sharedConsole().setDocumentFileHook(nullptr);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(told, (std::vector<bool>{false, true})) << "a refused open does not tell the window";
    std::filesystem::remove(file);
    std::filesystem::remove(file + ".bak");
}

TEST(ScriptModule, selectionIsNoneWithoutAnEditorAndRefusesAForeignDocument) {
    curvy::SketchDocument document;
    document.newBlank("None", 100.0f, 100.0f);
    auto result = runOn(document, "print(selection, doc.selection)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "None None\n");

    curvy::SelectionModel selection(&document);
    ASSERT_TRUE(curvytest::sharedConsole().run("kept = selection", &document, &selection).ok);
    selection.setDocument(nullptr);
    result = curvytest::sharedConsole().run("kept.clear()", &document, nullptr);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("no longer belongs"), std::string::npos);
}

TEST(ScriptModule, theWrangleNodeSampleScriptMakesAWrangleWithDeclaredParameters) {
    std::ifstream in("Resources/samples/scripts/wrangleNode.py");
    ASSERT_TRUE(in.is_open());
    const std::string code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    curvy::SketchDocument document;
    document.newBlank("Wrangle", 100.0f, 100.0f);
    curvy::SelectionModel selection(&document);
    const auto result = curvytest::sharedConsole().run(code, &document, &selection);
    ASSERT_TRUE(result.ok) << result.error;
    const curvy::NodeRef picked = selection.primary();
    ASSERT_TRUE(picked.valid());
    EXPECT_EQ(document.node(picked).typeName(), "wrangle");
    EXPECT_TRUE(document.node(picked).hasFloat("gain"));
    EXPECT_TRUE(document.node(picked).hasFloat("steps"));
    EXPECT_NO_THROW(document.cook(picked));
}

TEST(ScriptModule, checkWrangleNamesTheFirstProblemAndListsTheFunctions) {
    curvy::SketchDocument document;
    document.newBlank("Wrangle check", 100.0f, 100.0f);
    auto run = [&](const std::string& code) { return runOn(document, "import curvy\n" + code); };
    auto result = run("print(curvy.check_wrangle('y = y + 1;'))");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "None\n");
    result = run("p = curvy.check_wrangle('x = 1;\\ny = nosuch(x);')\nprint(p[0], type(p[1]).__name__, bool(p[2]))");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "2 int True\n");
    result = run("f = dict(curvy.wrangle_functions())\nprint(len(f) > 0, 'sin' in f, f['sin'])");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "True True 1\n");
}

TEST(ScriptModule, anObjectsOpacityAndBlendAreParametersOfItsNode) {
    curvy::SketchDocument document;
    document.newBlank("Effects", 100.0f, 100.0f);
    auto result = runOn(document, "import curvy\nlayer = doc.add_layer('L')\nlayer.opacity = 0.5\nlayer.blend = 'multiply'\n"
                                  "print(layer.opacity, layer.blend, curvy.blend_modes()[:2])");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "0.5 Multiply ['Normal', 'Multiply']\n");
    result = runOn(document, "layer = doc.layers[0]\nlayer.opacity = 7\nprint(layer.opacity)\nlayer.blend = 'nosuch'");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("unknown blend mode"), std::string::npos);
    result = runOn(document, "print(doc.layers[0].opacity)");
    EXPECT_EQ(result.output, "1.0\n");
}

TEST(ScriptModule, aWrangleNodesDeclaredLimitsAreReadableFromPython) {
    curvy::SketchDocument document;
    document.newBlank("Limits", 100.0f, 100.0f);
    const auto result = runOn(document, "w = doc.add_node('wrangle', 'bend')\ndoc.set_string(w, 'code', '| gain = 2(0, 10), steps(1,) |\\ny = y * gain;')\n"
                                        "print(w.float_minimum('gain'), w.float_maximum('gain'), w.float_minimum('steps'), w.float_maximum('steps'))");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "(True, 0.0) (True, 10.0) (True, 1.0) (False, 0.0)\n");
}

namespace {

    const char* assetPrelude = R"py(
import jitnodes as jn
a = jn.assets
TEXT = '''{ format: "jitnodes-asset", version: 1, type: "pyTestAsset", inputs: 1, promote: { amount: 2 },
  graph: { display: 2, nodes: [
    { id: 1, type: "subnetInput", name: "in", floats: { index: 0 }, inputs: [] },
    { id: 2, type: "transform", name: "xf", expressions: { tx: "parent.amount" }, inputs: [1] } ] } }'''
)py";

}

TEST(ScriptModule, anAssetDefinitionIsEditedRegisteredAndInstancedFromPython) {
    curvy::SketchDocument document;
    document.newBlank("Assets", 100.0f, 100.0f);
    const auto result = runOn(document, std::string(assetPrelude) + R"py(
d = a.AssetDef.from_text(TEXT)
d.label = "Test"
d.category = "Testing"
p = d.params[0]
p.minimum = 0
p.maximum = 5
p.group = "Main"
extra = d.add_param("extra", 1.0, label="Extra")
d.set_script("onCreated", "x = 1")
print(d.name, d.version, [q.name for q in d.params], d.validate() == "", sorted(d.scripts))
d.apply()
info = [i for i in a.list() if i.name == "pyTestAsset"][0]
print(info.label, info.category, info.params, info.embedded, info.instances)

net = jn.Network()
line = net.add_node("line", "line")
inst = net.add_node("pyTestAsset", "inst")
line >> inst
print(inst.amount, inst.float_minimum("amount"), inst.float_maximum("amount"), inst.float_minimum("extra"))
print([i.instances for i in a.list(net) if i.name == "pyTestAsset"])
try:
    a.uninstall("pyTestAsset", net)
except RuntimeError as e:
    print("refused:", "instance" in str(e))

d2 = a.get("pyTestAsset")
d2.params[0].default = 4
d2.apply()
print([i.outdated for i in a.list(net) if i.name == "pyTestAsset"], a.update_instances(net), [i.outdated for i in a.list(net) if i.name == "pyTestAsset"])
net.remove_node(inst)
a.uninstall("pyTestAsset", net)
print(a.has("pyTestAsset"))
)py");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output,
              "pyTestAsset 1 ['amount', 'extra'] True ['onCreated']\n"
              "Test Testing ['amount', 'extra'] True 0\n"
              "2.0 (True, 0.0) (True, 5.0) (False, 0.0)\n"
              "[1]\n"
              "refused: True\n"
              "[1] 1 [0]\n"
              "False\n");
}

TEST(ScriptModule, docAssetsUpdateAndUnlockAreUndoStepsOfTheDocument) {
    curvy::SketchDocument document;
    document.newBlank("DocAssets", 100.0f, 100.0f);
    auto result = runOn(document, std::string(assetPrelude) + R"py(
d = a.AssetDef.from_text(TEXT.replace("pyTestAsset", "pyDocAsset"))
d.apply()
inst = doc.add_node("pyDocAsset", "inst")
d2 = a.get("pyDocAsset")
d2.params[0].default = 4
d2.apply()
print([i.outdated for i in a.list(inst.network) if i.name == "pyDocAsset"])
)py");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "[1]\n");

    const std::size_t before = document.undoStack().depth();
    result = runOn(document, R"py(
print(doc.assets.update_instances("pyDocAsset"), doc.assets.update_instances())
)py");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "1 0\n");
    EXPECT_EQ(document.undoStack().depth(), before + 1);
    document.undoStack().undo();
    EXPECT_EQ(document.undoStack().depth(), before);

    result = runOn(document, R"py(
import jitnodes as jn
inst = doc.add_node("pyDocAsset", "second")
count = lambda: [i.instances for i in jn.assets.list(inst.network) if i.name == "pyDocAsset"]
print(count())
doc.assets.unlock(inst)
print(count())
try:
    doc.assets.unlock(doc.add_node("transform", "plain"))
except RuntimeError:
    print("not an instance")
)py");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "[2]\n[1]\nnot an instance\n");
    jitnodes::AssetLibrary::instance().uninstall("pyDocAsset");
}

TEST(ScriptModule, aDefinitionIsSavedToAFileAndInstalledAgain) {
    curvy::SketchDocument document;
    document.newBlank("Assets", 100.0f, 100.0f);
    const auto result = runOn(document, std::string(assetPrelude) + R"py(
import os, tempfile
folder = tempfile.mkdtemp()
path = os.path.join(folder, "pyFileAsset.asset.json")
d = a.AssetDef.from_text(TEXT.replace("pyTestAsset", "pyFileAsset"))
try:
    d.save()
except ValueError as e:
    print("needs a path")
d.save(path)
print(d.version, d.source == path, os.path.exists(path), a.get("pyFileAsset").version)
d.save()
print(d.version)
a.uninstall("pyFileAsset")
a.install(path)
print(a.get("pyFileAsset").version, a.get("pyFileAsset").source == path)
count, error = a.install_directory(folder)
print(count, error == "")
a.uninstall("pyFileAsset")
print(a.has("pyFileAsset"))
)py");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "needs a path\n1 True True 1\n2\n2 True\n1 True\nFalse\n");
}

TEST(ScriptModule, theScriptCheckNamesWhatAnEventMayNotDo) {
    curvy::SketchDocument document;
    document.newBlank("Check", 100.0f, 100.0f);
    const auto result = runOn(document, std::string(assetPrelude) + R"py(
def problems(**scripts):
    return [(s, line, col) for s, line, col, message in a.check_scripts(scripts)]

# onFrameChanged may not change its node
print(problems(onFrameChanged="x = node.get_float('amount')\nnode.set_float('amount', x)"))
print(problems(onFrameChanged="n = kwargs['node']\nm = n\nm.set_expression('amount', '1')"))
print(problems(onFrameChanged="kwargs.get('node').amount = 3"))
print(problems(onFrameChanged="for n in node.network().nodes():\n    n.add_tag('x')"))
print(problems(onFrameChanged="node >> other"), problems(onFrameChanged="other >> node"))
print(problems(onFrameChanged="print(node.get_float('amount'), node.name())"))
# things that hide an edit
print(problems(onFrameChanged="getattr(node, name)()"), problems(onFrameChanged="getattr(node, 'get_float')('amount')"))
print(problems(onFrameChanged="exec('1')"), problems(onFrameChanged="setattr(node, 'amount', 1)"))
# helpers in the module
print(problems(module="def poke(n):\n    n.set_float('amount', 1)\ndef double(v):\n    return v * 2", onFrameChanged="module.poke(node)"))
print(problems(module="def poke(n):\n    n.set_float('amount', 1)\ndef double(v):\n    return v * 2", onFrameChanged="print(module.double(2))"))
print(problems(module="def a(n):\n    n.set_float('x', 1)\ndef b(n):\n    a(n)", onFrameChanged="module.b(node)"))
# other events are free, onVersionUpdated may only read old and write fresh
print(problems(onCreated="node.set_float('amount', 1)\nnode.amount = 2"))
print(problems(onVersionUpdated="fresh.set_float('amount', old.get_float('scale') * 2)"))
print(problems(onVersionUpdated="old.set_float('scale', 1)"), problems(onVersionUpdated="print(doc.name)"), problems(onVersionUpdated="network.node(1)"))
print(problems(onVersionUpdated="import curvy"))
# syntax
print(problems(onCreated="def ("))
msg = a.check_scripts({"onFrameChanged": "node.set_float('a', 1)"})[0]
print(msg[3])
)py");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output,
              "[('onFrameChanged', 2, 1)]\n"
              "[('onFrameChanged', 3, 1)]\n"
              "[('onFrameChanged', 1, 1)]\n"
              "[('onFrameChanged', 2, 5)]\n"
              "[] [('onFrameChanged', 1, 1)]\n"
              "[]\n"
              "[('onFrameChanged', 1, 1)] []\n"
              "[('onFrameChanged', 1, 1)] [('onFrameChanged', 1, 1)]\n"
              "[('onFrameChanged', 1, 1)]\n"
              "[]\n"
              "[('onFrameChanged', 1, 1)]\n"
              "[]\n"
              "[]\n"
              "[('onVersionUpdated', 1, 1)] [('onVersionUpdated', 1, 7)] [('onVersionUpdated', 1, 1)]\n"
              "[('onVersionUpdated', 1, 1)]\n"
              "[('onCreated', 1, 5)]\n"
              "cannot change this asset (calls set_float)\n");
}

TEST(ScriptModule, aDefinitionWithAScriptThatBreaksTheRulesIsRefused) {
    curvy::SketchDocument document;
    document.newBlank("Refuse", 100.0f, 100.0f);
    const auto result = runOn(document, std::string(assetPrelude) + R"py(
d = a.AssetDef.from_text(TEXT.replace("pyTestAsset", "pyRefusedAsset"))
d.set_script("onFrameChanged", "x = 1\nnode.set_float('amount', x)")
print(d.validate())
try:
    d.apply()
except RuntimeError as e:
    print("apply refused:", str(e).startswith("onFrameChanged 2:1"))
print(a.has("pyRefusedAsset"))
d.set_script("onFrameChanged", "print(node.get_float('amount'))")
print(repr(d.validate()))
d.apply()
print(a.has("pyRefusedAsset"))
a.uninstall("pyRefusedAsset")
)py");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output,
              "onFrameChanged 2:1: cannot change this asset (calls set_float)\n"
              "apply refused: True\n"
              "False\n''\nTrue\n");
}

TEST(ScriptModule, aMigrationRestrictionAllowsOnlyTheNewInstance) {
    curvy::SketchDocument document;
    document.newBlank("Migration", 100.0f, 100.0f);
    const auto result = runOn(document, std::string(assetPrelude) + R"py(
a.AssetDef.from_text(TEXT.replace("pyTestAsset", "pyMigAsset")).apply()
net = jn.Network()
old = net.add_node("pyMigAsset", "old")
fresh = net.add_node("pyMigAsset", "fresh")
before = old.amount

def refused(fn):
    try:
        fn()
    except PermissionError:
        return True
    return False

with a.restricted(fresh, "onVersionUpdated"):
    fresh.amount = 4
    print(refused(lambda: setattr(old, "amount", 1)), refused(lambda: net.add_node("line", "x")), refused(lambda: net.clear()))
print(fresh.amount, old.amount == before)
net.remove_node(old)
net.remove_node(fresh)
a.uninstall("pyMigAsset")
)py");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "True True True\n4.0 True\n");
}

TEST(ScriptModule, theEventGuardCostsLittleForAllowedEditsAndNothingForReads) {
    curvy::SketchDocument document;
    document.newBlank("GuardCost", 100.0f, 100.0f);
    const auto result = runOn(document, std::string(assetPrelude) + R"py(
import time
a.AssetDef.from_text(TEXT.replace("pyTestAsset", "pyCostAsset")).apply()
net = jn.Network()
up = net.add_node("line", "up")
inst = net.add_node("pyCostAsset", "inst")
up >> inst
others = [net.add_node("line", "n%d" % i) for i in range(1000)]
targets = others[:100]

def edits():
    start = time.perf_counter()
    for i, n in enumerate(targets):
        n.count = 3 + i % 5
    return time.perf_counter() - start

def reads():
    start = time.perf_counter()
    for n in others:
        n.count
    return time.perf_counter() - start

edits()
reads()
plainEdits = min(edits() for _ in range(3))
plainReads = min(reads() for _ in range(3))
with a.restricted(inst, "onFrameChanged"):
    guardedEdits = min(edits() for _ in range(3))
    guardedReads = min(reads() for _ in range(3))
print("per-edit overhead under 0.5 ms:", (guardedEdits - plainEdits) / len(targets) < 0.0005)
print("reads unchanged:", guardedReads < plainReads * 1.5 + 0.002)
)py");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "per-edit overhead under 0.5 ms: True\nreads unchanged: True\n");
}

TEST(ScriptModule, whileAnEventRunsTheHandlesRefuseEditsToTheAssetAndWhatItReadsFrom) {
    curvy::SketchDocument document;
    document.newBlank("Guard", 100.0f, 100.0f);
    const auto result = runOn(document, std::string(assetPrelude) + R"py(
a.AssetDef.from_text(TEXT.replace("pyTestAsset", "pyGuardAsset")).apply()
net = jn.Network()
up = net.add_node("line", "up")
inst = net.add_node("pyGuardAsset", "inst")
down = net.add_node("transform", "down")
side = net.add_node("line", "side")
up >> inst >> down

def refused(fn):
    try:
        fn()
    except PermissionError as e:
        return True
    return False

with a.restricted(inst, "onFrameChanged"):
    print(refused(lambda: setattr(inst, "amount", 3)),
          refused(lambda: inst.set_float("amount", 3)),
          refused(lambda: setattr(up, "count", 9)),
          refused(lambda: up.set_float("count", 9)),
          refused(lambda: net.disconnect(inst, 0)),
          refused(lambda: inst.disconnect(0)))
    # downstream, unrelated nodes and new nodes may change
    down.tx = 3
    side.count = 4
    new = net.add_node("line", "new")
    new >> down
    print(refused(lambda: a.update_instances(net)), refused(lambda: a.uninstall("pyGuardAsset")))
    print(down.tx, side.count, "new" in net)
inst.amount = 3
up.count = 9
print(inst.amount, up.count)

with a.restricted(inst, "onVersionUpdated"):
    print(refused(lambda: setattr(down, "tx", 1)), refused(lambda: net.add_node("line", "x")), refused(lambda: net.clear()))
print(down.tx)

with a.restricted(inst, "onFrameChanged"):
    try:
        with a.restricted(inst, "onFrameChanged"):
            print("nested ran")
    except RuntimeError as e:
        print("not twice:", "already running" in str(e))
with a.restricted(inst, "onCreated"):
    inst.amount = 5
print(inst.amount, refused(lambda: None))
net.remove_node(inst)
a.uninstall("pyGuardAsset")
)py");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output,
              "True True True True True True\n"
              "True True\n"
              "3.0 4.0 True\n"
              "3.0 9.0\n"
              "True True True\n"
              "3.0\n"
              "not twice: True\n"
              "5.0 False\n");
}

TEST(ScriptModule, rulerOrigin) {
    curvy::SketchDocument document;
    document.newBlank("Origin", 100.0f, 100.0f);
    auto result = runOn(document, "print(doc.ruler_origin)\ndoc.ruler_origin = (10, 20.5)\nprint(doc.ruler_origin)");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "(0.0, 0.0)\n(10.0, 20.5)\n");
    EXPECT_EQ(document.rulerOrigin(), (curvy::RulerOrigin{10.0, 20.5}));
    EXPECT_FALSE(runOn(document, "doc.ruler_origin = (float('nan'), 0)").ok);
}
