#include "pythonconsole.h"

#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

    curvy::ScriptConsole& console() {
        return curvytest::sharedConsole();
    }

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
