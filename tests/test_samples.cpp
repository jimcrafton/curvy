#include "curvy/sketchdocument.h"

#include "curvy/objectnode.h"

#include <gtest/gtest.h>

#include <filesystem>

using curvy::NodeRef;
using curvy::SketchDocument;

namespace {

    class SampleFiles : public ::testing::Test {
    protected:
        static std::filesystem::path samples() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "Resources" / "samples";
        }

        static std::string soundFile() { return (samples() / "layersAndGroups.curvy").u8string(); }
        static std::string damagedFile() { return (samples() / "layersAndGroupsDamaged.curvy").u8string(); }

        static void build(SketchDocument& document) {
            document.newBlank("Layers and groups", 595.0f, 842.0f);
            document.buildSampleLayers();
        }

        static curvy::ObjectNode& object(SketchDocument& document, const NodeRef& ref) { return static_cast<curvy::ObjectNode&>(document.node(ref)); }
    };

}

// Run with --gtest_also_run_disabled_tests --gtest_filter=*WriteTheSampleFiles to rewrite the two files in Resources/samples.
TEST_F(SampleFiles, DISABLED_WriteTheSampleFiles) {
    SketchDocument sound;
    build(sound);
    ASSERT_TRUE(sound.save(soundFile()));

    SketchDocument damaged;
    build(damaged);
    const std::vector<NodeRef> layers = damaged.childrenOf();
    const NodeRef group2 = damaged.childrenOf(layers[0])[1];
    curvy::ObjectNode& broken = object(damaged, group2);
    broken.network().removeNode(broken.xformId());
    curvy::ObjectNode& unwired = object(damaged, layers[1]);
    unwired.network().disconnect(unwired.xformId(), 0);
    ASSERT_TRUE(damaged.save(damagedFile()));
}

TEST_F(SampleFiles, theSoundFileLoadsWithNoProblems) {
    SketchDocument document;
    ASSERT_TRUE(document.load(soundFile())) << document.lastError();
    EXPECT_TRUE(document.objectProblems().empty());
    const std::vector<NodeRef> layers = document.childrenOf();
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(document.node(layers[0]).name(), "Layer1");
    EXPECT_EQ(document.childrenOf(layers[0]).size(), 2u);
    EXPECT_EQ(document.childrenOf(layers[1]).size(), 1u);
    EXPECT_EQ(document.childrenOf(layers[2]).size(), 1u);
}

TEST_F(SampleFiles, theDamagedFileIsFlaggedAndRepairsOnRequest) {
    SketchDocument document;
    ASSERT_TRUE(document.load(damagedFile())) << document.lastError();
    EXPECT_EQ(document.objectProblems().size(), 2u);
    document.repairObjectProblems();
    EXPECT_TRUE(document.objectProblems().empty());
    for (const NodeRef& layer : document.childrenOf()) {
        EXPECT_TRUE(object(document, layer).validate().empty());
        for (const NodeRef& group : document.childrenOf(layer)) {
            EXPECT_TRUE(object(document, group).validate().empty());
        }
    }
}
