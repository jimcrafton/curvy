#include "curvy/graphview.h"

#include "curvy/colorpickercard.h"
#include "curvy/colorwheel.h"
#include "curvy/graphpanel.h"
#include "curvy/sketchdocument.h"
#include "testsupport.h"

#include <newui/mouse_constants.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace {

    // The graph view of a fresh document with a circle and a fill added next to what the document has, driven with real mouse messages.
    class GraphEditTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            curvytest::WorkspaceTest::SetUp();
            window().newDocument("Graph", 612.0f, 792.0f);
            document_ = &window().document();
            circle_ = document_->addGraphNode("circle", "circle").id;
            fill_ = document_->addGraphNode("curvyFill", "fill").id;
            frame_.rootView().updateLayout();
            window().toggleGraph();
            frame_.rootView().updateLayout();
            graph_ = &window().graph();
            graph_->fit();
            messages_.clear();
            graph_->onMessage = [this](const std::string& text) { messages_.push_back(text); };
        }

        std::size_t indexOf(jitnodes::NodeId id) const {
            const std::vector<jitnodes::NodeId> ids = document_->network().nodeIds();
            return std::size_t(std::find(ids.begin(), ids.end(), id) - ids.begin());
        }

        BLRect rectOf(jitnodes::NodeId id) const { return graph_->nodeRects().at(indexOf(id)); }

        // A point of the graph as the LPARAM of a window message.
        LPARAM at(double x, double y) const {
            const BLRect seen = graph_->visibleRect();
            const double zoom = graph_->zoom();
            const newui::Point local(float((x - seen.x) * zoom), float((y - seen.y) * zoom));
            const newui::Point inRoot = graph_->mapTo(frame_.rootView(), local);
            return MAKELPARAM(int(inRoot.x), int(inRoot.y));
        }

        HWND target() const { return frame_.rootView().windowHandle(); }

        void drag(double fromX, double fromY, double toX, double toY, WPARAM keys = 0) {
            ::SendMessage(target(), WM_LBUTTONDOWN, MK_LBUTTON | keys, at(fromX, fromY));
            ::SendMessage(target(), WM_MOUSEMOVE, MK_LBUTTON | keys, at((fromX + toX) * 0.5, (fromY + toY) * 0.5));
            ::SendMessage(target(), WM_MOUSEMOVE, MK_LBUTTON | keys, at(toX, toY));
            ::SendMessage(target(), WM_LBUTTONUP, keys, at(toX, toY));
        }

        void click(double x, double y, WPARAM keys = 0) {
            ::SendMessage(target(), WM_LBUTTONDOWN, MK_LBUTTON | keys, at(x, y));
            ::SendMessage(target(), WM_LBUTTONUP, keys, at(x, y));
        }

        void dragOutputToInput(jitnodes::NodeId from, jitnodes::NodeId to, int input) {
            const BLRect a = rectOf(from), b = rectOf(to);
            drag(a.x + a.w, a.y + a.h * 0.5, b.x, b.y + 22.0 + 12.0 * input);
        }

        jitnodes::NodeId firstOfType(const std::string& type) const {
            for (const jitnodes::NodeId id : document_->network().nodeIds()) {
                if (document_->network().node(id).typeName() == type) {
                    return id;
                }
            }
            return jitnodes::invalidNodeId;
        }

        curvy::SketchDocument* document_ = nullptr;
        curvy::GraphView* graph_ = nullptr;
        jitnodes::NodeId circle_ = jitnodes::invalidNodeId, fill_ = jitnodes::invalidNodeId;
        std::vector<std::string> messages_;
    };

}

TEST_F(GraphEditTest, draggingFromAnOutputDotToAnInputDotWiresThemAsOneUndoStep) {
    ASSERT_EQ(document_->network().inputOf(fill_, 0), jitnodes::invalidNodeId);
    dragOutputToInput(circle_, fill_, 0);
    EXPECT_EQ(document_->network().inputOf(fill_, 0), circle_);
    EXPECT_EQ(document_->undoStack().undoDescription(), "Connect Nodes");
    window().undo();
    EXPECT_EQ(document_->network().inputOf(fill_, 0), jitnodes::invalidNodeId);
    window().redo();
    EXPECT_EQ(document_->network().inputOf(fill_, 0), circle_);
}

TEST_F(GraphEditTest, draggingAWireOffAnInputDotInTheOpenUnwiresIt) {
    document_->connectGraphNodes(circle_, fill_, 0);
    graph_->fit();
    const BLRect f = rectOf(fill_);
    drag(f.x, f.y + 22.0, f.x - 30.0, f.y + 200.0);
    EXPECT_EQ(document_->network().inputOf(fill_, 0), jitnodes::invalidNodeId);
    EXPECT_EQ(document_->undoStack().undoDescription(), "Disconnect Nodes");
}

TEST_F(GraphEditTest, aWireDroppedOnTheSameInputChangesNothing) {
    document_->connectGraphNodes(circle_, fill_, 0);
    graph_->fit();
    const std::string last = document_->undoStack().undoDescription();
    const BLRect f = rectOf(fill_);
    drag(f.x, f.y + 22.0, f.x + 2.0, f.y + 24.0);
    EXPECT_EQ(document_->network().inputOf(fill_, 0), circle_);
    EXPECT_EQ(document_->undoStack().undoDescription(), last);
}

TEST_F(GraphEditTest, aWireThatWouldMakeACycleIsRefusedWithAMessage) {
    const jitnodes::NodeId second = document_->addGraphNode("curvyFill", "second").id;
    document_->connectGraphNodes(fill_, second, 0);
    graph_->fit();
    const std::string last = document_->undoStack().undoDescription();
    dragOutputToInput(second, fill_, 0);
    EXPECT_EQ(document_->undoStack().undoDescription(), last);
    EXPECT_EQ(document_->network().inputOf(fill_, 0), jitnodes::invalidNodeId);
    ASSERT_EQ(messages_.size(), 1u);
    EXPECT_NE(messages_[0].find("cycle"), std::string::npos);
}

TEST_F(GraphEditTest, droppingAWireOnANodeWithoutInputsDoesNothing) {
    const std::string last = document_->undoStack().undoDescription();
    dragOutputToInput(fill_, circle_, 0);
    EXPECT_EQ(document_->undoStack().undoDescription(), last);
    EXPECT_TRUE(messages_.empty());
}

TEST_F(GraphEditTest, deleteRemovesTheSelectedNodeAsOneUndoStep) {
    graph_->select(curvy::NodeRef(circle_));
    ASSERT_TRUE(graph_->deleteSelected());
    EXPECT_FALSE(document_->network().hasNode(circle_));
    EXPECT_EQ(document_->undoStack().undoDescription(), "Delete Node");
    EXPECT_TRUE(graph_->selection().empty());
    window().undo();
    EXPECT_TRUE(document_->network().hasNode(circle_));
}

TEST_F(GraphEditTest, ctrlClickBuildsASelectionAndDeleteRemovesAllOfItInOneStep) {
    graph_->fit();
    const BLRect c = rectOf(circle_), f = rectOf(fill_);
    click(c.x + c.w * 0.5, c.y + c.h * 0.5);
    click(f.x + f.w * 0.5, f.y + f.h * 0.5, MK_CONTROL);
    ASSERT_EQ(graph_->selection().size(), 2u);
    EXPECT_EQ(graph_->selected(), curvy::NodeRef(fill_));

    click(c.x + c.w * 0.5, c.y + c.h * 0.5, MK_CONTROL);   // a second Ctrl+click takes it out again
    EXPECT_EQ(graph_->selection().size(), 1u);
    click(c.x + c.w * 0.5, c.y + c.h * 0.5, MK_CONTROL);
    ASSERT_EQ(graph_->selection().size(), 2u);

    ASSERT_TRUE(graph_->deleteSelected());
    EXPECT_FALSE(document_->network().hasNode(circle_));
    EXPECT_FALSE(document_->network().hasNode(fill_));
    EXPECT_EQ(document_->undoStack().undoDescription(), "Delete Nodes");
    window().undo();
    EXPECT_TRUE(document_->network().hasNode(circle_));
    EXPECT_TRUE(document_->network().hasNode(fill_));
}

TEST_F(GraphEditTest, aShiftDragOnTheBackgroundSelectsTheBoxesItTouches) {
    graph_->fit();
    const BLRect c = rectOf(circle_), f = rectOf(fill_);
    const double left = (std::min)(c.x, f.x) - 10.0, top = (std::min)(c.y, f.y) - 10.0;
    const double right = (std::max)(c.x + c.w, f.x + f.w) + 10.0, bottom = (std::max)(c.y + c.h, f.y + f.h) + 10.0;
    for (const BLRect& other : graph_->nodeRects()) {   // the sweep must start in the open
        ASSERT_FALSE(left >= other.x && left <= other.x + other.w && top >= other.y && top <= other.y + other.h);
    }
    drag(left, top, right, bottom, MK_SHIFT);
    const auto& selection = graph_->selection();
    EXPECT_NE(std::find(selection.begin(), selection.end(), curvy::NodeRef(circle_)), selection.end());
    EXPECT_NE(std::find(selection.begin(), selection.end(), curvy::NodeRef(fill_)), selection.end());
}

TEST_F(GraphEditTest, draggingOneOfSeveralSelectedNodesMovesThemAll) {
    graph_->fit();
    const BLRect c = rectOf(circle_), f = rectOf(fill_);
    click(c.x + c.w * 0.5, c.y + c.h * 0.5);
    click(f.x + f.w * 0.5, f.y + f.h * 0.5, MK_CONTROL);
    const double dx = 40.0 / graph_->zoom(), dy = 30.0 / graph_->zoom();
    drag(f.x + f.w * 0.5, f.y + f.h * 0.5, f.x + f.w * 0.5 + dx, f.y + f.h * 0.5 + dy);   // a plain press on a selected box keeps the selection
    EXPECT_NEAR(rectOf(circle_).x - c.x, dx, 1.5);
    EXPECT_NEAR(rectOf(fill_).y - f.y, dy, 1.5);
    EXPECT_EQ(document_->undoStack().undoDescription(), "Move Node");
}

TEST_F(GraphEditTest, theDocumentsOwnNodesCannotBeDeletedAndTheViewSaysWhy) {
    document_->buildSampleLayers();   // a blank document has no render node yet
    const jitnodes::NodeId render = firstOfType("curvyRender");
    ASSERT_NE(render, jitnodes::invalidNodeId);
    const std::string last = document_->undoStack().undoDescription();
    graph_->select(curvy::NodeRef(render));
    EXPECT_FALSE(graph_->deleteSelected());
    EXPECT_TRUE(document_->network().hasNode(render));
    EXPECT_EQ(document_->undoStack().undoDescription(), last);
    ASSERT_EQ(messages_.size(), 1u);
    EXPECT_NE(messages_[0].find("Cannot delete"), std::string::npos);
}

TEST_F(GraphEditTest, undoingAColourEditBringsTheOpenColourCardAndThePanelBack) {
    graph_->select(curvy::NodeRef(fill_));
    curvy::GraphPanel& panel = window().graphPanel();
    ASSERT_EQ(panel.paramCaption(0), "color");
    const std::string originalHex = panel.paramValue(0);
    panel.toggleColorPicker(0);
    ASSERT_TRUE(panel.colorPickerOpen());
    curvy::ColorPickerCard& card = *panel.colorPicker();
    const double originalRed = card.red(), originalGreen = card.green();

    curvy::ColorWheel& wheel = card.wheel();
    wheel.onDragStarted(wheel);
    wheel.setHsb(0.0, 1.0, 1.0);
    wheel.onChanged(wheel);
    wheel.onDragFinished(wheel);
    ASSERT_NE(panel.paramValue(0), originalHex);
    ASSERT_NE(card.red(), originalRed);

    window().undo();
    EXPECT_EQ(panel.paramValue(0), originalHex);
    EXPECT_NEAR(card.red(), originalRed, 1e-6);
    EXPECT_NEAR(card.green(), originalGreen, 1e-6);
    window().redo();
    EXPECT_NE(panel.paramValue(0), originalHex);
    EXPECT_NE(card.red(), originalRed);
}

TEST_F(GraphEditTest, theTagsFieldEditsANodesTagsAsOneUndoStep) {
    graph_->select(curvy::NodeRef(circle_));
    curvy::GraphPanel& panel = window().graphPanel();
    panel.retag("shape  sketch shape");   // blanks are separators and a repeated word counts once
    const std::vector<std::string> expected = {"shape", "sketch"};
    EXPECT_EQ(document_->network().node(circle_).tags(), expected);
    EXPECT_EQ(document_->undoStack().undoDescription(), "Change Tags");
    window().undo();
    EXPECT_TRUE(document_->network().node(circle_).tags().empty());
    window().redo();
    EXPECT_EQ(document_->network().node(circle_).tags(), expected);

    const std::string last = document_->undoStack().undoDescription();
    panel.retag("shape sketch");   // the same tags: no step
    EXPECT_EQ(document_->undoStack().undoDescription(), last);
}

TEST_F(GraphEditTest, theTagsThatMarkTheDocumentsOwnNodesCannotBeEditedFromThePanel) {
    document_->buildSampleLayers();
    const curvy::NodeRef layer = document_->childrenOf()[0];
    graph_->select(layer);
    curvy::GraphPanel& panel = window().graphPanel();
    std::vector<std::string> refusals;
    panel.onRefused = [&](const std::string& message) { refusals.push_back(message); };
    const std::vector<std::string> before = document_->node(layer).tags();
    ASSERT_NE(std::find(before.begin(), before.end(), "layer"), before.end());

    panel.retag("mine");                 // takes the layer tag off
    EXPECT_EQ(document_->node(layer).tags(), before);
    panel.retag("layer group mine");     // adds the group tag
    EXPECT_EQ(document_->node(layer).tags(), before);
    EXPECT_EQ(refusals.size(), 2u);

    std::string text;
    for (const std::string& tag : before) {
        text += tag + " ";
    }
    panel.retag(text + "mine");          // keeping the reserved ones is fine
    EXPECT_NE(std::find(document_->node(layer).tags().begin(), document_->node(layer).tags().end(), "mine"), document_->node(layer).tags().end());
    EXPECT_EQ(refusals.size(), 2u);
}

TEST_F(GraphEditTest, aNodeWithoutAPositionNeverLandsOnAPinnedBox) {
    const std::vector<BLRect> laidOut = graph_->nodeRects();
    const std::size_t first = indexOf(circle_);
    // Pin the fill exactly where the circle is laid out.
    document_->setGraphPositionsLive(0, {{fill_, {float(laidOut[first].x), float(laidOut[first].y)}}});
    const BLRect a = rectOf(circle_), b = rectOf(fill_);
    const bool overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
    EXPECT_FALSE(overlap);
}
