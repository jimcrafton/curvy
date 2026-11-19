#include "pythonconsole.h"

#include "curvy/assetpropertiessession.h"
#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>
#include <jitnodes/graph/network.h>
#include <jitnodes/graph/serialize.h>
#include <pyPythonNode.h>
#include <jitnodes/graph/assetLibrary.h>
#include <jitnodes/graph/subnet.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <thread>
#include <string>
#include <vector>

namespace {

    using jitnodes::GeometryHandle;
    using jitnodes::Network;
    using jitnodes::NodeId;

    class PythonNodeTest : public ::testing::Test {
    protected:
        void SetUp() override {
            curvytest::sharedConsole();
            jitnodes::python::setPythonNodePolicy([](const jitnodes::Node&) { return true; });
        }
        void TearDown() override {
            jitnodes::python::setPythonNodePolicy(nullptr);
            jitnodes::python::setPythonNodeTimeLimit(std::chrono::seconds(10));
            jitnodes::python::setPythonNodeInterruptCheck(nullptr);
        }

        NodeId addLine(Network& network, int count) {
            const NodeId line = network.addNode("line");
            network.node(line).setFloat("count", static_cast<float>(count));
            network.node(line).setFloat("length", 10.0f);
            return line;
        }

        NodeId addPython(Network& network, const std::string& code, NodeId input) {
            const NodeId node = network.addNode("python");
            network.node(node).setString("code", code);
            if (input != jitnodes::invalidNodeId) {
                network.connect(input, node, 0);
            }
            return node;
        }

        std::string run(const std::string& code) {
            const auto result = curvytest::sharedConsole().run(code, nullptr);
            return result.ok ? result.output : "ERROR: " + result.error;
        }
    };

    std::vector<float> column(const jitnodes::Geometry& geometry, const char* name) {
        const jitnodes::AttribArray* attrib = geometry.findAttrib(jitnodes::AttribClass::point, name);
        std::vector<float> values;
        for (std::size_t i = 0; attrib != nullptr && i < attrib->size(); ++i) {
            values.push_back(attrib->at(i));
        }
        return values;
    }

}

TEST_F(PythonNodeTest, aPythonNodeIsRefusedUntilTheApplicationAllowsIt) {
    jitnodes::python::setPythonNodePolicy(nullptr);
    Network network;
    const NodeId node = addPython(network, "pass", addLine(network, 3));
    try {
        network.cook(node);
        FAIL() << "the node cooked without being allowed";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("not allowed"), std::string::npos) << error.what();
    }
}

TEST_F(PythonNodeTest, scriptsWriteThroughViewsAndTheInputStaysUntouched) {
    Network network;
    const NodeId line = addLine(network, 4);
    const NodeId node = addPython(network, "x = geo.point_attrib('x')\nx *= 2\ngeo.point_attrib('y')[:] = 7\n", line);

    const GeometryHandle before = network.cook(line);
    const std::vector<float> xBefore = column(*before, "x");
    const GeometryHandle result = network.cook(node);

    ASSERT_EQ(result->pointCount(), 4u);
    const std::vector<float> x = column(*result, "x");
    for (std::size_t i = 0; i < x.size(); ++i) {
        EXPECT_FLOAT_EQ(x[i], xBefore[i] * 2.0f);
    }
    for (float y : column(*result, "y")) {
        EXPECT_FLOAT_EQ(y, 7.0f);
    }
    EXPECT_EQ(column(*network.cook(line), "x"), xBefore) << "the cached input geometry must not change";
}

TEST_F(PythonNodeTest, aGeneratorWithoutInputsCanResizeAndAddAttributes) {
    Network network;
    const NodeId node = addPython(network,
        "import jitnodes\n"
        "geo.resize_points(5)\n"
        "geo.add_attrib(jitnodes.AttribClass.point, 'alpha')\n"
        "a = geo.point_attrib('alpha')\n"
        "a[:] = [1, 2, 3, 4, 5]\n"
        "geo.set_detail('n', params['a'])\n",
        jitnodes::invalidNodeId);
    network.node(node).setFloat("a", 9.0f);

    const GeometryHandle result = network.cook(node);
    EXPECT_EQ(result->pointCount(), 5u);
    EXPECT_EQ(column(*result, "alpha"), (std::vector<float>{1, 2, 3, 4, 5}));
    EXPECT_FLOAT_EQ(result->detail("n"), 9.0f);
}

TEST_F(PythonNodeTest, primitivesWithoutAKeyStayAnonymousUnlessTheNetworkStampsThem) {
    Network network;
    const NodeId node = addPython(network, "geo.resize_points(4)\ngeo.add_primitive(0, 2)\ngeo.add_primitive(2, 2)\n", jitnodes::invalidNodeId);
    const GeometryHandle plain = network.cook(node);
    ASSERT_EQ(plain->primitiveCount(), 2u);
    EXPECT_FALSE(plain->hasShapeKeys()) << "stamping is opt-in: geometry that never gets keys allocates nothing";
}

TEST_F(PythonNodeTest, aStampingNetworkGivesEveryUnkeyedPrimitiveItsOwnStableKey) {
    Network network;
    network.setStampAnonymousShapes(true);
    const NodeId node = addPython(network, "geo.resize_points(4)\ngeo.add_primitive(0, 2)\ngeo.add_primitive(2, 2)\n", jitnodes::invalidNodeId);
    const GeometryHandle first = network.cook(node);
    ASSERT_EQ(first->primitiveCount(), 2u);
    const std::string key0 = first->shapeKey(0), key1 = first->shapeKey(1);
    EXPECT_FALSE(key0.empty());
    EXPECT_FALSE(key1.empty());
    EXPECT_NE(key0, key1) << "each primitive is a shape of its own";
    EXPECT_EQ(first->shapes().size(), 2u);
    EXPECT_NO_THROW(first->checkShapes());

    network.node(node).setString("code", "geo.resize_points(4)\ngeo.add_primitive(0, 2)\ngeo.add_primitive(2, 2)\n# recook\n");
    const GeometryHandle again = network.cook(node);
    EXPECT_EQ(again->shapeKey(0), key0) << "a recook keeps the keys, so a selection survives it";
    EXPECT_EQ(again->shapeKey(1), key1);
}

TEST_F(PythonNodeTest, aKeyTheScriptGaveIsKeptAndOnlyTheOthersAreStamped) {
    Network network;
    network.setStampAnonymousShapes(true);
    const NodeId node = addPython(network, "geo.resize_points(4)\ngeo.add_primitive(0, 2, key='mine')\ngeo.add_primitive(2, 2)\n", jitnodes::invalidNodeId);
    const GeometryHandle result = network.cook(node);
    EXPECT_EQ(result->shapeKey(0), "mine");
    EXPECT_FALSE(result->shapeKey(1).empty());
    EXPECT_NE(result->shapeKey(1), "mine");
}

TEST_F(PythonNodeTest, theDocumentNetworkStamps) {
    curvy::SketchDocument document;
    EXPECT_TRUE(document.network().stampAnonymousShapes());
}

TEST_F(PythonNodeTest, resizingWhileAViewIsAliveIsRefusedAndAfterDelItWorks) {
    Network network;
    const NodeId bad = addPython(network, "x = geo.point_attrib('x')\ngeo.resize_points(10)\n", addLine(network, 3));
    try {
        network.cook(bad);
        FAIL() << "resize_points moved storage under a live view";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("still alive"), std::string::npos) << error.what();
    }

    const NodeId good = addPython(network, "x = geo.point_attrib('x')\nx[:] = 1\ndel x\ngeo.resize_points(10)\n", addLine(network, 3));
    EXPECT_EQ(network.cook(good)->pointCount(), 10u);
}

TEST_F(PythonNodeTest, aViewTheScriptKeepsCannotChangeTheCachedResult) {
    Network network;
    const NodeId node = addPython(network, "import builtins\nbuiltins.keptSlice = geo.point_attrib('x')[1:]\nbuiltins.kept = geo.point_attrib('x')\n",
                                  addLine(network, 4));
    const GeometryHandle result = network.cook(node);
    const std::vector<float> before = column(*result, "x");

    EXPECT_NE(run("import builtins\nbuiltins.kept[0] = 99").find("ERROR"), std::string::npos) << "the view itself is read-only after the cook";
    EXPECT_EQ(run("import builtins\nbuiltins.keptSlice[:] = 99\nprint('ok')"), "ok\n");
    EXPECT_EQ(column(*result, "x"), before) << "a writable slice still reached the cached geometry";
    EXPECT_EQ(column(*network.cook(node), "x"), before);
    run("import builtins\ndel builtins.kept\ndel builtins.keptSlice");
}

TEST_F(PythonNodeTest, aScriptErrorReachesTheCookWithTheTraceback) {
    Network network;
    const NodeId node = addPython(network, "geo.point_attrib('nope')", addLine(network, 2));
    try {
        network.cook(node);
        FAIL() << "the error was swallowed";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("no point attribute 'nope'"), std::string::npos) << error.what();
    }
}

TEST_F(PythonNodeTest, leavingNothingInGeoIsReported) {
    Network network;
    const NodeId node = addPython(network, "geo = 5", addLine(network, 2));
    EXPECT_THROW(network.cook(node), std::exception);
}

TEST_F(PythonNodeTest, severalPythonNodesCookInParallelWhileTheCallerHoldsTheInterpreter) {
    Network network;
    const NodeId line = addLine(network, 1000);
    const NodeId merge = network.addNode("merge");
    for (int i = 0; i < 6; ++i) {
        const NodeId node = addPython(network, "x = geo.point_attrib('x')\nx += " + std::to_string(i) + "\n", line);
        network.connect(node, merge, i);
    }
    const GeometryHandle result = network.cook(merge);
    EXPECT_EQ(result->pointCount(), 6000u);
}

TEST_F(PythonNodeTest, aScriptAddsPrimitivesOverItsPointsAndReadsThemBack) {
    Network network;
    const NodeId node = addPython(network,
        "import jitnodes\n"
        "geo.resize_points(9)\n"
        "geo.add_primitive(0, 3)\n"
        "geo.add_primitive(3, 6, closed=True, kind=jitnodes.PrimKind.cubic)\n"
        "assert geo.primitive_count() == 2\n"
        "assert geo.primitive(1) == (3, 6, True, jitnodes.PrimKind.cubic)\n"
        "assert len(geo.primitives()) == 2\n",
        jitnodes::invalidNodeId);
    const GeometryHandle result = network.cook(node);
    ASSERT_EQ(result->primitiveCount(), 2u);
    EXPECT_EQ(result->primitives()[1].start, 3u);
    EXPECT_TRUE(result->primitives()[1].closed);
    EXPECT_EQ(result->primitives()[1].kind, jitnodes::PrimKind::cubic);
}

TEST_F(PythonNodeTest, primitiveAttributesGrowWithThePrimitivesAndAddingIsRefusedWhileAnArrayIsAlive) {
    Network network;
    const NodeId good = addPython(network,
        "import jitnodes\n"
        "geo.resize_points(4)\n"
        "geo.add_attrib(jitnodes.AttribClass.primitive, 'w')\n"
        "geo.add_primitive(0, 2)\n"
        "geo.add_primitive(2, 2)\n"
        "w = geo.prim_attrib('w')\n"
        "assert len(w) == 2\n"
        "w[:] = [5, 6]\n",
        jitnodes::invalidNodeId);
    const GeometryHandle result = network.cook(good);
    const jitnodes::AttribArray* w = result->findAttrib(jitnodes::AttribClass::primitive, "w");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->size(), 2u);
    EXPECT_FLOAT_EQ(w->at(1), 6.0f);

    const NodeId bad = addPython(network,
        "import jitnodes\n"
        "geo.resize_points(4)\n"
        "geo.add_attrib(jitnodes.AttribClass.primitive, 'w')\n"
        "w = geo.prim_attrib('w')\n"
        "geo.add_primitive(0, 2)\n",
        jitnodes::invalidNodeId);
    try {
        network.cook(bad);
        FAIL() << "a primitive was added under a live prim_attrib array";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("still alive"), std::string::npos) << error.what();
    }
}

TEST_F(PythonNodeTest, badPrimitivesAreRefusedWhenAddedAndWhenThePointsLaterShrink) {
    Network network;
    for (const char* script : {"geo.resize_points(3)\ngeo.add_primitive(1, 5)",
                               "geo.resize_points(3)\ngeo.add_primitive(0, 0)",
                               "import jitnodes\ngeo.resize_points(5)\ngeo.add_primitive(0, 5, kind=jitnodes.PrimKind.cubic)",
                               "geo.resize_points(5)\ngeo.add_primitive(0, 5)\ngeo.resize_points(2)"}) {
        const NodeId node = addPython(network, script, jitnodes::invalidNodeId);
        EXPECT_THROW(network.cook(node), std::exception) << script;
    }
}

TEST_F(PythonNodeTest, shapeKeysGroupPrimitivesIntoShapesAndABrokenShapeFailsTheCook) {
    Network network;
    const NodeId good = addPython(network,
        "geo.resize_points(6)\n"
        "geo.add_primitive(0, 3, key='outer')\n"
        "geo.add_primitive(3, 3, key='outer')\n"
        "geo.add_primitive(0, 2)\n"
        "assert geo.shape_key(0) == 'outer' and geo.shape_key(2) == ''\n"
        "geo.set_shape_key(2, 'inner')\n",
        jitnodes::invalidNodeId);
    const GeometryHandle result = network.cook(good);
    EXPECT_EQ(result->shapeKey(1), "outer");
    EXPECT_EQ(result->shapeKey(2), "inner");
    EXPECT_EQ(result->shapes().size(), 2u);

    const NodeId split = addPython(network,
        "geo.resize_points(6)\n"
        "geo.add_primitive(0, 2, key='a')\n"
        "geo.add_primitive(2, 2, key='b')\n"
        "geo.add_primitive(4, 2, key='a')\n",
        jitnodes::invalidNodeId);
    try {
        network.cook(split);
        FAIL() << "a shape split in two was accepted";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("comes back"), std::string::npos) << error.what();
    }
}

TEST_F(PythonNodeTest, stringAttributesAreSetOneAtATimeOrAllAtOnceAndReadBack) {
    Network network;
    const NodeId node = addPython(network,
        "import jitnodes\n"
        "P = jitnodes.AttribClass.point\n"
        "geo.resize_points(3)\n"
        "geo.add_attrib(P, 'name', jitnodes.AttribType.string)\n"
        "geo.set_string_values(P, 'name', ['a', 'b', 'a'])\n"
        "geo.set_string(P, 'name', 1, 'zed')\n"
        "assert geo.string_values(P, 'name') == ['a', 'zed', 'a']\n"
        "assert {'a', 'zed'} <= set(geo.strings(P, 'name')) and geo.strings(P, 'name')[0] == ''\n"
        "ids = geo.point_attrib('name')\n"
        "assert ids[0] == ids[2] and ids[0] != ids[1]\n"
        "del ids\n",
        jitnodes::invalidNodeId);
    const GeometryHandle result = network.cook(node);
    const jitnodes::AttribArray* names = result->findAttrib(jitnodes::AttribClass::point, "name");
    ASSERT_NE(names, nullptr);
    EXPECT_EQ(names->stringAt(1), "zed");
    EXPECT_EQ(names->stringAt(2), "a");
}

TEST_F(PythonNodeTest, stringSettersCheckTheAttributeTheIndexAndTheLength) {
    Network network;
    for (const char* script : {"import jitnodes\ngeo.resize_points(2)\ngeo.set_string(jitnodes.AttribClass.point, 'nope', 0, 'x')",
                               "import jitnodes\ngeo.resize_points(2)\ngeo.set_string(jitnodes.AttribClass.point, 'x', 0, 'x')",
                               "import jitnodes\nP = jitnodes.AttribClass.point\ngeo.resize_points(2)\ngeo.add_attrib(P, 's', jitnodes.AttribType.string)\ngeo.set_string(P, 's', 2, 'x')",
                               "import jitnodes\nP = jitnodes.AttribClass.point\ngeo.resize_points(2)\ngeo.add_attrib(P, 's', jitnodes.AttribType.string)\ngeo.set_string_values(P, 's', ['only one'])"}) {
        const NodeId node = addPython(network, script, jitnodes::invalidNodeId);
        EXPECT_THROW(network.cook(node), std::exception) << script;
    }
}

namespace {

    using Clock = std::chrono::steady_clock;

    double secondsSince(Clock::time_point start) {
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

}

TEST_F(PythonNodeTest, aNodeThatRunsPastTheTimeLimitIsStoppedAndSaysSo) {
    jitnodes::python::setPythonNodeTimeLimit(std::chrono::milliseconds(300));
    Network network;
    const NodeId node = addPython(network, "while True:\n    pass\n", jitnodes::invalidNodeId);
    const Clock::time_point start = Clock::now();
    try {
        network.cook(node);
        FAIL() << "an endless loop was not stopped";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("ran longer than 0.3 seconds"), std::string::npos) << error.what();
    }
    EXPECT_LT(secondsSince(start), 5.0);
    EXPECT_EQ(run("x = 1\nprint(x)"), "1\n") << "no stray exception is left to fire in the next Python code";
}

TEST_F(PythonNodeTest, aScriptCannotSwallowTheStopWithExceptException) {
    jitnodes::python::setPythonNodeTimeLimit(std::chrono::milliseconds(300));
    Network network;
    const NodeId node = addPython(network, "while True:\n    try:\n        while True:\n            pass\n    except Exception:\n        pass\n",
                                  jitnodes::invalidNodeId);
    EXPECT_THROW(network.cook(node), std::exception);
}

TEST_F(PythonNodeTest, aStoppedNodeDoesNotRunAgainUntilItsCodeOrParametersChange) {
    jitnodes::python::setPythonNodeTimeLimit(std::chrono::milliseconds(200));
    Network network;
    const NodeId node = addPython(network, "while True:\n    pass\n", jitnodes::invalidNodeId);
    EXPECT_THROW(network.cook(node), std::exception);

    const Clock::time_point again = Clock::now();
    try {
        network.cook(node);
        FAIL() << "a stopped node ran again";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("change the code"), std::string::npos) << error.what();
    }
    EXPECT_LT(secondsSince(again), 0.15) << "it must refuse at once, not run to the limit again";

    network.node(node).setString("code", "geo.resize_points(2)");
    EXPECT_EQ(network.cook(node)->pointCount(), 2u);
}

TEST_F(PythonNodeTest, raisingTheLimitLetsAStoppedNodeRunAgain) {
    jitnodes::python::setPythonNodeTimeLimit(std::chrono::milliseconds(150));
    Network network;
    const NodeId node = addPython(network, "import time\nt = time.perf_counter()\nwhile time.perf_counter() - t < 0.4:\n    pass\ngeo.resize_points(1)\n",
                                  jitnodes::invalidNodeId);
    EXPECT_THROW(network.cook(node), std::exception);
    jitnodes::python::setPythonNodeTimeLimit(std::chrono::seconds(10));
    EXPECT_EQ(network.cook(node)->pointCount(), 1u);
}

TEST_F(PythonNodeTest, cancellingTheNetworkStopsARunningNodeAsACancelNotAFailure) {
    Network network;
    const NodeId node = addPython(network, "while True:\n    pass\n", jitnodes::invalidNodeId);
    std::thread canceller([&network]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        network.cancel();
    });
    const Clock::time_point start = Clock::now();
    EXPECT_THROW(network.cook(node), jitnodes::CookCancelled);
    canceller.join();
    EXPECT_LT(secondsSince(start), 5.0);

    jitnodes::python::setPythonNodeTimeLimit(std::chrono::seconds(10));
    const NodeId fine = addPython(network, "geo.resize_points(3)", jitnodes::invalidNodeId);
    EXPECT_EQ(network.cook(fine)->pointCount(), 3u) << "a cancel is not remembered against later cooks";
}

TEST_F(PythonNodeTest, theHostCanStopARunningNodeThroughTheInterruptCheck) {
    std::atomic<bool> stop{false};
    jitnodes::python::setPythonNodeInterruptCheck([&stop]() { return stop.load(); });
    Network network;
    const NodeId node = addPython(network, "while True:\n    pass\n", jitnodes::invalidNodeId);
    std::thread host([&stop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        stop = true;
    });
    try {
        network.cook(node);
        FAIL() << "the host's stop was ignored";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("Python node stopped"), std::string::npos) << error.what();
        EXPECT_EQ(std::string(error.what()).find("longer than"), std::string::npos) << "it was not the time limit";
    }
    host.join();
    jitnodes::python::setPythonNodeInterruptCheck(nullptr);
}

TEST_F(PythonNodeTest, aNodeThatFinishesInTimeIsNotTouchedByTheWatchdog) {
    jitnodes::python::setPythonNodeTimeLimit(std::chrono::milliseconds(2000));
    Network network;
    const NodeId node = addPython(network, "geo.resize_points(5)\n", jitnodes::invalidNodeId);
    for (int i = 0; i < 30; ++i) {
        network.node(node).setString("code", "geo.resize_points(" + std::to_string(i + 1) + ")");
        EXPECT_EQ(network.cook(node)->pointCount(), static_cast<size_t>(i + 1));
    }
    EXPECT_EQ(run("print(2)"), "2\n");
}

namespace {

    std::string sampleScript(const char* name) {
        const std::filesystem::path file = std::filesystem::path(__FILE__).parent_path().parent_path() / "Resources" / "samples" / "scripts" / name;
        std::ifstream in(file, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    class PythonNodeSampleTest : public PythonNodeTest {
    protected:
        void SetUp() override {
            PythonNodeTest::SetUp();
            document_.newBlank("Samples", 612.0f, 792.0f);
        }

        void runSample(const char* name) {
            const auto result = curvytest::sharedConsole().run(sampleScript(name), &document_);
            ASSERT_TRUE(result.ok) << name << ": " << result.error;
        }

        NodeId node(const char* name) {
            const NodeId id = document_.network().findNode(name);
            EXPECT_NE(id, jitnodes::invalidNodeId) << name;
            return id;
        }

        curvy::SketchDocument document_;
    };

}

TEST_F(PythonNodeSampleTest, theBasicsScriptStretchesItsInputWithNumPy) {
    runSample("pythonNodeBasics.py");
    Network& network = document_.network();
    const GeometryHandle ring = network.cook(node("ring"));
    const GeometryHandle stretched = network.cook(node("stretch"));
    ASSERT_EQ(stretched->pointCount(), ring->pointCount());
    const std::vector<float> before = column(*ring, "x");
    const std::vector<float> after = column(*stretched, "x");
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_NEAR(after[i], before[i] * 1.5f, 1e-3f);
    }
    const std::vector<float> yBefore = column(*ring, "y");
    const std::vector<float> yAfter = column(*stretched, "y");
    for (std::size_t i = 0; i < yBefore.size(); ++i) {
        EXPECT_NEAR(yAfter[i], yBefore[i] * 0.75f, 1e-3f);
    }
}

TEST_F(PythonNodeSampleTest, theShapesScriptBuildsAKeyedStarWithLabels) {
    runSample("pythonNodeShapes.py");
    const GeometryHandle star = document_.network().cook(node("star"));
    EXPECT_EQ(star->pointCount(), 10u);
    ASSERT_EQ(star->primitiveCount(), 1u);
    EXPECT_TRUE(star->primitives()[0].closed);
    EXPECT_EQ(star->shapeKey(0), "star");
    const jitnodes::AttribArray* label = star->findAttrib(jitnodes::AttribClass::point, "label");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->stringAt(0), "tip");
    EXPECT_EQ(label->stringAt(1), "dent");
    EXPECT_FLOAT_EQ(star->detail("points"), 10.0f);

    document_.network().node(node("star")).setFloat("a", 6.0f);
    EXPECT_EQ(document_.network().cook(node("star"))->pointCount(), 6u) << "changing a recooks it";
}

TEST_F(PythonNodeSampleTest, theErrorScriptFailsWithTheMessageThePanelShows) {
    runSample("pythonNodeError.py");
    try {
        document_.network().cook(node("broken"));
        FAIL() << "the node should fail";
    } catch (const std::exception& error) {
        EXPECT_EQ(std::string(error.what()).substr(0, std::string(error.what()).find('\n')), "ValueError: this node always fails");
    }
}

TEST_F(PythonNodeSampleTest, theRunawayScriptIsStoppedByTheTimeLimit) {
    runSample("pythonNodeRunaway.py");
    jitnodes::python::setPythonNodeTimeLimit(std::chrono::milliseconds(300));
    try {
        document_.network().cook(node("runaway"));
        FAIL() << "the loop was not stopped";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("ran longer than"), std::string::npos) << error.what();
    }
}

TEST_F(PythonNodeSampleTest, theSubnetScriptMakesASubnetThatTheAssetDialogCanTurnIntoAnAsset) {
    runSample("subnetForAssets.py");
    const curvy::NodeRef box(0, node("box"));
    auto* subnet = dynamic_cast<jitnodes::SubnetNode*>(&document_.node(box));
    ASSERT_NE(subnet, nullptr);
    EXPECT_EQ(subnet->network().nodeIds().size(), 2u);

    curvy::AssetPropertiesSession session = curvy::AssetPropertiesSession::fromSubnet(document_.network(), box.id, "sampleSubnetAsset");
    EXPECT_EQ(session.nodeCount(), 2u);
    EXPECT_EQ(session.problem(), "") << "the subnet the script makes must be accepted by + in the Assets panel";
    EXPECT_EQ(session.apply(), "");
    EXPECT_TRUE(jitnodes::AssetLibrary::instance().has("sampleSubnetAsset"));
    jitnodes::AssetLibrary::instance().uninstall("sampleSubnetAsset");
}

TEST_F(PythonNodeTest, aPythonNodeSurvivesSavingAndLoadingAGraphWithItsCodeAndParameters) {
    Network network;
    const NodeId node = addPython(network, "x = geo.point_attrib('x')\nx += params['a']\n", addLine(network, 3));
    network.node(node).setFloat("a", 4.5f);
    network.node(node).setString("code", "x = geo.point_attrib('x')\nx += params['a']  # \"quoted\" \ text\n");

    const std::string text = jitnodes::saveNetwork(network);
    Network loaded;
    jitnodes::loadNetwork(loaded, text);
    const NodeId again = loaded.findNode(network.node(node).name());
    ASSERT_NE(again, jitnodes::invalidNodeId);
    EXPECT_EQ(loaded.node(again).typeName(), "python");
    EXPECT_EQ(loaded.node(again).getString("code"), network.node(node).getString("code"));
    EXPECT_FLOAT_EQ(loaded.node(again).baseFloat("a"), 4.5f);

    const std::vector<float> before = column(*network.cook(node), "x");
    EXPECT_EQ(column(*loaded.cook(again), "x"), before);
}
