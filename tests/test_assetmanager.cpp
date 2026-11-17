#include "pythonconsole.h"

#include "curvy/assetmanagermodel.h"
#include "curvy/assetpropertiessession.h"

#include <jitnodes/graph/assetLibrary.h>
#include <jitnodes/graph/serialize.h>
#include <jitnodes/graph/subnet.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {

    using jitnodes::AssetDefinition;
    using jitnodes::AssetLibrary;

    std::string definitionText(const std::string& name, const std::vector<std::string>& params) {
        std::string promote;
        for (const std::string& p : params) {
            promote += (promote.empty() ? "" : ", ") + p + ": 1";
        }
        return "{ format: 'jitnodes-asset', version: 1, type: '" + name + "', inputs: 1, promote: { " + promote + " },\n"
               "  graph: { display: 2, nodes: [\n"
               "    { id: 1, type: 'subnetInput', name: 'in', floats: { index: 0 }, inputs: [] },\n"
               "    { id: 2, type: 'transform', name: 'xf', inputs: [1] } ] } }";
    }

    // Registers assets for one test and removes them again (the library is process-global).
    class AssetUiTest : public ::testing::Test {
    protected:
        void SetUp() override {
            curvytest::sharedConsole();
            folder_ = std::filesystem::temp_directory_path() / ("curvy_asset_ui_" + std::to_string(counter_++));
            std::filesystem::remove_all(folder_);
            std::filesystem::create_directories(folder_);
        }

        void TearDown() override {
            for (const std::string& name : names_) {
                if (AssetLibrary::instance().has(name)) {
                    AssetLibrary::instance().uninstall(name);
                }
            }
            std::filesystem::remove_all(folder_);
        }

        AssetDefinition add(const std::string& name, const std::string& category, const std::vector<std::string>& params = {"amount"},
                            const std::string& label = {}) {
            AssetDefinition d = AssetDefinition::fromText(definitionText(name, params));
            d.category = category;
            d.label = label;
            AssetLibrary::instance().apply(d);
            names_.push_back(name);
            return d;
        }

        // Like add, but installed from a file, so the asset is part of the library and not only embedded.
        void addFile(const std::string& name, const std::string& category, const std::vector<std::string>& params = {"amount"},
                     const std::string& label = {}) {
            AssetDefinition d = AssetDefinition::fromText(definitionText(name, params));
            d.category = category;
            d.label = label;
            AssetLibrary::instance().installFile(write(name + ".asset.json", d.toText()));
            names_.push_back(name);
        }

        std::string write(const std::string& file, const std::string& text) {
            const std::string path = (folder_ / file).string();
            std::ofstream(path, std::ios::binary) << text;
            return path;
        }

        std::filesystem::path folder_;
        std::vector<std::string> names_;
        static inline int counter_ = 0;
    };

    std::vector<std::string> titlesOf(const curvy::AssetManagerModel& model) {
        std::vector<std::string> titles;
        for (const auto& group : model.groups()) {
            titles.push_back(group.title);
        }
        return titles;
    }

}

TEST_F(AssetUiTest, theManagerGroupsFiltersAndListsCategories) {
    addFile("amTestInk", "", {"referenceWidth", "guideLength"});
    addFile("amTestWobble", "Deform", {"amount"}, "Wobbler");
    addFile("amTestScale", "Deform");
    AssetDefinition embedded = AssetDefinition::fromText(definitionText("amTestEmbedded", {"amount"}));
    AssetLibrary::instance().apply(embedded);
    names_.push_back("amTestEmbedded");

    curvy::AssetManagerModel model({});
    model.setFilterText("amtest");
    EXPECT_EQ(titlesOf(model), (std::vector<std::string>{"Brushes", "Deform", curvy::AssetManagerModel::kEmbeddedTitle}));
    EXPECT_EQ(model.groups()[1].entries.size(), 2u);

    const std::vector<std::string> categories = model.categories();
    EXPECT_EQ(categories.front(), "All");
    EXPECT_EQ(categories.back(), "Embedded");
    EXPECT_NE(std::find(categories.begin(), categories.end(), "Deform"), categories.end());

    model.setCategory("Deform");
    EXPECT_EQ(titlesOf(model), (std::vector<std::string>{"Deform"}));
    model.setCategory("Embedded");
    ASSERT_EQ(model.groups().size(), 1u);
    EXPECT_EQ(model.groups()[0].entries[0].name, "amTestEmbedded");
    model.setCategory("All");
    model.setFilterText("WOBBLER");   // the label, not the name
    ASSERT_EQ(model.groups().size(), 1u);
    EXPECT_EQ(model.groups()[0].entries[0].name, "amTestWobble");
    model.setFilterText("nothing like it");
    EXPECT_TRUE(model.groups().empty());
}

TEST_F(AssetUiTest, theManagerShowsDetailsTheOutdatedBannerAndUpdatesInstances) {
    add("amTestDetail", "Deform", {"amount", "speed"});
    jitnodes::Network net(1);
    net.addNode("amTestDetail", "one");
    net.addNode("amTestDetail", "two");

    curvy::AssetManagerHooks hooks;
    hooks.network = [&net]() { return &net; };
    hooks.updateInstances = [&net](const std::string& type, std::size_t& count) {
        count = jitnodes::updateAssetInstances(net, type);
        return std::string();
    };
    curvy::AssetManagerModel model(hooks);
    int changes = 0;
    model.onChanged = [&changes]() { ++changes; };

    EXPECT_FALSE(model.details().has_value());
    model.select("amTestDetail");
    ASSERT_TRUE(model.details().has_value());
    EXPECT_EQ(model.details()->instances, 2u);
    EXPECT_EQ(model.details()->params, (std::vector<std::string>{"amount", "speed"}));
    EXPECT_TRUE(model.details()->embedded);
    EXPECT_TRUE(model.bannerText().empty());
    EXPECT_GT(changes, 0);

    AssetDefinition edited = AssetLibrary::instance().definition("amTestDetail");
    edited.params[0].defaultValue = 5.0f;
    AssetLibrary::instance().apply(edited);   // the library tells the model
    EXPECT_EQ(model.bannerText(), "2 instances are older than v1");
    EXPECT_EQ(model.outdatedCount(), 2u);
    EXPECT_NE(model.statusText().find("2 outdated"), std::string::npos);

    EXPECT_EQ(model.updateInstances(), "");
    EXPECT_TRUE(model.bannerText().empty());
    EXPECT_EQ(model.outdatedCount(), 0u);
}

TEST_F(AssetUiTest, theManagerCommandsReportWhatIsMissingAndWhatFailed) {
    add("amTestCmd", "Deform");
    jitnodes::Network net(1);
    std::string added;
    curvy::AssetManagerHooks hooks;
    hooks.network = [&net]() { return &net; };
    hooks.addInstance = [&](const std::string& type) {
        added = type;
        net.addNode(type, "inst");
        return std::string();
    };
    curvy::AssetManagerModel model(hooks);

    EXPECT_EQ(model.addInstance(), "Select an asset first");
    EXPECT_EQ(model.reload(), "Select an asset first");
    EXPECT_EQ(model.uninstall(), "Select an asset first");
    model.select("amTestCmd");
    EXPECT_EQ(model.addInstance(), "");
    EXPECT_EQ(added, "amTestCmd");
    EXPECT_EQ(model.details()->instances, 1u);
    EXPECT_NE(model.uninstall().find("instance"), std::string::npos);   // refused while the document holds one
    EXPECT_TRUE(AssetLibrary::instance().has("amTestCmd"));
    EXPECT_NE(model.reload().find("no file"), std::string::npos);        // embedded: nothing to read again
    EXPECT_EQ(model.unlock(), "No document is open");

    const std::string path = (folder_ / "amTestCmd.asset.json").string();
    EXPECT_EQ(model.saveToLibrary(path), "");
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_FALSE(model.details()->embedded);
    EXPECT_EQ(model.details()->source, path);
    EXPECT_EQ(model.reload(), "");

    net.removeNode(net.findNode("inst"));
    model.refresh();
    EXPECT_EQ(model.uninstall(), "");
    EXPECT_FALSE(AssetLibrary::instance().has("amTestCmd"));
    EXPECT_TRUE(model.selected().empty());

    EXPECT_EQ(model.installFile(path), "");
    EXPECT_EQ(model.selected(), "amTestCmd");
    names_.push_back("amTestCmd");
    EXPECT_NE(model.installFile((folder_ / "missing.asset.json").string()), "");
    write("amTestBroken.asset.json", "{ nope");
    std::size_t loaded = 0;
    EXPECT_NE(model.installFolder(folder_.string(), &loaded).find("amTestBroken"), std::string::npos);
    EXPECT_EQ(loaded, 1u);
}

TEST_F(AssetUiTest, aBlankDefinitionNeedsANameThenCreatesAnAsset) {
    curvy::AssetPropertiesSession session = curvy::AssetPropertiesSession::blank();
    EXPECT_EQ(session.kind(), curvy::AssetPropertiesSession::Kind::blank);
    EXPECT_STREQ(session.saveLabel(), "Create");
    EXPECT_EQ(session.working().inputs, 0);
    EXPECT_EQ(session.versionText(), "1 (new)");
    EXPECT_NE(session.problem().find("type name"), std::string::npos);
    EXPECT_FALSE(session.dirty());
    EXPECT_NE(session.apply().find("type name"), std::string::npos);

    session.setName("amTestBlank");
    names_.push_back("amTestBlank");
    session.setLabel("Blank");
    EXPECT_EQ(session.problem(), "");
    EXPECT_TRUE(session.dirty());
    EXPECT_EQ(session.footerNote(), "Unsaved changes");
    EXPECT_EQ(session.suggestedFileName(), "amTestBlank.asset.json");

    EXPECT_EQ(session.apply(), "");
    EXPECT_TRUE(AssetLibrary::instance().has("amTestBlank"));
    EXPECT_EQ(session.kind(), curvy::AssetPropertiesSession::Kind::asset);
    EXPECT_STREQ(session.saveLabel(), "Save");
    EXPECT_FALSE(session.dirty());
    EXPECT_EQ(session.footerNote(), "No changes");

    session.setLabel("Changed");
    EXPECT_TRUE(session.dirty());
    session.revert();
    EXPECT_EQ(session.working().label, "Blank");
    EXPECT_FALSE(session.dirty());
}

TEST_F(AssetUiTest, parametersAreAddedReorderedAndRenamedWithTheContents) {
    jitnodes::Network source(1);
    const jitnodes::NodeId sub = source.addNode("subnet", "sub");
    auto& subnet = dynamic_cast<jitnodes::SubnetNode&>(source.node(sub));
    subnet.addSpareFloat("amount", 2.0f);
    jitnodes::Network& inner = subnet.network();
    const jitnodes::NodeId in = inner.addNode("subnetInput", "in");
    const jitnodes::NodeId xf = inner.addNode("transform", "xf");
    inner.connect(in, xf);
    inner.setDisplayNode(xf);
    inner.node(xf).setExpression("tx", "parent.amount * 3");

    curvy::AssetPropertiesSession session = curvy::AssetPropertiesSession::fromSubnet(source, sub, "amTestParams");
    names_.push_back("amTestParams");
    EXPECT_EQ(session.kind(), curvy::AssetPropertiesSession::Kind::subnet);
    EXPECT_TRUE(session.dirty());
    EXPECT_EQ(session.nodeCount(), 2u);
    ASSERT_EQ(session.paramCount(), 1u);

    const std::size_t added = session.addParam();
    EXPECT_EQ(session.param(added).name, "param1");
    EXPECT_EQ(session.addParam(), 2u);
    EXPECT_EQ(session.param(2).name, "param2");
    session.moveParam(2, 0);
    EXPECT_EQ(session.param(0).name, "param2");
    session.removeParam(0);
    EXPECT_EQ(session.paramCount(), 2u);

    jitnodes::AssetParam renamed = session.param(0);
    renamed.name = "size";
    renamed.minimum = 0.0f;
    EXPECT_EQ(session.setParam(0, renamed), "");
    EXPECT_NE(session.working().graphText.find("parent.size * 3"), std::string::npos);
    EXPECT_EQ(session.working().graphText.find("parent.amount"), std::string::npos);
    EXPECT_EQ(session.working().renames.count("amount"), 0u);   // no registered version had it

    jitnodes::AssetParam clash = session.param(1);
    clash.name = "size";
    EXPECT_NE(session.setParam(1, clash).find("already exists"), std::string::npos);
    EXPECT_EQ(session.param(1).name, "param1");
    EXPECT_EQ(session.apply(), "");

    // A parameter the registered version has records its rename.
    curvy::AssetPropertiesSession again = curvy::AssetPropertiesSession::forAsset("amTestParams");
    jitnodes::AssetParam again0 = again.param(0);
    again0.name = "span";
    EXPECT_EQ(again.setParam(0, again0), "");
    EXPECT_EQ(again.working().renames.at("size"), "span");
}

TEST_F(AssetUiTest, brushNamesAreRecognisedAndGuarded) {
    curvy::AssetPropertiesSession session = curvy::AssetPropertiesSession::blank();
    EXPECT_FALSE(session.isBrush());
    for (const char* name : {"referenceWidth", "guideLength"}) {
        jitnodes::AssetParam p;
        p.name = name;
        const std::size_t i = session.addParam();
        EXPECT_EQ(session.setParam(i, p), "");
    }
    EXPECT_TRUE(session.isBrush());
    EXPECT_TRUE(session.touchesBrushNames("guideLength"));
    EXPECT_FALSE(session.touchesBrushNames("amount"));
}

TEST_F(AssetUiTest, aScriptThatBreaksTheRulesBlocksApplyAndSaveUntilItIsFixed) {
    curvy::AssetPropertiesSession session = curvy::AssetPropertiesSession::blank();
    session.setName("amTestScript");
    names_.push_back("amTestScript");
    EXPECT_FALSE(session.hasScript("onFrameChanged"));
    session.setScript("onFrameChanged", "x = 1\nnode.set_float('amount', x)");
    EXPECT_TRUE(session.hasScript("onFrameChanged"));
    EXPECT_NE(session.scriptProblem("onFrameChanged").find("2:1 cannot change this asset"), std::string::npos);
    EXPECT_EQ(session.scriptProblem("onCreated"), "");
    ASSERT_EQ(session.scriptProblems().size(), 1u);
    EXPECT_NE(session.footerNote().find("onFrameChanged 2:1"), std::string::npos);
    EXPECT_NE(session.apply().find("onFrameChanged"), std::string::npos);
    EXPECT_NE(session.save((folder_ / "x.asset.json").string()).find("onFrameChanged"), std::string::npos);
    EXPECT_FALSE(AssetLibrary::instance().has("amTestScript"));
    EXPECT_FALSE(std::filesystem::exists(folder_ / "x.asset.json"));

    session.setScript("onFrameChanged", "print(node.get_float('amount'))");
    EXPECT_EQ(session.problem(), "");
    EXPECT_EQ(session.apply(), "");
    session.setScript("onFrameChanged", "");
    EXPECT_FALSE(session.hasScript("onFrameChanged"));
    EXPECT_EQ(curvy::AssetPropertiesSession::scriptNames().front(), "module");
}

TEST_F(AssetUiTest, savingWritesTheNextVersionAndTheFooterCountsInstances) {
    add("amTestSave", "Deform");
    jitnodes::Network net(1);
    net.addNode("amTestSave", "one");
    net.addNode("amTestSave", "two");

    curvy::AssetPropertiesSession session = curvy::AssetPropertiesSession::forAsset("amTestSave", [&net]() { return &net; });
    EXPECT_EQ(session.versionText(), "1 (Save writes 2)");
    EXPECT_EQ(session.footerNote(), "No changes");
    session.setLabel("Saved");
    EXPECT_EQ(session.footerNote(), "Unsaved changes: 2 instances will be outdated");
    EXPECT_EQ(session.save(""), "Choose a file to save to");

    const std::string path = (folder_ / "amTestSave.asset.json").string();
    EXPECT_EQ(session.save(path), "");
    EXPECT_EQ(session.working().version, 2);
    EXPECT_EQ(session.working().source, path);
    EXPECT_EQ(jitnodes::NodeRegistry::instance().assetVersion("amTestSave"), 2);
    EXPECT_EQ(session.versionText(), "2 (Save writes 3)");
    EXPECT_EQ(session.footerNote(), "No changes");
    EXPECT_EQ(session.save(""), "");   // the source file is kept
    EXPECT_EQ(session.working().version, 3);
    EXPECT_THROW(curvy::AssetPropertiesSession::forAsset("amTestNoSuchAsset"), std::runtime_error);
}

TEST_F(AssetUiTest, matchingASubnetTakesItsContentsAndKeepsParameterSettings) {
    add("amTestMatch", "Deform", {"amount"});
    jitnodes::Network source(1);
    const jitnodes::NodeId sub = source.addNode("subnet", "sub");
    auto& subnet = dynamic_cast<jitnodes::SubnetNode&>(source.node(sub));
    subnet.addSpareFloat("amount", 9.0f);
    subnet.addSpareFloat("extra", 1.0f);
    jitnodes::Network& inner = subnet.network();
    const jitnodes::NodeId in = inner.addNode("subnetInput", "in");
    const jitnodes::NodeId a = inner.addNode("transform", "a");
    const jitnodes::NodeId b = inner.addNode("transform", "b");
    inner.connect(in, a);
    inner.connect(a, b);
    inner.setDisplayNode(b);

    curvy::AssetPropertiesSession session = curvy::AssetPropertiesSession::forAsset("amTestMatch");
    jitnodes::AssetParam amount = session.param(0);
    amount.label = "Kept label";
    EXPECT_EQ(session.setParam(0, amount), "");
    EXPECT_EQ(session.nodeCount(), 2u);

    EXPECT_EQ(session.matchSubnet(source, sub), "");
    EXPECT_EQ(session.nodeCount(), 3u);
    ASSERT_EQ(session.paramCount(), 2u);
    EXPECT_EQ(session.param(0).label, "Kept label");   // an existing parameter keeps what was set for it
    EXPECT_EQ(session.param(1).name, "extra");
    EXPECT_NE(session.matchSubnet(source, source.addNode("line")), "");
    EXPECT_EQ(session.problem(), "");
}
