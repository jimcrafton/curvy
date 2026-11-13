#include "curvy/sketchdocument.h"

#include "curvy/objectnode.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using curvy::ObjectNode;
using curvy::ObjectTree;
using curvy::SketchDocument;

namespace {

    constexpr int kLayers = 60;
    constexpr int kGroupsPerLayer = 10;
    constexpr int kCirclesPerGroup = 25;

    // A few tens of thousands of nodes: a save takes seconds, long enough for a GUI test to act while it runs. The file opens with the graph
    // view up and the first layer selected, so the test can edit through the property panel without knowing where any box is.
    void buildLarge(SketchDocument& document) {
        document.newBlank("Large", 1920.0f, 1080.0f);
        jitnodes::Network& root = document.network();
        const jitnodes::NodeId rootMerge = ObjectTree::ensureRootMerge(root);
        for (int l = 0; l < kLayers; ++l) {
            ObjectNode& layer = *ObjectTree::objectAt(root, ObjectTree::addObject(root, rootMerge, curvy::layerTag, "Layer" + std::to_string(l + 1)));
            for (int g = 0; g < kGroupsPerLayer; ++g) {
                ObjectNode& group = *ObjectTree::objectAt(layer.network(), ObjectTree::addObject(layer.network(), layer.mergeId(), curvy::groupTag, "Group" + std::to_string(g + 1)));
                for (int c = 0; c < kCirclesPerGroup; ++c) {
                    const float x = 40.0f + float((l * 61 + c * 37) % 1840);
                    const float y = 40.0f + float((g * 97 + c * 53) % 1000);
                    ObjectTree::addCircle(group.network(), group.mergeId(), x, y, 12.0f + float(c % 9), newui::Color(0.2f + 0.01f * float(l % 40), 0.4f, 0.8f - 0.03f * float(g), 0.7f));
                }
            }
        }
        curvy::ViewState view;
        view.graph = curvy::GraphViewState();
        view.graph->open = true;
        view.graph->selected.push_back(document.childrenOf()[0].id);
        document.setViewState(view);
    }

}

// The GUI test (tests/gui/test_curvy_unsaved.py) runs this with CURVY_LARGE_FILE set to where the file should go.
TEST(LargeDocument, DISABLED_WriteTheLargeFile) {
    const char* target = std::getenv("CURVY_LARGE_FILE");
    ASSERT_NE(target, nullptr) << "set CURVY_LARGE_FILE to the path to write";
    SketchDocument document;
    buildLarge(document);
    ASSERT_TRUE(document.save(target)) << document.lastError();
}

TEST(LargeDocument, itSavesAndLoadsBackWithEveryLayer) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "curvyLargeDocument.curvy";
    SketchDocument document;
    buildLarge(document);
    ASSERT_TRUE(document.save(file.u8string())) << document.lastError();

    SketchDocument loaded;
    ASSERT_TRUE(loaded.load(file.u8string())) << loaded.lastError();
    EXPECT_EQ(loaded.childrenOf().size(), std::size_t(kLayers));
    EXPECT_EQ(loaded.childrenOf(loaded.childrenOf()[0]).size(), std::size_t(kGroupsPerLayer));
    EXPECT_FALSE(loaded.isModified());
    ASSERT_TRUE(loaded.viewState().graph.has_value());
    EXPECT_TRUE(loaded.viewState().graph->open);
    EXPECT_EQ(loaded.viewState().graph->selected.size(), 1u);
    std::filesystem::remove(file);
}
