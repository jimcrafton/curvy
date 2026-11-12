#include "curvy/graphview.h"

#include "curvy/geometrytable.h"
#include "curvy/graphcrumbs.h"
#include "curvy/graphpanel.h"
#include "curvy/objectnode.h"
#include "curvy/sketchdocument.h"
#include "testsupport.h"

#include <newui/mouse_constants.h>

#include <gtest/gtest.h>

TEST(GraphViewTest, entersASubnetAndGoesBackUp) {
    curvy::SketchDocument document;
    document.buildSampleLayers();
    curvy::GraphView view;
    view.setDocument(&document);
    EXPECT_EQ(view.path(), "root");

    const curvy::NodeRef layer = document.childrenOf()[0];
    ASSERT_TRUE(view.enter(layer));
    EXPECT_EQ(view.path(), "root > Layer1");
    EXPECT_NE(view.scope(), 0u);

    const curvy::NodeRef group = document.childrenOf(layer)[0];
    ASSERT_TRUE(view.enter(group));
    EXPECT_EQ(view.path(), "root > Layer1 > Group1");

    EXPECT_TRUE(view.exit());
    EXPECT_EQ(view.path(), "root > Layer1");
    EXPECT_EQ(view.selected(), group);
    EXPECT_TRUE(view.exit());
    EXPECT_FALSE(view.exit());
}

TEST(GraphViewTest, goesStraightUpToAnAncestorLevel) {
    curvy::SketchDocument document;
    document.buildSampleLayers();
    curvy::GraphView view;
    view.setDocument(&document);
    const curvy::NodeRef layer = document.childrenOf()[0];
    ASSERT_TRUE(view.enter(layer));
    ASSERT_TRUE(view.enter(document.childrenOf(layer)[0]));
    EXPECT_EQ(view.levelNames(), (std::vector<std::string>{"root", "Layer1", "Group1"}));

    EXPECT_TRUE(view.goToLevel(0));
    EXPECT_EQ(view.path(), "root");
    EXPECT_FALSE(view.goToLevel(0));
    EXPECT_FALSE(view.goToLevel(2));
}

TEST(GraphViewTest, refusesToEnterANodeThatIsNotASubnet) {
    curvy::SketchDocument document;
    document.buildSampleLayers();
    curvy::GraphView view;
    view.setDocument(&document);
    const curvy::NodeRef group = document.childrenOf(document.childrenOf()[0])[0];
    ASSERT_TRUE(view.enter(group));
    const curvy::NodeRef fill(view.scope(), document.networkOf(view.scope()).nodeIds().front());
    EXPECT_FALSE(view.enter(fill));
}

TEST(GraphViewTest, describesTheSelectedNode) {
    curvy::SketchDocument document;
    document.buildSampleLayers();
    curvy::GraphView view;
    view.setDocument(&document);
    EXPECT_EQ(view.describeSelected(), "");
    view.select(document.childrenOf()[1]);
    EXPECT_EQ(view.describeSelected(), "Layer2 (curvyObject) #layer");
}

class GraphOverlayTest : public curvytest::WorkspaceTest {};

TEST_F(GraphOverlayTest, theGraphViewReplacesTheCanvasUntilToggledBack) {
    window().toggleGraph();
    EXPECT_FALSE(window().graphShown());   // nothing open yet

    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    EXPECT_TRUE(window().graphShown());
    EXPECT_EQ(window().graph().path(), "root");
    const newui::Rect canvas = window().canvas().getClientBounds();
    EXPECT_NEAR(window().graph().getClientBounds().width() + 260.0f, canvas.width(), 1.0f);

    window().toggleGraph();
    EXPECT_FALSE(window().graphShown());
}

TEST_F(GraphOverlayTest, theDataPopupShowsTheSelectedNodesPointsPrimitivesAndInputs) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();

    window().openPointTable();   // nothing selected
    EXPECT_FALSE(window().pointTable().shown());

    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef group = document.childrenOf(document.childrenOf()[0])[0];
    ASSERT_TRUE(window().graph().enter(group));
    curvy::NodeRef fill;
    for (const jitnodes::NodeId id : document.networkOf(window().graph().scope()).nodeIds()) {
        if (document.networkOf(window().graph().scope()).node(id).typeName() == "curvyFill") {
            fill = curvy::NodeRef(window().graph().scope(), id);
        }
    }
    ASSERT_TRUE(fill.valid());
    window().graph().select(fill);
    window().openPointTable();
    ASSERT_TRUE(window().pointTable().shown());
    ASSERT_NE(window().pointTable().model(), nullptr);
    EXPECT_EQ(window().pointTable().model()->rowCount(), 96u);
    EXPECT_EQ(window().pointTable().sourceCount(), 2u);

    window().pointTable().setKind(curvy::GeometryTableModel::Kind::Primitives);
    EXPECT_EQ(window().pointTable().model()->rowCount(), 1u);
    EXPECT_EQ(window().pointTable().model()->columnTitle(4), "fill.a");   // after the structural columns, the fill the node stamped

    window().pointTable().setSource(1);   // the circle that feeds the fill: no look yet
    EXPECT_EQ(window().pointTable().model()->columnCount(), 4u);

    const std::string copied = window().pointTable().copySelection();
    EXPECT_NE(copied.find("start\tcount"), std::string::npos);
    window().toggleGraph();
    EXPECT_FALSE(window().pointTable().shown());
}

TEST_F(GraphOverlayTest, theNodeRingRunsItsCommandsOnTheSelectedNode) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef layer = document.childrenOf()[0];

    EXPECT_TRUE(window().runGraphCommandFor("graph.bypass"));   // nothing selected: only a hint
    EXPECT_FALSE(document.node(layer).bypassed());

    window().graph().select(layer);
    EXPECT_EQ(window().graphPanel().title(), "Layer1");
    EXPECT_EQ(window().graphPanel().kind(), "curvyObject");

    window().runGraphCommandFor("graph.bypass");
    EXPECT_TRUE(document.node(layer).bypassed());
    window().runGraphCommandFor("graph.bypass");
    EXPECT_FALSE(document.node(layer).bypassed());

    window().runGraphCommandFor("graph.display");
    EXPECT_EQ(document.network().displayNode(), layer.id);

    window().runGraphCommandFor("graph.enter");
    EXPECT_EQ(window().graph().path(), "root > Layer1");
    window().runGraphCommandFor("graph.exit");
    EXPECT_EQ(window().graph().path(), "root");
    EXPECT_FALSE(window().runGraphCommandFor("file.open"));
}

TEST_F(GraphOverlayTest, theBreadcrumbLinksGoBackUpToThatLevel) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::GraphCrumbs& crumbs = window().graphCrumbs();
    EXPECT_EQ(crumbs.linkCount(), 0u);

    const curvy::NodeRef layer = window().document().childrenOf()[0];
    ASSERT_TRUE(window().graph().enter(layer));
    ASSERT_TRUE(window().graph().enter(window().document().childrenOf(layer)[0]));
    ASSERT_EQ(crumbs.linkCount(), 2u);
    EXPECT_EQ(crumbs.link(0)->text(), "root");
    EXPECT_EQ(crumbs.link(1)->text(), "Layer1");
    EXPECT_TRUE(crumbs.link(1)->isHotLink());

    crumbs.link(1)->onLinkClicked(*crumbs.link(1));
    EXPECT_EQ(window().graph().path(), "root > Layer1");
    EXPECT_EQ(crumbs.linkCount(), 1u);
    crumbs.link(0)->onLinkClicked(*crumbs.link(0));
    EXPECT_EQ(window().graph().path(), "root");
    EXPECT_EQ(crumbs.linkCount(), 0u);
}

TEST_F(GraphOverlayTest, thePanelRenamesANodeAndEditsItsNumbersAsUndoSteps) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef layer = document.childrenOf()[0];
    window().graph().select(layer);

    window().graphPanel().rename("Backdrop");
    EXPECT_EQ(document.node(layer).name(), "Backdrop");
    EXPECT_EQ(window().graphPanel().title(), "Backdrop");
    document.undoStack().undo();
    EXPECT_EQ(document.node(layer).name(), "Layer1");

    window().graph().select(layer);
    ASSERT_TRUE(window().graphPanel().paramShown(0));
    const std::string caption = window().graphPanel().paramCaption(0);
    window().graphPanel().setParam(0, "1");
    EXPECT_EQ(document.node(layer).getFloat(caption), 1.0f);
    EXPECT_EQ(window().graphPanel().paramValue(0), "1");
}

TEST_F(GraphOverlayTest, undoAndRedoBringThePanelBackToTheDocument) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef layer = document.childrenOf()[0];
    window().graph().select(layer);
    ASSERT_TRUE(window().graphPanel().paramShown(0));
    const std::string caption = window().graphPanel().paramCaption(0);
    const std::string before = window().graphPanel().paramValue(0);

    window().graphPanel().setParam(0, "7");
    ASSERT_EQ(window().graphPanel().paramValue(0), "7");
    EXPECT_TRUE(window().undo());
    EXPECT_EQ(window().graphPanel().paramValue(0), before);
    EXPECT_NE(document.node(layer).getFloat(caption), 7.0f);
    EXPECT_TRUE(window().redo());
    EXPECT_EQ(window().graphPanel().paramValue(0), "7");

    window().graphPanel().rename("Backdrop");
    EXPECT_TRUE(window().undo());
    EXPECT_EQ(window().graphPanel().name(), "Layer1");
}

TEST_F(GraphOverlayTest, groupingFromTheGraphWrapsTheSelectedShape) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef group = document.childrenOf(document.childrenOf()[0])[0];
    ASSERT_TRUE(window().graph().enter(group));
    curvy::NodeRef fill;
    for (const jitnodes::NodeId id : document.networkOf(window().graph().scope()).nodeIds()) {
        if (document.networkOf(window().graph().scope()).node(id).typeName() == "curvyFill") {
            fill = curvy::NodeRef(window().graph().scope(), id);
        }
    }
    window().graph().select(fill);
    window().runGraphCommandFor("graph.group");
    EXPECT_TRUE(document.node(window().graph().selected()).hasTag(curvy::groupTag));
    window().runGraphCommandFor("graph.ungroup");
    EXPECT_EQ(document.node(window().graph().selected()).typeName(), "curvyFill");
}

TEST_F(GraphOverlayTest, thePanelShowsEveryParameterOfATransformAndFoldsAFillsRgbIntoOneColour) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef group = document.childrenOf(document.childrenOf()[0])[0];
    ASSERT_TRUE(window().graph().enter(group));
    const jitnodes::Network& network = document.networkOf(window().graph().scope());
    curvy::NodeRef transform, fill;
    for (const jitnodes::NodeId id : network.nodeIds()) {
        const std::string type = network.node(id).typeName();
        if (type == "transform") {
            transform = curvy::NodeRef(window().graph().scope(), id);
        } else if (type == "curvyFill") {
            fill = curvy::NodeRef(window().graph().scope(), id);
        }
    }

    window().graph().select(transform);
    curvy::GraphPanel& panel = window().graphPanel();
    ASSERT_EQ(panel.paramCount(), 8u);
    bool hasTx = false, hasTy = false;
    for (std::size_t row = 0; row < panel.paramCount(); ++row) {
        hasTx = hasTx || panel.paramCaption(row) == "tx";
        hasTy = hasTy || panel.paramCaption(row) == "ty";
        EXPECT_TRUE(panel.paramShown(row));
    }
    EXPECT_TRUE(hasTx && hasTy);

    window().graph().select(fill);
    ASSERT_EQ(panel.paramCaption(0), "color");
    EXPECT_EQ(panel.paramCount(), document.node(fill).floatNames().size() - 3);   // r, g, b and alpha fold into the one colour row
    for (std::size_t row = 0; row < panel.paramCount(); ++row) {
        for (const char* folded : {"r", "g", "b", "alpha"}) {
            EXPECT_NE(panel.paramCaption(row), folded);
        }
    }
    const float originalGreen = document.node(fill).getFloat("g");
    const float originalAlpha = document.node(fill).getFloat("alpha");
    panel.setParam(0, "#FF8000");
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("r"), 1.0f);
    EXPECT_NEAR(document.node(fill).getFloat("g"), 128.0f / 255.0f, 1e-5);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("b"), 0.0f);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("alpha"), originalAlpha);   // six digits leave the alpha alone
    EXPECT_EQ(panel.paramValue(0), curvy::GraphPanel::colorText(1.0f, 128.0f / 255.0f, 0.0f, originalAlpha));
    panel.setParam(0, "#FF800080");
    EXPECT_NEAR(document.node(fill).getFloat("alpha"), 128.0f / 255.0f, 1e-5);
    document.undoStack().undo();
    document.undoStack().undo();
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("g"), originalGreen);
}

TEST_F(GraphOverlayTest, parameterTagsSayHowToGroupAndScrubThem) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef group = document.childrenOf(document.childrenOf()[0])[0];
    const jitnodes::Network& network = document.networkOf(document.scopeOfSubnet(group));
    for (const jitnodes::NodeId id : network.nodeIds()) {
        const jitnodes::Node& node = network.node(id);
        if (node.typeName() == "curvyFill") {
            EXPECT_EQ(node.paramTagValue("r", "group"), "color");
            EXPECT_EQ(node.paramTagValue("alpha", "channel"), "a");
            EXPECT_TRUE(node.hasParamTag("opacity", "unit:0.01"));
            EXPECT_FALSE(node.paramTagValue("evenOdd", "group").has_value());
        } else if (node.typeName() == "transform") {
            EXPECT_EQ(node.paramTagValue("sx", "unit"), "0.01");
            EXPECT_FALSE(node.paramTagValue("tx", "unit").has_value());
        }
    }
}

TEST_F(GraphOverlayTest, minAndMaxTagsKeepATypedNumberInRange) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef group = document.childrenOf(document.childrenOf()[0])[0];
    ASSERT_TRUE(window().graph().enter(group));
    const jitnodes::Network& network = document.networkOf(window().graph().scope());
    curvy::NodeRef fill;
    for (const jitnodes::NodeId id : network.nodeIds()) {
        if (network.node(id).typeName() == "curvyFill") {
            fill = curvy::NodeRef(window().graph().scope(), id);
        }
    }
    window().graph().select(fill);
    curvy::GraphPanel& panel = window().graphPanel();
    std::string refused;
    panel.onRefused = [&refused](const std::string& message) { refused = message; };
    std::size_t opacity = panel.paramCount();
    for (std::size_t row = 0; row < panel.paramCount(); ++row) {
        if (panel.paramCaption(row) == "opacity") {
            opacity = row;
        }
    }
    ASSERT_LT(opacity, panel.paramCount());

    panel.setParam(opacity, "1.04");
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("opacity"), 1.0f);
    EXPECT_EQ(refused, "opacity is limited to 0 to 1");
    panel.setParam(opacity, "-3");
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("opacity"), 0.0f);
    panel.setParam(opacity, "0.25");
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("opacity"), 0.25f);
}

TEST_F(GraphOverlayTest, theColourSwatchOpensAWheelThatEditsTheColourAsOneUndoStep) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef group = document.childrenOf(document.childrenOf()[0])[0];
    ASSERT_TRUE(window().graph().enter(group));
    const jitnodes::Network& network = document.networkOf(window().graph().scope());
    curvy::NodeRef fill, other;
    for (const jitnodes::NodeId id : network.nodeIds()) {
        (network.node(id).typeName() == "curvyFill" ? fill : other) = curvy::NodeRef(window().graph().scope(), id);
    }
    window().graph().select(fill);
    curvy::GraphPanel& panel = window().graphPanel();
    ASSERT_EQ(panel.paramCaption(0), "color");
    const float originalR = document.node(fill).getFloat("r"), originalG = document.node(fill).getFloat("g"), originalB = document.node(fill).getFloat("b");

    EXPECT_FALSE(panel.colorPickerOpen());
    panel.toggleColorPicker(0);
    ASSERT_TRUE(panel.colorPickerOpen());
    curvy::ColorPickerCard& card = *panel.colorPicker();
    EXPECT_TRUE(card.hasAlpha());     // the fill has an alpha, so the card shows an alpha slider
    EXPECT_FALSE(card.hexVisible());  // the panel's colour row already has the hex
    curvy::ColorWheel& wheel = card.wheel();
    wheel.onDragStarted(wheel);
    wheel.setHsb(0.0, 1.0, 1.0);
    wheel.onChanged(wheel);
    wheel.onDragFinished(wheel);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("r"), 1.0f);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("g"), 0.0f);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("b"), 0.0f);
    EXPECT_EQ(panel.paramValue(0).substr(0, 7), "#FF0000");

    document.undoStack().undo();
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("r"), originalR);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("g"), originalG);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("b"), originalB);

    // The alpha slider and the wheel edit the same colour, each gesture one undo step. (This undoes through the document's stack, which does not
    // refresh the panel; the Ctrl+Z path does, see GraphEditTest.)
    panel.show(fill);
    const float originalAlpha = document.node(fill).getFloat("alpha");
    card.alphaSlider().setValue(50.0f);
    EXPECT_NEAR(document.node(fill).getFloat("alpha"), 0.5f, 1e-5);
    card.alphaSlider().onMouseUp(card.alphaSlider(), newui::Point(0.0f, 0.0f), newui::mbmLeftButton, 0);   // letting go of the slider ends the gesture
    wheel.onDragStarted(wheel);
    wheel.setHsb(2.0 / 3.0, 1.0, 1.0);
    wheel.onChanged(wheel);
    wheel.onDragFinished(wheel);
    EXPECT_NEAR(document.node(fill).getFloat("b"), 1.0f, 1e-5);
    EXPECT_NEAR(document.node(fill).getFloat("alpha"), 0.5f, 1e-5);   // the wheel leaves the alpha alone
    document.undoStack().undo();
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("b"), originalB);
    document.undoStack().undo();
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("alpha"), originalAlpha);

    panel.toggleColorPicker(0);
    EXPECT_FALSE(panel.colorPickerOpen());
    panel.toggleColorPicker(0);
    ASSERT_TRUE(panel.colorPickerOpen());
    window().graph().select(other);
    EXPECT_FALSE(panel.colorPickerOpen());
}

TEST_F(GraphOverlayTest, usingALinkedSwatchTiesTheColourToItAndAHandEditUntiesIt) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef group = document.childrenOf(document.childrenOf()[0])[0];
    ASSERT_TRUE(window().graph().enter(group));
    const jitnodes::Network& network = document.networkOf(window().graph().scope());
    curvy::NodeRef fill;
    for (const jitnodes::NodeId id : network.nodeIds()) {
        if (network.node(id).typeName() == "curvyFill") {
            fill = curvy::NodeRef(window().graph().scope(), id);
        }
    }
    window().graph().select(fill);
    curvy::GraphPanel& panel = window().graphPanel();
    document.setSwatchLinked(0, 5, true);
    const std::string tag = curvy::SwatchLinks::tag(document.palettes()[0].swatches[5].link, "color");

    panel.toggleColorPicker(0);
    ASSERT_TRUE(panel.colorPickerOpen());
    panel.colorPicker()->swatchStrip().onUse(5);
    EXPECT_TRUE(document.node(fill).hasTag(tag));
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("r"), document.palettes()[0].swatches[5].r);

    document.undoStack().undo();   // the colour and its tie are one step
    EXPECT_FALSE(document.node(fill).hasTag(tag));
    document.undoStack().redo();
    EXPECT_TRUE(document.node(fill).hasTag(tag));

    document.replaceSwatch(0, 5, curvy::Swatch{0.2f, 0.4f, 0.6f, 1.0f, ""});   // the swatch follows to the shape and the panel
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("b"), 0.6f);

    panel.setParam(0, "#112233");
    EXPECT_FALSE(document.node(fill).hasTag(tag));
    document.undoStack().undo();
    EXPECT_TRUE(document.node(fill).hasTag(tag));

    panel.colorPicker()->swatchStrip().onUse(0);   // a plain swatch: no tie
    EXPECT_FALSE(document.node(fill).hasTag(tag));
}

TEST_F(GraphOverlayTest, keysOneToEightApplyTheRecentColoursWhileTheColourCardIsOpen) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef group = document.childrenOf(document.childrenOf()[0])[0];
    ASSERT_TRUE(window().graph().enter(group));
    const jitnodes::Network& network = document.networkOf(window().graph().scope());
    curvy::NodeRef fill;
    for (const jitnodes::NodeId id : network.nodeIds()) {
        if (network.node(id).typeName() == "curvyFill") {
            fill = curvy::NodeRef(window().graph().scope(), id);
        }
    }
    window().graph().select(fill);
    curvy::GraphPanel& panel = window().graphPanel();
    const HWND target = frame_.rootView().windowHandle();
    frame_.rootView().setFocusedSubView(&window().graph());

    ::SendMessage(target, WM_KEYDOWN, '1', 0);   // the card is closed: the key does nothing
    EXPECT_TRUE(document.recentColors().empty());

    panel.toggleColorPicker(0);
    ASSERT_TRUE(panel.colorPickerOpen());
    panel.colorPicker()->swatchStrip().onUse(5);
    panel.colorPicker()->swatchStrip().onUse(3);
    const curvy::Swatch newest = document.recentColors()[0], older = document.recentColors()[1];
    ASSERT_NE(newest.r, older.r);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("r"), newest.r);

    ::SendMessage(target, WM_KEYDOWN, '2', 0);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("r"), older.r);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("g"), older.g);
    EXPECT_EQ(document.recentColors()[0].r, older.r);   // applying it makes it the newest again

    EXPECT_FALSE(panel.useRecentColor(5));   // only two recent colours
    document.undoStack().undo();            // the key was one undo step
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("r"), newest.r);
}

TEST_F(GraphOverlayTest, theColourCardShowsThePalettesAndASwatchClickEditsTheColourAsOneUndoStepAndFillsRecent) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    curvy::SketchDocument& document = window().document();
    const curvy::NodeRef group = document.childrenOf(document.childrenOf()[0])[0];
    ASSERT_TRUE(window().graph().enter(group));
    const jitnodes::Network& network = document.networkOf(window().graph().scope());
    curvy::NodeRef fill;
    for (const jitnodes::NodeId id : network.nodeIds()) {
        if (network.node(id).typeName() == "curvyFill") {
            fill = curvy::NodeRef(window().graph().scope(), id);
        }
    }
    window().graph().select(fill);
    curvy::GraphPanel& panel = window().graphPanel();
    const float originalR = document.node(fill).getFloat("r"), originalG = document.node(fill).getFloat("g"), originalB = document.node(fill).getFloat("b");

    panel.toggleColorPicker(0);
    ASSERT_TRUE(panel.colorPickerOpen());
    curvy::ColorPickerCard& card = *panel.colorPicker();
    EXPECT_TRUE(card.swatchesVisible());
    curvy::SwatchStrip& strip = card.swatchStrip();
    ASSERT_EQ(strip.palettes().size(), 2u);   // Recent and the document's "Swatches"
    EXPECT_EQ(strip.tiles().swatches().size(), 13u);
    EXPECT_TRUE(document.recentColors().empty());

    strip.onUse(5);   // "Red"
    const curvy::Swatch red = document.palettes()[0].swatches[5];
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("r"), red.r);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("g"), red.g);
    ASSERT_EQ(document.recentColors().size(), 1u);   // what was applied is now recent
    EXPECT_FLOAT_EQ(document.recentColors()[0].r, red.r);

    document.undoStack().undo();   // the whole click was one step
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("r"), originalR);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("g"), originalG);
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("b"), originalB);

    // The panel's own edits and a palette edit stay separate undo steps.
    document.undoStack().redo();
    EXPECT_FLOAT_EQ(document.node(fill).getFloat("r"), red.r);
    panel.swatches()->setCurrent(curvy::Swatch{0.3f, 0.3f, 0.3f, 1.0f, ""});
    strip.onAdd();
    EXPECT_EQ(document.palettes()[0].swatches.size(), 14u);
    EXPECT_EQ(strip.tiles().swatches().size(), 14u);
    EXPECT_TRUE(card.bounds().height() > 0.0f);
}

TEST_F(GraphOverlayTest, theColourCardHasRgbHsbCmykAndGraySlidersOverOneColour) {
    curvy::ColorPickerCard card;
    int changes = 0;
    card.onChanged += [&changes](curvy::ColorPickerCard&) {
        ++changes;
        return newui::SyncReturn::Handled;
    };
    card.setColor(1.0, 0.5, 0.0, 0.25, true);
    EXPECT_EQ(card.sliderCount(), 3u);
    EXPECT_FLOAT_EQ(card.slider(0).value(), 255.0f);
    EXPECT_NEAR(card.slider(1).value(), 127.5f, 1.0f);
    EXPECT_FLOAT_EQ(card.alphaSlider().value(), 25.0f);
    EXPECT_EQ(changes, 0);   // showing a colour announces nothing

    card.setMode(curvy::ColorPickerCard::Mode::Hsb);
    EXPECT_NEAR(card.slider(0).value(), 30.0f, 0.5f);   // orange
    EXPECT_FLOAT_EQ(card.slider(1).value(), 100.0f);
    EXPECT_FLOAT_EQ(card.slider(2).value(), 100.0f);

    card.setMode(curvy::ColorPickerCard::Mode::Cmyk);
    EXPECT_EQ(card.sliderCount(), 4u);
    EXPECT_NEAR(card.slider(1).value(), 50.0f, 0.5f);
    card.slider(3).setValue(50.0f);   // half black darkens it
    EXPECT_NEAR(card.red(), 0.5, 1e-3);
    EXPECT_GT(changes, 0);

    card.setMode(curvy::ColorPickerCard::Mode::Gray);
    EXPECT_EQ(card.sliderCount(), 1u);
    card.slider(0).setValue(40.0f);
    EXPECT_NEAR(card.red(), 0.4, 1e-3);
    EXPECT_NEAR(card.green(), 0.4, 1e-3);
    EXPECT_NEAR(card.blue(), 0.4, 1e-3);

    card.setColor(0.2, 0.4, 0.6, 1.0, false);   // no alpha on this colour: no alpha slider
    EXPECT_FALSE(card.hasAlpha());
    EXPECT_DOUBLE_EQ(card.alpha(), 1.0);
}

TEST_F(GraphOverlayTest, theColourCardsHexFieldCanBeHiddenAndStillWorks) {
    curvy::ColorPickerCard card;
    EXPECT_TRUE(card.hexVisible());   // shown unless the owner hides it
    card.setColor(1.0, 0.5, 0.0, 0.25, true);
    EXPECT_EQ(card.hexText(), "#FF800040");
    const float withHex = card.bounds().height();

    card.setHexVisible(false);
    EXPECT_FALSE(card.hexVisible());
    EXPECT_LT(card.bounds().height(), withHex);   // the card is shorter without the field
    EXPECT_TRUE(card.setHex("#0080FF"));          // still usable from code
    EXPECT_NEAR(card.green(), 128.0 / 255.0, 1e-3);
    EXPECT_NEAR(card.alpha(), 0.25, 1e-3);        // six digits leave the alpha alone
    EXPECT_FALSE(card.setHex("#12"));
    EXPECT_EQ(card.hexText(), "#0080FF40");

    card.setHexVisible(true);
    EXPECT_FLOAT_EQ(card.bounds().height(), withHex);
}

TEST(ColorWheelTest, hsbAndRgbConvertBothWays) {
    for (const double hue : {0.0, 0.1, 0.33, 0.5, 0.77, 0.95}) {
        double r = 0.0, g = 0.0, b = 0.0, h = 0.0, s = 0.0, v = 0.0;
        bool defined = false;
        curvy::ColorWheel::hsbToRgb(hue, 0.6, 0.8, r, g, b);
        curvy::ColorWheel::rgbToHsb(r, g, b, h, s, v, defined);
        EXPECT_TRUE(defined);
        EXPECT_NEAR(h, hue, 1e-9);
        EXPECT_NEAR(s, 0.6, 1e-9);
        EXPECT_NEAR(v, 0.8, 1e-9);
    }
    double h = 0.0, s = 0.0, v = 0.0;
    bool defined = true;
    curvy::ColorWheel::rgbToHsb(0.5, 0.5, 0.5, h, s, v, defined);
    EXPECT_FALSE(defined);
}

TEST(GraphPanelColor, convertsHexAndRejectsJunk) {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    bool hasAlpha = true;
    EXPECT_TRUE(curvy::GraphPanel::parseColor("e64d33", r, g, b, a, hasAlpha));
    EXPECT_FALSE(hasAlpha);
    EXPECT_EQ(curvy::GraphPanel::colorText(r, g, b), "#E64D33");
    EXPECT_TRUE(curvy::GraphPanel::parseColor("#E64D3380", r, g, b, a, hasAlpha));
    EXPECT_TRUE(hasAlpha);
    EXPECT_EQ(curvy::GraphPanel::colorText(r, g, b, a), "#E64D3380");
    EXPECT_FALSE(curvy::GraphPanel::parseColor("#12345", r, g, b, a, hasAlpha));
    EXPECT_FALSE(curvy::GraphPanel::parseColor("#GG0000", r, g, b, a, hasAlpha));
}

TEST_F(GraphOverlayTest, theDataWindowShowsAVerticalScrollBarForManyRowsAndKeepsItWhenResized) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    window().graph().select(window().document().childrenOf()[0]);
    window().openPointTable();
    ASSERT_TRUE(window().pointTable().shown());
    ASSERT_GT(window().pointTable().model()->rowCount(), 100u);

    newui::View* root = window().pointTable().root();
    auto* scroll = dynamic_cast<newui::ScrollView*>(root->findView("pointTableScroll"));
    ASSERT_NE(scroll, nullptr);
    EXPECT_TRUE(scroll->vBar()->isVisible());

    const newui::Rect big(0.0f, 0.0f, 900.0f, 700.0f);
    root->setBounds(big);
    root->findView("pointTableBox")->setBounds(big);
    root->updateLayout();
    EXPECT_TRUE(scroll->vBar()->isVisible());
    EXPECT_GT(scroll->contentSize().height, scroll->getClientBounds().height());
}

TEST_F(GraphOverlayTest, draggingANodeMovesOnlyThatNodeAndIsOneUndoStep) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    frame_.rootView().updateLayout();
    curvy::GraphView& graph = window().graph();
    graph.fit();
    curvy::SketchDocument& document = window().document();
    const std::vector<BLRect> before = graph.nodeRects();
    ASSERT_GT(before.size(), 1u);

    const BLRect seen = graph.visibleRect();
    const double zoom = graph.zoom();
    const auto rootPoint = [&](double x, double y) {
        const newui::Point local(float((x - seen.x) * zoom), float((y - seen.y) * zoom));
        const newui::Point inRoot = graph.mapTo(frame_.rootView(), local);
        return MAKELPARAM(int(inRoot.x), int(inRoot.y));
    };
    const HWND target = frame_.rootView().windowHandle();
    const BLRect& first = before[0];
    const double grabX = first.x + first.w * 0.5, grabY = first.y + first.h * 0.5;
    const double dragX = 60.0 / zoom, dragY = 30.0 / zoom;

    ::SendMessage(target, WM_LBUTTONDOWN, MK_LBUTTON, rootPoint(grabX, grabY));
    ::SendMessage(target, WM_MOUSEMOVE, MK_LBUTTON, rootPoint(grabX + dragX * 0.5, grabY + dragY * 0.5));
    ::SendMessage(target, WM_MOUSEMOVE, MK_LBUTTON, rootPoint(grabX + dragX, grabY + dragY));
    ::SendMessage(target, WM_LBUTTONUP, 0, rootPoint(grabX + dragX, grabY + dragY));

    const std::vector<BLRect> after = graph.nodeRects();
    ASSERT_EQ(after.size(), before.size());
    EXPECT_NEAR(after[0].x - before[0].x, dragX, 1.5);
    EXPECT_NEAR(after[0].y - before[0].y, dragY, 1.5);
    for (std::size_t i = 1; i < before.size(); ++i) {
        EXPECT_EQ(after[i].x, before[i].x);
        EXPECT_EQ(after[i].y, before[i].y);
    }

    EXPECT_EQ(document.undoStack().undoDescription(), "Move Node");
    window().undo();
    const std::vector<BLRect> undone = graph.nodeRects();
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(undone[i].x, before[i].x);
        EXPECT_EQ(undone[i].y, before[i].y);
    }
    window().redo();
    EXPECT_NEAR(graph.nodeRects()[0].x - before[0].x, dragX, 1.5);
}

TEST_F(GraphOverlayTest, aClickOnANodeSelectsItWithoutMovingOrRecordingAnything) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    frame_.rootView().updateLayout();
    curvy::GraphView& graph = window().graph();
    graph.fit();
    curvy::SketchDocument& document = window().document();
    const std::string lastStep = document.undoStack().undoDescription();
    const std::vector<BLRect> before = graph.nodeRects();
    const BLRect seen = graph.visibleRect();
    const double zoom = graph.zoom();
    const newui::Point local(float((before[0].x + before[0].w * 0.5 - seen.x) * zoom), float((before[0].y + before[0].h * 0.5 - seen.y) * zoom));
    const newui::Point inRoot = graph.mapTo(frame_.rootView(), local);
    const LPARAM at = MAKELPARAM(int(inRoot.x), int(inRoot.y));
    const HWND target = frame_.rootView().windowHandle();
    ::SendMessage(target, WM_LBUTTONDOWN, MK_LBUTTON, at);
    ::SendMessage(target, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(int(inRoot.x) + 2, int(inRoot.y)));
    ::SendMessage(target, WM_LBUTTONUP, 0, at);

    EXPECT_EQ(document.undoStack().undoDescription(), lastStep);
    const std::vector<BLRect> after = graph.nodeRects();
    EXPECT_EQ(after[0].x, before[0].x);
}

TEST_F(GraphOverlayTest, aRealDoubleClickOnALayerBoxEntersIt) {
    window().newDocument("Graph", 612.0f, 792.0f);
    window().document().buildSampleLayers();
    frame_.rootView().updateLayout();
    window().toggleGraph();
    frame_.rootView().updateLayout();
    curvy::GraphView& graph = window().graph();
    graph.fit();
    ASSERT_EQ(graph.scope(), 0u);

    // The first box that is a layer: find it by selecting each in turn is overkill; the layers merge's inputs are the layers, and boxes are
    // laid out in id order, so try the centre of every box until a double-click enters something.
    const std::vector<BLRect> rects = graph.nodeRects();
    const BLRect seen = graph.visibleRect();
    const double zoom = double(graph.getClientBounds().width()) / seen.w;
    bool entered = false;
    for (const BLRect& r : rects) {
        const newui::Point local(float((r.x + r.w * 0.5 - seen.x) * zoom), float((r.y + r.h * 0.5 - seen.y) * zoom));
        const newui::Point inRoot = graph.mapTo(frame_.rootView(), local);
        const HWND target = frame_.rootView().windowHandle();
        const LPARAM at = MAKELPARAM(int(inRoot.x), int(inRoot.y));
        ::SendMessage(target, WM_LBUTTONDOWN, MK_LBUTTON, at);
        ::SendMessage(target, WM_LBUTTONUP, 0, at);
        ::SendMessage(target, WM_LBUTTONDBLCLK, MK_LBUTTON, at);
        ::SendMessage(target, WM_LBUTTONUP, 0, at);
        if (graph.scope() != 0u) {
            entered = true;
            break;
        }
    }
    EXPECT_TRUE(entered);
}
