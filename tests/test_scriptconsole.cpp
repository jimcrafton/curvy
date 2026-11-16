#include "pythonconsole.h"

#include "curvy/sketchdocument.h"

#include <gtest/gtest.h>

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
