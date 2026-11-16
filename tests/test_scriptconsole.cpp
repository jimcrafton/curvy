#include "pythonconsole.h"

#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>

namespace {

    curvy::ScriptConsole& console() {
        return curvytest::sharedConsole();
    }

}

TEST(ScriptConsole, checkSyntaxNamesTheFirstErrorWithoutRunningAnything) {
    EXPECT_TRUE(console().run("syntaxProbe = 1", nullptr).ok);
    EXPECT_FALSE(console().checkSyntax("x = 1\nfor i in range(3):\n    print(i)\n").has_value());

    const auto problem = console().checkSyntax("syntaxProbe = 2\nx = (1,\ny = 3 +\n");
    ASSERT_TRUE(problem.has_value());
    EXPECT_GE(problem->line, 2);
    EXPECT_FALSE(problem->message.empty());
    EXPECT_EQ(console().run("print(syntaxProbe)", nullptr).output, "1\n") << "nothing of the checked code ran";

    const auto indent = console().checkSyntax("def f():\nreturn 1\n");
    ASSERT_TRUE(indent.has_value());
    EXPECT_EQ(indent->line, 2);
    EXPECT_NE(indent->message.find("indent"), std::string::npos) << indent->message;

    const auto text = console().checkSyntax("s = 'unterminated\n");
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(text->line, 1);
    EXPECT_GE(text->column, 5);
}

TEST(ScriptConsole, runsStatementsAndCapturesPrint) {
    auto result = console().run("print('hello')", nullptr);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "hello\n");
}

TEST(ScriptConsole, anExpressionPrintsItsValueAndNamesPersist) {
    EXPECT_TRUE(console().run("answer = 6 * 7", nullptr).ok);
    auto result = console().run("answer", nullptr);
    EXPECT_EQ(result.output, "42\n");
}

TEST(ScriptConsole, aScriptErrorIsReportedNotThrown) {
    auto result = console().run("1 / 0", nullptr);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("ZeroDivisionError"), std::string::npos) << result.error;
    EXPECT_TRUE(console().run("print('still alive')", nullptr).ok);
}

TEST(ScriptConsole, readsAndSetsThePage) {
    curvy::SketchDocument document;
    document.newBlank("Script", 612.0f, 792.0f);
    auto result = console().run("doc.set_page_size(100, 200)\nprint(doc.page_width, doc.page_height, doc.name)", &document);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "100.0 200.0 Script\n");
    EXPECT_FLOAT_EQ(document.pageWidth(), 100.0f);
}

TEST(ScriptConsole, graphEditsGoThroughTheUndoStack) {
    curvy::SketchDocument document;
    document.newBlank("Script", 612.0f, 792.0f);
    auto result = console().run("c = doc.add_node('circle', 'ring')\nprint(c.name())", &document);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "ring\n");
    EXPECT_TRUE(document.undoStack().canUndo());
    result = console().run("doc.undo()\nprint(c.valid)", &document);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "False\n");
}

TEST(ScriptConsole, aNodeFromAnotherNetworkIsRefused) {
    curvy::SketchDocument first;
    first.newBlank("A", 10.0f, 10.0f);
    curvy::SketchDocument second;
    second.newBlank("B", 10.0f, 10.0f);
    EXPECT_TRUE(console().run("other = doc.add_node('circle')", &first).ok);
    auto result = console().run("doc.set_param(other, 'radius', 3)", &second);
    EXPECT_FALSE(result.ok);
}

TEST(ScriptConsole, aClosedDocumentRaisesInsteadOfCrashing) {
    {
        curvy::SketchDocument document;
        document.newBlank("Gone", 10.0f, 10.0f);
        EXPECT_TRUE(console().run("kept = doc", &document).ok);
    }
    auto result = console().run("kept.name", nullptr);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("closed"), std::string::npos) << result.error;
}

namespace {

    // Runs `code`, and cancels it from another thread after `delayMs`.
    curvy::ScriptResult runAndCancel(const std::string& code, curvy::SketchDocument* document, int delayMs = 150) {
        std::thread canceller([delayMs] {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            console().cancel();
        });
        curvy::ScriptResult result = console().run(code, document);
        canceller.join();
        return result;
    }

}

TEST(ScriptConsole, cancelStopsALoopThatNeverEnds) {
    auto result = runAndCancel("while True:\n    pass", nullptr);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.cancelled);
    EXPECT_NE(result.error.find("cancelled"), std::string::npos) << result.error;
    EXPECT_EQ(console().run("print('fine')", nullptr).output, "fine\n");
}

TEST(ScriptConsole, anExceptExceptionClauseCannotSwallowACancel) {
    auto result = runAndCancel("try:\n    while True:\n        pass\nexcept Exception:\n    print('swallowed')", nullptr);
    EXPECT_TRUE(result.cancelled);
    EXPECT_EQ(result.output.find("swallowed"), std::string::npos);
}

TEST(ScriptConsole, cancelWithNothingRunningDoesNotAffectTheNextRun) {
    console().cancel();
    auto result = console().run("total = sum(range(100000))\nprint(total)", nullptr);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_FALSE(result.cancelled);
}

TEST(ScriptConsole, editsBeforeTheCancelStayAsOneUndoStep) {
    curvy::SketchDocument document;
    document.newBlank("Cancel", 10.0f, 10.0f);
    auto result = runAndCancel("while True:\n    doc.add_layer()\n    if len(doc.layers) >= 3:\n        while True:\n            pass", &document);
    EXPECT_TRUE(result.cancelled);
    EXPECT_EQ(document.childrenOf().size(), 3u);
    EXPECT_EQ(document.undoStack().depth(), 1u);
    EXPECT_TRUE(console().run("doc.undo()\nprint(len(doc.layers))", &document).ok);
    EXPECT_EQ(document.childrenOf().size(), 0u);
}

TEST(ScriptConsole, findPythonHomePrefersTheBundledFolderNextToTheExe) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "curvyPythonHomeTest";
    fs::remove_all(root);
    fs::create_directories(root / "python" / "Lib");
    const fs::path development = "D:/dev/python";

    EXPECT_EQ(curvy::findPythonHome(root, development), development) << "a python folder without a standard library is not a bundle";
    std::ofstream(root / "python" / "Lib" / "os.py") << "";
    EXPECT_EQ(curvy::findPythonHome(root, development), root / "python");
    EXPECT_EQ(curvy::findPythonHome(root / "elsewhere", development), development);
    EXPECT_EQ(curvy::findPythonHome({}, development), development);
    fs::remove_all(root);
}

TEST(ScriptConsole, theInterpreterRunsFromTheFolderFindPythonHomeChose) {
    const std::filesystem::path home = curvy::findPythonHome(curvy::executableDirectory(), PYTHON_HOME_DIR);
    auto result = console().run("import sys\nprint(sys.prefix)\nprint(sys.flags.isolated)", nullptr);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(std::filesystem::path(result.output.substr(0, result.output.find('\n'))).lexically_normal(), home.lexically_normal()) << result.output;
}

TEST(ScriptConsole, numpyWorksInTheEmbeddedInterpreter) {
    auto result = console().run("import numpy\nprint(int(numpy.arange(10).sum()))", nullptr);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "45\n");
}

TEST(ScriptConsole, aLateCancelDoesNotHitTheNextRun) {
    for (int i = 0; i < 20; ++i) {
        auto result = runAndCancel("pass", nullptr, 0);
        EXPECT_TRUE(result.ok || result.cancelled);
        EXPECT_TRUE(console().run("x = 1", nullptr).ok);
    }
}

namespace {

    // What the application's run loop does for the console: tasks posted from the script's thread run on the test (main) thread when it
    // pumps.
    class MainQueue {
    public:
        void post(std::function<void()> task) {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back(std::move(task));
        }

        bool pump() {
            std::deque<std::function<void()>> batch;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                batch.swap(tasks_);
            }
            for (auto& task : batch) {
                task();
            }
            return !batch.empty();
        }

        template <typename Predicate>
        bool pumpUntil(Predicate finished, int limitMs = 10000) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limitMs);
            while (!finished()) {
                if (std::chrono::steady_clock::now() > end) {
                    return false;
                }
                if (!pump()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            return true;
        }

    private:
        std::mutex mutex_;
        std::deque<std::function<void()>> tasks_;
    };

    class BackgroundScript : public ::testing::Test {
    protected:
        void SetUp() override {
            console().setMainThreadPoster([this](std::function<void()> task) { queue_.post(std::move(task)); });
        }

        void TearDown() override {
            console().abort();
            console().setMainThreadPoster(nullptr);
        }

        curvy::ScriptResult runToEnd(const std::string& code, curvy::SketchDocument* document = nullptr) {
            curvy::ScriptResult result;
            bool finished = false;
            EXPECT_TRUE(console().runInBackground(code, document, nullptr, [&](const curvy::ScriptResult& r) {
                result = r;
                finished = true;
            }));
            EXPECT_TRUE(queue_.pumpUntil([&] { return finished; }));
            return result;
        }

        MainQueue queue_;
    };

}

TEST_F(BackgroundScript, theResultArrivesOnTheMainThroughThePoster) {
    EXPECT_TRUE(console().canRunInBackground());
    const auto result = runToEnd("print('hello')");
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "hello\n");
    EXPECT_EQ(runToEnd("6 * 7").output, "42\n");
    EXPECT_FALSE(console().running());
}

TEST_F(BackgroundScript, anErrorIsReportedAndTheNextRunWorks) {
    const auto result = runToEnd("1 / 0");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("ZeroDivisionError"), std::string::npos) << result.error;
    EXPECT_TRUE(runToEnd("print('fine')").ok);
}

TEST_F(BackgroundScript, documentCallsRunOnTheMainThreadAndAreOneUndoStep) {
    curvy::SketchDocument document;
    document.newBlank("Background", 100.0f, 100.0f);
    const auto result = runToEnd("for i in range(30):\n    doc.add_layer('l%d' % i)\nprint(len(doc.layers))", &document);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "30\n");
    EXPECT_EQ(document.childrenOf().size(), 30u);
    EXPECT_EQ(document.undoStack().depth(), 1u);
}

TEST_F(BackgroundScript, anErrorInsideACallComesBackAsThatError) {
    curvy::SketchDocument document;
    document.newBlank("Background", 100.0f, 100.0f);
    const auto result = runToEnd("try:\n    doc.set_page_size('x', 1)\nexcept TypeError:\n    print('type error')\ndoc.layers[0]", &document);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("IndexError"), std::string::npos) << result.error;
    EXPECT_EQ(result.output, "type error\n");
}

TEST_F(BackgroundScript, jitnodesNetworkCallsAreHandedOverToo) {
    curvy::SketchDocument document;
    document.newBlank("Background", 100.0f, 100.0f);
    const auto result = runToEnd("c = doc.add_node('circle', 'ring')\nprint(c.name(), len(list(network)) > 0)", &document);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.output, "ring True\n");
}

TEST_F(BackgroundScript, theMainThreadStaysFreeWhileTheScriptRuns) {
    bool finished = false;
    ASSERT_TRUE(console().runInBackground("import time\ntime.sleep(0.3)", nullptr, nullptr, [&](const curvy::ScriptResult&) { finished = true; }));
    EXPECT_TRUE(console().running());
    int turns = 0;
    while (!finished && turns < 10000) {
        queue_.pump();
        ++turns;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(finished);
    EXPECT_GT(turns, 20) << "the caller was blocked by the script";
}

TEST_F(BackgroundScript, outputAndProgressCanBeReadWhileItRuns) {
    bool finished = false;
    ASSERT_TRUE(console().runInBackground("curvy.progress(0.5, 'half way')\nprint('partial')\nimport time\ntime.sleep(0.5)", nullptr, nullptr,
                                          [&](const curvy::ScriptResult&) { finished = true; }));
    EXPECT_TRUE(queue_.pumpUntil([&] { return console().progress().fraction > 0.4f || finished; }));
    const curvy::ScriptProgress progress = console().progress();
    EXPECT_FLOAT_EQ(progress.fraction, 0.5f);
    EXPECT_EQ(progress.message, "half way");
    EXPECT_TRUE(queue_.pumpUntil([&] { return console().outputSoFar() == "partial\n" || finished; }));
    EXPECT_FALSE(finished);
    EXPECT_TRUE(queue_.pumpUntil([&] { return finished; }));
}

TEST_F(BackgroundScript, onlyOneScriptRunsAtATime) {
    bool finished = false;
    ASSERT_TRUE(console().runInBackground("import time\ntime.sleep(0.2)", nullptr, nullptr, [&](const curvy::ScriptResult&) { finished = true; }));
    EXPECT_FALSE(console().runInBackground("pass", nullptr, nullptr, [](const curvy::ScriptResult&) {}));
    EXPECT_FALSE(console().run("pass", nullptr).ok);
    EXPECT_TRUE(queue_.pumpUntil([&] { return finished; }));
}

TEST_F(BackgroundScript, cancelStopsABackgroundScript) {
    curvy::SketchDocument document;
    document.newBlank("Background", 100.0f, 100.0f);
    bool finished = false;
    curvy::ScriptResult result;
    ASSERT_TRUE(console().runInBackground("doc.add_layer('a')\nwhile True:\n    pass", &document, nullptr, [&](const curvy::ScriptResult& r) {
        result = r;
        finished = true;
    }));
    queue_.pumpUntil([&] { return document.childrenOf().size() == 1u; });
    console().cancel();
    EXPECT_TRUE(queue_.pumpUntil([&] { return finished; }));
    EXPECT_TRUE(result.cancelled);
    EXPECT_EQ(document.undoStack().depth(), 1u);
}

TEST_F(BackgroundScript, abortStopsTheScriptAndSkipsTheResult) {
    curvy::SketchDocument document;
    document.newBlank("Background", 100.0f, 100.0f);
    bool finished = false;
    ASSERT_TRUE(console().runInBackground("while True:\n    doc.add_layer('x')", &document, nullptr, [&](const curvy::ScriptResult&) { finished = true; }));
    queue_.pumpUntil([&] { return document.childrenOf().size() >= 2u; });
    console().abort();
    EXPECT_FALSE(console().running());
    queue_.pump();
    EXPECT_FALSE(finished) << "a completion posted before the abort is stale";
    EXPECT_TRUE(runToEnd("print('again')").ok);
}

TEST_F(BackgroundScript, abortWhileTheMainThreadIsNotPumping) {
    curvy::SketchDocument document;
    document.newBlank("Background", 100.0f, 100.0f);
    ASSERT_TRUE(console().runInBackground("while True:\n    doc.add_layer('x')", &document, nullptr, [](const curvy::ScriptResult&) {}));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    console().abort();
    EXPECT_FALSE(console().running());
}
