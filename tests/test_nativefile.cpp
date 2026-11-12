#include "curvy/sketchdocument.h"

#include "testsupport.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using curvy::ColorSpace;
using curvy::SketchDocument;
using curvy::TaskProgress;

namespace {

    class NativeFile : public ::testing::Test {
    protected:
        void SetUp() override {
            path_ = curvytest::tempFile("curvy_test_native.curvy");
            file_ = path_.u8string();
            std::filesystem::remove(path_);
            saved_.newBlank("Round trip", 595.0f, 842.0f, 144.0f, ColorSpace::CMYK, newui::Color(0.5f, 0.25f, 0.125f, 1.0f));
            saved_.setTaskProgress(&progress_);
        }

        void TearDown() override {
            std::filesystem::remove(path_);
            std::filesystem::remove(curvytest::tempFile("curvy_test_native_bad.curvy"));
        }

        std::string text() const {
            std::ifstream in(path_, std::ios::binary);
            std::ostringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }

        std::filesystem::path path_;
        std::string file_;
        SketchDocument saved_;
        TaskProgress progress_;
    };
}

TEST_F(NativeFile, SavingWritesTheFileAndReleasesTheLock) {
    EXPECT_TRUE(saved_.save(file_));
    EXPECT_FALSE(saved_.isLocked());
    EXPECT_EQ(1.0f, progress_.fraction()) << "a save ends at 100%";
    EXPECT_EQ(file_, saved_.filePath());
    EXPECT_FALSE(saved_.isModified());
}

TEST_F(NativeFile, TheFileIsMetaThenTheDocumentThenTheGraph) {
    ASSERT_TRUE(saved_.save(file_));
    const std::string content = text();
    const std::size_t format = content.find("format"), meta = content.find("meta"), document = content.find("document"), graph = content.find("graph");
    EXPECT_LT(format, meta);
    EXPECT_LT(meta, document);
    EXPECT_LT(document, graph);
    EXPECT_NE(std::string::npos, graph);
}

TEST_F(NativeFile, LoadingReadsBackTheSameDocument) {
    ASSERT_TRUE(saved_.save(file_));
    SketchDocument loaded;
    TaskProgress progress;
    loaded.setTaskProgress(&progress);
    EXPECT_TRUE(loaded.load(file_));
    EXPECT_FALSE(loaded.isLocked());
    EXPECT_EQ("Round trip", loaded.name());
    EXPECT_EQ(595.0f, loaded.pageWidth());
    EXPECT_EQ(842.0f, loaded.pageHeight());
    EXPECT_EQ(144.0f, loaded.ppi());
    EXPECT_EQ(ColorSpace::CMYK, loaded.colorMode());
    EXPECT_TRUE(loaded.pageColor() == saved_.pageColor());
    EXPECT_EQ(file_, loaded.filePath());
    EXPECT_FALSE(loaded.isModified());
}

TEST_F(NativeFile, ACancelledLoadOrSaveChangesNothing) {
    ASSERT_TRUE(saved_.save(file_));
    SketchDocument loaded;
    TaskProgress progress;
    loaded.setTaskProgress(&progress);
    ASSERT_TRUE(loaded.load(file_));

    TaskProgress cancelled;
    cancelled.cancel();
    loaded.setTaskProgress(&cancelled);
    EXPECT_FALSE(loaded.load(file_));
    EXPECT_EQ("Round trip", loaded.name());
    const auto before = std::filesystem::file_size(path_);
    EXPECT_FALSE(loaded.save(file_));
    EXPECT_EQ(before, std::filesystem::file_size(path_)) << "a cancelled save leaves the file as it was";
    loaded.setTaskProgress(nullptr);
}

TEST_F(NativeFile, ABadFileIsRefusedAndTheDocumentIsUntouched) {
    ASSERT_TRUE(saved_.save(file_));
    SketchDocument loaded;
    TaskProgress progress;
    loaded.setTaskProgress(&progress);
    ASSERT_TRUE(loaded.load(file_));

    const std::filesystem::path bad = curvytest::tempFile("curvy_test_native_bad.curvy");
    {
        std::ofstream out(bad);
        out << "{ format: \"curvy\", version: 3, document: { page: { width: -5 } }, graph: {} }";
    }
    EXPECT_FALSE(loaded.load(bad.u8string()));
    EXPECT_FALSE(loaded.lastError().empty());
    EXPECT_EQ(595.0f, loaded.pageWidth());
    {
        std::ofstream out(bad);
        out << "{ format: \"curvy\", version: 2, document: {}, graph: {} }";
    }
    EXPECT_FALSE(loaded.load(bad.u8string()));
    EXPECT_NE(std::string::npos, loaded.lastError().find("version")) << "another version is refused by name";
}

namespace {

    curvy::ViewState sampleView(jitnodes::NodeId subnet) {
        curvy::ViewState view;
        view.canvas = curvy::CanvasViewState{2.5f, -30.0f, 12.5f};
        curvy::GraphViewState graph;
        graph.open = true;
        graph.level = {subnet};
        graph.placed = true;
        graph.zoom = 0.75;
        graph.panX = -120.5;
        graph.panY = 40.25;
        graph.selected = {subnet};
        view.graph = graph;
        return view;
    }
}

TEST_F(NativeFile, TheViewComesBackWithTheFile) {
    const curvy::NodeRef layer = saved_.addLayer();
    saved_.setViewState(sampleView(layer.id));
    ASSERT_TRUE(saved_.save(file_));
    const std::string content = text();
    ASSERT_NE(std::string::npos, content.find("view"));
    EXPECT_LT(content.find("document"), content.find("view")) << "the view follows the document";
    EXPECT_LT(content.find("view"), content.find("nodes")) << "and comes before the node graph";

    SketchDocument loaded;
    ASSERT_TRUE(loaded.load(file_));
    const curvy::ViewState& view = loaded.viewState();
    ASSERT_TRUE(view.canvas.has_value());
    EXPECT_EQ(2.5f, view.canvas->zoom);
    EXPECT_EQ(-30.0f, view.canvas->panX);
    EXPECT_EQ(12.5f, view.canvas->panY);
    ASSERT_TRUE(view.graph.has_value());
    EXPECT_TRUE(view.graph->open);
    EXPECT_EQ(std::vector<jitnodes::NodeId>{layer.id}, view.graph->level);
    EXPECT_TRUE(view.graph->placed);
    EXPECT_EQ(0.75, view.graph->zoom);
    EXPECT_EQ(-120.5, view.graph->panX);
    EXPECT_EQ(40.25, view.graph->panY);
    EXPECT_EQ(std::vector<jitnodes::NodeId>{layer.id}, view.graph->selected);
    EXPECT_FALSE(loaded.isModified()) << "a view is not an edit";
}

TEST_F(NativeFile, AFileWithoutAViewOpensWithNoneAndNewClearsIt) {
    ASSERT_TRUE(saved_.save(file_));
    EXPECT_EQ(std::string::npos, text().find("view"));
    SketchDocument loaded;
    loaded.setViewState(sampleView(1));
    ASSERT_TRUE(loaded.load(file_));
    EXPECT_FALSE(loaded.viewState().canvas.has_value());
    EXPECT_FALSE(loaded.viewState().graph.has_value());

    loaded.setViewState(sampleView(1));
    loaded.newBlank("Fresh", 100.0f, 100.0f);
    EXPECT_FALSE(loaded.viewState().graph.has_value());
}

TEST_F(NativeFile, AMalformedPartOfTheViewIsDroppedAndTheFileStillOpens) {
    saved_.setViewState(sampleView(1));
    ASSERT_TRUE(saved_.save(file_));
    std::string content = text();
    int renamed = 0;
    for (std::size_t at = content.find("pan"); at != std::string::npos; at = content.find("pan", at)) {
        content.replace(at, 3, "pxn");   // both parts lose their pan
        ++renamed;
    }
    ASSERT_EQ(2, renamed);
    {
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out << content;
    }
    SketchDocument loaded;
    ASSERT_TRUE(loaded.load(file_)) << loaded.lastError();
    EXPECT_FALSE(loaded.viewState().canvas.has_value()) << "a canvas part without a pan is not used";
    ASSERT_TRUE(loaded.viewState().graph.has_value());
    EXPECT_TRUE(loaded.viewState().graph->open);
    EXPECT_FALSE(loaded.viewState().graph->placed) << "without a zoom and pan the graph view fits itself";
}

TEST_F(NativeFile, TheParameterTagsAPersonAddedAreSavedAndTheDeclaredOnesAreNot) {
    const curvy::NodeRef ref = saved_.addGraphNode("transform");
    jitnodes::Node& node = saved_.node(ref);
    node.addParamTag("sx", "hot");
    node.addSpareFloat("mine", 2.0f);
    node.setParamTags("mine", {"group:color", "channel:r"});
    ASSERT_TRUE(saved_.save(file_));
    EXPECT_NE(std::string::npos, text().find("ptags"));
    EXPECT_EQ(std::string::npos, text().find("unit:0.01")) << "a tag the node type declares is not written";

    SketchDocument loaded;
    ASSERT_TRUE(loaded.load(file_));
    const jitnodes::Node& back = loaded.node(ref);
    EXPECT_EQ((std::vector<std::string>{"unit:0.01", "hot"}), back.paramTags("sx"));
    EXPECT_EQ((std::vector<std::string>{"group:color", "channel:r"}), back.paramTags("mine"));
    EXPECT_TRUE(back.paramTags("sy") == std::vector<std::string>{"unit:0.01"});
}
