#include "testsupport.h"

#include "pythonconsole.h"

#include "curvy/pythonnodetrust.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

    std::filesystem::path trustFile(const char* name) {
        const std::filesystem::path file = std::filesystem::temp_directory_path() / "curvyPythonNodeTrustTests" / name;
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        return file;
    }

}

TEST(PythonNodeTrust, unknownCodeIsRefusedOnceNoticedAndRememberedForTheQuestion) {
    curvy::PythonNodeTrust trust;
    int notices = 0;
    trust.setRefusedNotice([&notices]() { ++notices; });

    EXPECT_FALSE(trust.allows("print(1)"));
    EXPECT_FALSE(trust.allows("print(1)"));
    EXPECT_FALSE(trust.allows("print(2)"));
    EXPECT_EQ(notices, 1) << "one notice until the question is asked";
    EXPECT_EQ(trust.pendingCount(), 2u);
}

TEST(PythonNodeTrust, allowingRemembersTheExactTextInTheFileAndAnEditedTextAsksAgain) {
    const std::filesystem::path file = trustFile("allow.txt");
    {
        curvy::PythonNodeTrust trust;
        trust.setTrustFile(file);
        trust.setPrompt([](std::size_t count) {
            EXPECT_EQ(count, 1u);
            return true;
        });
        EXPECT_FALSE(trust.allows("a = 1"));
        EXPECT_TRUE(trust.askPending());
        EXPECT_TRUE(trust.allows("a = 1"));
        EXPECT_FALSE(trust.allows("a = 2")) << "an edited text is another text";
    }
    curvy::PythonNodeTrust next;
    next.setTrustFile(file);
    EXPECT_TRUE(next.allows("a = 1")) << "the allowance survives a restart";
    EXPECT_FALSE(next.isTrusted("a = 2"));
}

TEST(PythonNodeTrust, decliningIsNotAskedAgainInTheSameSessionAndIsNotSaved) {
    const std::filesystem::path file = trustFile("decline.txt");
    curvy::PythonNodeTrust trust;
    trust.setTrustFile(file);
    int asked = 0;
    trust.setPrompt([&asked](std::size_t) {
        ++asked;
        return false;
    });
    EXPECT_FALSE(trust.allows("x"));
    EXPECT_FALSE(trust.askPending());
    EXPECT_FALSE(trust.allows("x"));
    EXPECT_EQ(trust.pendingCount(), 0u);
    EXPECT_FALSE(trust.askPending());
    EXPECT_EQ(asked, 1);
    EXPECT_FALSE(std::filesystem::exists(file));
}

TEST(PythonNodeTrust, trustingTextDirectlySkipsTheQuestion) {
    curvy::PythonNodeTrust trust;
    EXPECT_FALSE(trust.allows("own code"));
    trust.trust("own code");
    EXPECT_TRUE(trust.allows("own code"));
    EXPECT_EQ(trust.pendingCount(), 0u);
}

class PythonNodeGateTest : public curvytest::WorkspaceTest {
protected:
    void SetUp() override {
        WorkspaceTest::SetUp();
        window().newDocument("Gate", 612.0f, 792.0f);
        curvytest::sharedConsole().setMainThreadPoster([](std::function<void()>) {});
        window().setScriptConsole(&curvytest::sharedConsole());
        window().pythonNodeTrust()->setTrustFile({});
    }

    void TearDown() override {
        WorkspaceTest::TearDown();
        curvytest::sharedConsole().setMainThreadPoster(nullptr);
    }
};

TEST_F(PythonNodeGateTest, aWindowWithTheConsoleRefusesPythonNodeCodeUntilTheUserAllowsIt) {
    ASSERT_NE(window().pythonNodeTrust(), nullptr);
    jitnodes::Network& network = window().document().network();
    const jitnodes::NodeId node = network.addNode("python");
    network.node(node).setString("code", "geo.resize_points(3)");

    EXPECT_THROW(network.cook(node), std::exception);
    EXPECT_EQ(window().pythonNodeTrust()->pendingCount(), 1u);

    window().pythonNodeTrust()->setPrompt([](std::size_t) { return true; });
    EXPECT_TRUE(window().pythonNodeTrust()->askPending());
    EXPECT_EQ(network.cook(node)->pointCount(), 3u) << "the node that failed is cooked again once it is allowed";
}

TEST_F(PythonNodeGateTest, aClosedWindowLeavesNothingAllowed) {
    jitnodes::Network network;
    const jitnodes::NodeId node = network.addNode("python");
    network.node(node).setString("code", "pass");
    window().pythonNodeTrust()->trust("pass");
    EXPECT_NO_THROW(network.cook(node));
    window().setScriptConsole(nullptr);
    const jitnodes::NodeId second = network.addNode("python");
    network.node(second).setString("code", "pass");
    EXPECT_THROW(network.cook(second), std::exception) << "the first node's result was cached, a new node asks the gate again";
}

TEST(PythonNodeTrust, blankCodeNeedsNoAllowanceBecauseNothingRuns) {
    curvy::PythonNodeTrust trust;
    EXPECT_TRUE(trust.allows(""));
    EXPECT_TRUE(trust.allows("  \n\t"));
    EXPECT_EQ(trust.pendingCount(), 0u);
}
