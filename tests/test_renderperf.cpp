#include "curvy/canvasview.h"
#include "curvy/objectnode.h"
#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>

#include <chrono>
#include <iostream>

namespace {

    class RenderPerf : public ::testing::Test {
    protected:
        using Clock = std::chrono::steady_clock;

        static double millisecondsSince(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

        struct Group {
            curvy::NodeRef xform;
            curvy::NodeRef firstFill;
        };

        // layers x groups x circles of 96 points, every group at its own place and each layer rotated.
        void build(int layers, int groups, int circles) {
            for (int l = 0; l < layers; ++l) {
                const curvy::NodeRef layer = document_.addLayer();
                auto& layerObject = static_cast<curvy::ObjectNode&>(document_.node(layer));
                layerXforms_.emplace_back(document_.scopeOfSubnet(layer), layerObject.xformId());
                for (int g = 0; g < groups; ++g) {
                    const jitnodes::NodeId groupId = curvy::ObjectTree::addObject(layerObject.network(), layerObject.mergeId(), curvy::groupTag, "");
                    auto& group = *curvy::ObjectTree::objectAt(layerObject.network(), groupId);
                    jitnodes::NodeId first = jitnodes::invalidNodeId;
                    for (int c = 0; c < circles; ++c) {
                        const jitnodes::NodeId fill = curvy::ObjectTree::addCircle(group.network(), group.mergeId(), float(10 + (c % 10) * 12),
                                                                                    float(10 + (c / 10) * 12), 5.0f, newui::Color(0.2f, 0.4f, 0.8f, 0.5f));
                        first = first == jitnodes::invalidNodeId ? fill : first;
                    }
                    const curvy::NodeRef groupRef(document_.scopeOfSubnet(layer), groupId);
                    const std::uint32_t scope = document_.scopeOfSubnet(groupRef);
                    groups_.push_back({curvy::NodeRef(scope, group.xformId()), curvy::NodeRef(scope, first)});
                }
            }
            const auto replaced = Clock::now();
            document_.graphReplaced();
            replaceMs_ = millisecondsSince(replaced);
        }

        double replaceMs_ = 0.0;

        curvy::SketchDocument document_;
        std::vector<curvy::NodeRef> layerXforms_;
        std::vector<Group> groups_;
    };

}

// 10 layers x 10 groups x 100 circles = 10,000 shapes. The limits are loose so a slow machine passes; the printed times are what to look at.
TEST_F(RenderPerf, tenThousandCirclesCookAndDrawQuickly) {
    auto start = Clock::now();
    build(10, 10, 100);
    const double setup = millisecondsSince(start);

    start = Clock::now();
    ASSERT_NE(document_.artwork(), nullptr);
    const double firstCook = millisecondsSince(start);
    EXPECT_EQ(document_.artwork()->primitiveCount(), 10000u);

    start = Clock::now();
    document_.setGraphParam(groups_.front().firstFill, "r", 0.9f);
    document_.artwork();
    const double fillEdit = millisecondsSince(start);

    start = Clock::now();
    document_.setGraphFloats(groups_.back().xform, {{"rot", 30.0f}, {"sx", 1.5f}, {"tx", 40.0f}}, "Transform Group");
    document_.artwork();
    const double groupEdit = millisecondsSince(start);

    start = Clock::now();
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        document_.setGraphFloats(groups_[i].xform, {{"rot", float(i)}, {"skew", float(i % 7)}, {"tx", float(i)}, {"ty", float(i % 13)}}, "Transform Group");
        document_.artwork();
    }
    const double allGroups = millisecondsSince(start);

    start = Clock::now();
    document_.setGraphFloats(layerXforms_.front(), {{"rot", 15.0f}, {"sy", 0.8f}}, "Transform Layer");
    document_.artwork();
    const double layerEdit = millisecondsSince(start);

    curvy::CanvasView canvas;
    canvas.setBounds(newui::Rect(0.0f, 0.0f, 1200.0f, 900.0f));
    canvas.setDocument(&document_);
    BLImage image(1200, 900, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    start = Clock::now();
    canvas.paint(ctx);
    ctx.end();
    const double draw = millisecondsSince(start);

    std::cout << "[perf] setup " << setup << " ms (graphReplaced " << replaceMs_ << " ms); first cook " << firstCook << " ms; edit one fill " << fillEdit << " ms; edit one group transform "
              << groupEdit << " ms; " << groups_.size() << " group edits " << allGroups << " ms (" << allGroups / double(groups_.size())
              << " each); edit a layer transform " << layerEdit << " ms; draw 10000 " << draw << " ms\n";
    EXPECT_LT(groupEdit, 250.0);
    EXPECT_LT(draw, 1000.0);
}
