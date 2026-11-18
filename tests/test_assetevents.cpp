#include "pythonconsole.h"
#include "testsupport.h"

#include "curvy/assetevents.h"
#include "curvy/sketchdocument.h"

#include <jitnodes/graph/assetLibrary.h>
#include <jitnodes/graph/serialize.h>

#include <gtest/gtest.h>

namespace {

    using jitnodes::AssetDefinition;
    using jitnodes::AssetLibrary;

    std::string assetText(const std::string& name, const std::string& scripts) {
        return "{ format: 'jitnodes-asset', version: 1, type: '" + name + "', inputs: 1, promote: { amount: 1 },\n"
               "  scripts: { " + scripts + " },\n"
               "  graph: { display: 2, nodes: [\n"
               "    { id: 1, type: 'subnetInput', name: 'in', floats: { index: 0 }, inputs: [] },\n"
               "    { id: 2, type: 'transform', name: 'xf', inputs: [1] } ] } }";
    }

    class AssetEventsTest : public ::testing::Test {
    protected:
        void SetUp() override {
            document_.newBlank("Events", 612.0f, 792.0f);
        }

        void TearDown() override {
            for (const std::string& name : names_) {
                if (AssetLibrary::instance().has(name)) {
                    AssetLibrary::instance().uninstall(name);
                }
            }
        }

        void install(const std::string& name, const std::string& scripts) {
            AssetLibrary::instance().apply(AssetDefinition::fromText(assetText(name, scripts)));
            names_.push_back(name);
        }

        std::size_t countOf(const std::string& type) {
            std::size_t count = 0;
            for (jitnodes::NodeId id : document_.network().nodeIds()) {
                count += document_.network().node(id).typeName() == type ? 1 : 0;
            }
            return count;
        }

        curvy::SketchDocument document_;
        std::vector<std::string> names_;
    };

}

TEST_F(AssetEventsTest, onCreatedRunsForAnInstanceWithItsReasonAndTheNodeHandle) {
    install("evtMarks", "onCreated: \"kwargs['node'].network.add_node('null', 'made_' + kwargs['reason'])\"");
    curvy::AssetEvents events(&curvytest::sharedConsole());
    events.setTrustPrompt([](const std::string&, const std::vector<std::string>&) { return true; });
    const curvy::NodeRef added = document_.addGraphNode("evtMarks");

    EXPECT_EQ(events.fireCreated(document_, nullptr, added, "pasted"), "");
    bool found = false;
    for (jitnodes::NodeId id : document_.network().nodeIds()) {
        found = found || document_.network().node(id).name() == "made_pasted";
    }
    EXPECT_TRUE(found);
}

TEST_F(AssetEventsTest, loadedFiresForEveryInstanceOfTheDocumentIncludingThoseInSubnetsInTheOrderOfTheWalk) {
    install("evtWalk", "onCreated: \"doc.add_node('null', 'seen_' + kwargs['reason'] + '_' + kwargs['node'].name())\"");
    curvy::AssetEvents events(&curvytest::sharedConsole());
    events.setTrustPrompt([](const std::string&, const std::vector<std::string>&) { return true; });
    document_.addGraphNode("evtWalk", "first");
    const curvy::NodeRef subnet = document_.addGraphNode("subnet", "box");
    document_.addGraphNode("evtWalk", "inner", document_.scopeOfSubnet(subnet));
    document_.addGraphNode("evtWalk", "last");

    EXPECT_EQ(events.fireCreatedInDocument(document_, nullptr, "loaded"), "");
    std::vector<std::string> seen;
    for (jitnodes::NodeId id : document_.network().nodeIds()) {
        const std::string name = document_.network().node(id).name();
        if (name.rfind("seen_", 0) == 0) {
            seen.push_back(name);
        }
    }
    EXPECT_EQ(seen, (std::vector<std::string>{"seen_loaded_first", "seen_loaded_inner", "seen_loaded_last"}));
}

TEST_F(AssetEventsTest, pastedFiresOnlyForTheGivenNodesAndTheirSubnetContents) {
    install("evtPaste", "onCreated: \"doc.add_node('null', 'seen_' + kwargs['reason'] + '_' + kwargs['node'].name())\"");
    curvy::AssetEvents events(&curvytest::sharedConsole());
    events.setTrustPrompt([](const std::string&, const std::vector<std::string>&) { return true; });
    const curvy::NodeRef old = document_.addGraphNode("evtPaste", "old");
    const curvy::NodeRef other = document_.addGraphNode("evtPaste", "other");
    const std::vector<jitnodes::NodeId> pasted = jitnodes::pasteNodes(document_.network(), {jitnodes::copyNode(document_.network(), other.id)});
    ASSERT_EQ(pasted.size(), 1u);

    EXPECT_EQ(events.fireCreatedFor(document_, nullptr, 0, pasted, "pasted"), "");
    const std::string copyName = document_.network().node(pasted.front()).name();
    std::vector<std::string> seen;
    for (jitnodes::NodeId id : document_.network().nodeIds()) {
        const std::string name = document_.network().node(id).name();
        if (name.rfind("seen_", 0) == 0) {
            seen.push_back(name);
        }
    }
    EXPECT_EQ(seen, std::vector<std::string>{"seen_pasted_" + copyName});
    (void)old;
}

TEST_F(AssetEventsTest, pastingASubnetFiresForTheInstancesInsideTheCopyOnly) {
    install("evtPasteIn", "onCreated: \"doc.add_node('null', 'seen_' + kwargs['reason'] + '_' + kwargs['node'].name())\"");
    curvy::AssetEvents events(&curvytest::sharedConsole());
    events.setTrustPrompt([](const std::string&, const std::vector<std::string>&) { return true; });
    const curvy::NodeRef box = document_.addGraphNode("subnet", "box");
    document_.addGraphNode("evtPasteIn", "inner", document_.scopeOfSubnet(box));
    const std::vector<jitnodes::NodeId> pasted = jitnodes::pasteNodes(document_.network(), {jitnodes::copyNode(document_.network(), box.id)});
    ASSERT_EQ(pasted.size(), 1u);

    EXPECT_EQ(events.fireCreatedFor(document_, nullptr, 0, pasted, "pasted"), "");
    std::vector<std::string> seen;
    for (jitnodes::NodeId id : document_.network().nodeIds()) {
        const std::string name = document_.network().node(id).name();
        if (name.rfind("seen_", 0) == 0) {
            seen.push_back(name);
        }
    }
    EXPECT_EQ(seen, std::vector<std::string>{"seen_pasted_inner"});   // once: the original box's instance did not fire
}

TEST_F(AssetEventsTest, aMissingScriptOrANodeThatIsNotAnInstanceDoesNothing) {
    install("evtPlain", "module: 'x = 1'");
    curvy::AssetEvents events(&curvytest::sharedConsole());
    events.setTrustPrompt([](const std::string&, const std::vector<std::string>&) { return true; });
    const curvy::NodeRef added = document_.addGraphNode("evtPlain");
    const std::size_t before = document_.network().nodeIds().size();
    EXPECT_EQ(events.fireCreated(document_, nullptr, added, "added"), "");
    EXPECT_EQ(document_.network().nodeIds().size(), before);

    const curvy::NodeRef plain = document_.addGraphNode("null");
    EXPECT_EQ(events.fireCreated(document_, nullptr, plain, "added"), "");
}

TEST_F(AssetEventsTest, theSharedModuleIsAvailableToEvents) {
    install("evtModule", "module: \"def label(): return 'from_module'\", onCreated: \"kwargs['node'].network.add_node('null', module.label())\"");
    curvy::AssetEvents events(&curvytest::sharedConsole());
    events.setTrustPrompt([](const std::string&, const std::vector<std::string>&) { return true; });
    const curvy::NodeRef added = document_.addGraphNode("evtModule");
    EXPECT_EQ(events.fireCreated(document_, nullptr, added, "added"), "");
    bool found = false;
    for (jitnodes::NodeId id : document_.network().nodeIds()) {
        found = found || document_.network().node(id).name() == "from_module";
    }
    EXPECT_TRUE(found);
}

TEST_F(AssetEventsTest, aScriptErrorIsReturnedAndTheDocumentStaysUsable) {
    install("evtBroken", "onCreated: \"raise ValueError('boom')\"");
    curvy::AssetEvents events(&curvytest::sharedConsole());
    events.setTrustPrompt([](const std::string&, const std::vector<std::string>&) { return true; });
    const curvy::NodeRef added = document_.addGraphNode("evtBroken");
    const std::string error = events.fireCreated(document_, nullptr, added, "added");
    EXPECT_NE(error.find("boom"), std::string::npos);
    EXPECT_NE(error.find("evtBroken.onCreated"), std::string::npos);
    EXPECT_TRUE(curvytest::sharedConsole().run("1 + 1", &document_).ok);
}

TEST_F(AssetEventsTest, anAssetOutsideTheTrustedFoldersAsksOnceAndRunsOnlyWhenAllowed) {
    install("evtAsk", "onCreated: \"kwargs['node'].network.add_node('null', 'ran')\"");
    curvy::AssetEvents events(&curvytest::sharedConsole());
    int asked = 0;
    bool answer = false;
    events.setTrustPrompt([&](const std::string& asset, const std::vector<std::string>& scripts) {
        ++asked;
        EXPECT_EQ(asset, "evtAsk");
        EXPECT_EQ(scripts, std::vector<std::string>{"onCreated"});
        return answer;
    });
    const curvy::NodeRef added = document_.addGraphNode("evtAsk");
    const std::size_t before = document_.network().nodeIds().size();

    events.fireCreated(document_, nullptr, added, "added");
    events.fireCreated(document_, nullptr, added, "added");
    EXPECT_EQ(asked, 1);   // a refusal is remembered for the session
    EXPECT_EQ(document_.network().nodeIds().size(), before);

    curvy::AssetEvents second(&curvytest::sharedConsole());
    second.setTrustPrompt([&](const std::string&, const std::vector<std::string>&) {
        ++asked;
        return true;
    });
    second.fireCreated(document_, nullptr, added, "added");
    second.fireCreated(document_, nullptr, added, "added");
    EXPECT_EQ(asked, 2);   // allowed: asked once more by the new object, then never again
    EXPECT_EQ(document_.network().nodeIds().size(), before + 2);
    EXPECT_TRUE(second.isTrusted("evtAsk"));
}

TEST_F(AssetEventsTest, withoutAPromptAnUntrustedAssetDoesNotRun) {
    install("evtNoPrompt", "onCreated: \"kwargs['node'].network.add_node('null', 'ran')\"");
    curvy::AssetEvents events(&curvytest::sharedConsole());
    const curvy::NodeRef added = document_.addGraphNode("evtNoPrompt");
    const std::size_t before = document_.network().nodeIds().size();
    events.fireCreated(document_, nullptr, added, "added");
    EXPECT_EQ(document_.network().nodeIds().size(), before);
}

TEST_F(AssetEventsTest, theAllowanceIsRememberedByHashInTheTrustFileAndAnEditedScriptAsksAgain) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "curvyTrustTest" / "trusted.txt";
    std::filesystem::remove_all(file.parent_path());
    install("evtHash", "onCreated: \"pass\"");
    const curvy::NodeRef added = document_.addGraphNode("evtHash");
    {
        curvy::AssetEvents first(&curvytest::sharedConsole());
        first.setTrustFile(file);
        first.setTrustPrompt([](const std::string&, const std::vector<std::string>&) { return true; });
        first.fireCreated(document_, nullptr, added, "added");
    }
    curvy::AssetEvents again(&curvytest::sharedConsole());
    again.setTrustFile(file);
    EXPECT_TRUE(again.isTrusted("evtHash"));

    AssetDefinition edited = AssetLibrary::instance().definition("evtHash");
    edited.scripts["onCreated"] = "pass  # changed";
    AssetLibrary::instance().apply(edited);
    EXPECT_FALSE(again.isTrusted("evtHash"));
    std::filesystem::remove_all(file.parent_path());
}

TEST_F(AssetEventsTest, anAssetInstalledFromATrustedFolderRunsWithoutAsking) {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "curvyTrustFolder";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    AssetDefinition definition = AssetDefinition::fromText(assetText("evtFolder", "onCreated: \"kwargs['node'].network.add_node('null', 'ran')\""));
    AssetLibrary::instance().save(definition, (folder / "evtFolder.asset.json").u8string());
    names_.push_back("evtFolder");

    curvy::AssetEvents events(&curvytest::sharedConsole());
    events.addTrustedFolder(folder);
    EXPECT_TRUE(events.isTrusted("evtFolder"));
    const curvy::NodeRef added = document_.addGraphNode("evtFolder");
    const std::size_t before = document_.network().nodeIds().size();
    EXPECT_EQ(events.fireCreated(document_, nullptr, added, "added"), "");
    EXPECT_EQ(document_.network().nodeIds().size(), before + 1);
    std::filesystem::remove_all(folder);
}
