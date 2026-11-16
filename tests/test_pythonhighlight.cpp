#include "curvy/pythonhighlight.h"

#include <gtest/gtest.h>

namespace {

    // The name of the last range covering the character at `offset` ("" when none).
    std::string styleAt(const std::vector<newui::text::TextStyleRange>& ranges, std::size_t offset) {
        std::string found;
        for (const auto& range : ranges) {
            if (offset >= range.start && offset < range.start + range.length) {
                found = range.style;
            }
        }
        return found;
    }

    std::string styleOf(const std::wstring& text, const std::wstring& word, std::size_t occurrence = 0) {
        std::size_t at = 0;
        for (std::size_t i = 0; i <= occurrence; ++i) {
            at = text.find(word, i == 0 ? 0 : at + 1);
            if (at == std::wstring::npos) {
                return "<not found>";
            }
        }
        return styleAt(curvy::pythonStyleRanges(text), at);
    }

}

TEST(PythonHighlight, keywordsNamesStringsNumbersAndComments) {
    const std::wstring text = L"def run(x):\n    return \"hi\" + 42  # note\n";
    EXPECT_EQ(styleOf(text, L"def"), "keyword");
    EXPECT_EQ(styleOf(text, L"run"), "definition");
    EXPECT_EQ(styleOf(text, L"x"), "identifier");
    EXPECT_EQ(styleOf(text, L"return"), "keyword");
    EXPECT_EQ(styleOf(text, L"\"hi\""), "string");
    EXPECT_EQ(styleOf(text, L"42"), "number");
    EXPECT_EQ(styleOf(text, L"# note"), "comment");
}

TEST(PythonHighlight, constantsDecoratorsAndClassNames) {
    const std::wstring text = L"@staticmethod\nclass Shape:\n    ok = True or None\n";
    EXPECT_EQ(styleOf(text, L"@staticmethod"), "decorator");
    EXPECT_EQ(styleOf(text, L"Shape"), "definition");
    EXPECT_EQ(styleOf(text, L"True"), "constant");
    EXPECT_EQ(styleOf(text, L"None"), "constant");
    EXPECT_EQ(styleOf(text, L"or"), "keyword");
}

TEST(PythonHighlight, aTripleQuotedStringCarriesAcrossLines) {
    const std::wstring text = L"s = \"\"\"first\nsecond def\n\"\"\"\nx = 1\n";
    EXPECT_EQ(styleOf(text, L"first"), "string");
    EXPECT_EQ(styleOf(text, L"second"), "string");
    EXPECT_EQ(styleOf(text, L"def"), "string");   // a word inside the string is not a keyword
    EXPECT_EQ(styleOf(text, L"x"), "identifier");
}

TEST(PythonHighlight, stringPrefixesAndEscapes) {
    const std::wstring text = L"a = rb'\\' q'\n";
    EXPECT_EQ(styleOf(text, L"rb"), "string");   // the prefix belongs to the string
    const std::wstring escaped = L"s = 'it\\'s' + tail\n";
    EXPECT_EQ(styleOf(escaped, L"s'"), "string");   // the escaped quote does not end it
    EXPECT_EQ(styleOf(escaped, L"tail"), "identifier");
}

TEST(PythonHighlight, aStringContinuedByABackslashKeepsGoing) {
    const std::wstring text = L"s = 'one \\\ntwo'\nx = 1\n";
    EXPECT_EQ(styleOf(text, L"two"), "string");
    EXPECT_EQ(styleOf(text, L"x"), "identifier");
}

TEST(PythonHighlight, anUnterminatedStringGetsASquiggleAndEndsAtTheLine) {
    const std::wstring text = L"s = 'oops\nx = 1\n";
    const auto ranges = curvy::pythonStyleRanges(text);
    bool problem = false;
    for (const auto& range : ranges) {
        problem = problem || range.style == "problem";
    }
    EXPECT_TRUE(problem);
    EXPECT_EQ(styleOf(text, L"x"), "identifier");
}

TEST(PythonHighlight, numbersInTheirForms) {
    const std::wstring text = L"a = 0x1F + 1_000 + 3.14e-2 + 2j + .5 + 0b101\n";
    for (const wchar_t* number : {L"0x1F", L"1_000", L"3.14e-2", L"2j", L".5", L"0b101"}) {
        EXPECT_EQ(styleOf(text, number), "number");
    }
}

TEST(PythonHighlight, emptyTextHasNothing) {
    EXPECT_TRUE(curvy::pythonStyleRanges(L"").empty());
    EXPECT_TRUE(curvy::pythonFolds(L"").empty());
}

TEST(PythonIndent, aColonAddsALevelAndOtherLinesKeepTheirs) {
    EXPECT_EQ(curvy::pythonNewLineIndent(L"def f():"), L"    ");
    EXPECT_EQ(curvy::pythonNewLineIndent(L"    if x:  # note"), L"        ");
    EXPECT_EQ(curvy::pythonNewLineIndent(L"    x = 1"), L"    ");
    EXPECT_EQ(curvy::pythonNewLineIndent(L"\tfoo:"), L"\t    ");
    EXPECT_EQ(curvy::pythonNewLineIndent(L""), L"");
}

TEST(PythonIndent, aColonInsideAStringOrCommentIsNotABlock) {
    EXPECT_EQ(curvy::pythonNewLineIndent(L"s = 'a:'"), L"");
    EXPECT_EQ(curvy::pythonNewLineIndent(L"x = 1  # why:"), L"");
    EXPECT_EQ(curvy::pythonNewLineIndent(L"d = {'a':"), L"");   // a dictionary entry, but it is brackets that continue it, not a block
}

TEST(PythonIndent, aLineThatLeavesTheBlockOutdentsTheNextOne) {
    EXPECT_EQ(curvy::pythonNewLineIndent(L"        return x"), L"    ");
    EXPECT_EQ(curvy::pythonNewLineIndent(L"    pass"), L"");
    EXPECT_EQ(curvy::pythonNewLineIndent(L"    raise ValueError()"), L"");
    EXPECT_EQ(curvy::pythonNewLineIndent(L"return"), L"");   // already at the left edge
    EXPECT_EQ(curvy::pythonNewLineIndent(L"    if x: return 1"), L"    ");   // does not start with return
}

TEST(PythonIndent, theIndentWidthIsTheCallers) {
    EXPECT_EQ(curvy::pythonNewLineIndent(L"def f():", 2), L"  ");
    EXPECT_EQ(curvy::pythonNewLineIndent(L"    if x:", 8), L"            ");
    EXPECT_EQ(curvy::pythonNewLineIndent(L"    return 1", 2), L"  ");
}

TEST(PythonStyles, everyListedSyntaxStyleHasAColourInBothLooks) {
    ASSERT_EQ(10u, curvy::pythonSyntaxStyles().size());
    for (const auto& style : curvy::pythonSyntaxStyles()) {
        EXPECT_NE(0u, curvy::pythonDefaultColor(style.name, true)) << style.name;   // none of the dark look's colours is black
    }
    EXPECT_NE(0u, curvy::pythonDefaultColor("keyword", false));
    EXPECT_EQ(0u, curvy::pythonDefaultColor("operator", false)) << "black is a real colour, so 0 does not mean 'no such style' in the light look";
    EXPECT_EQ(0u, curvy::pythonDefaultColor("nonsense", true));
    EXPECT_NE(curvy::pythonDefaultColor("background", true), curvy::pythonDefaultColor("background", false));
}

TEST(PythonStyles, theSheetHasTheLooksColoursExceptWhereTheUserChoseOne) {
    const auto plain = curvy::pythonStyleSheet(true);
    const auto custom = curvy::pythonStyleSheet(true, {{"keyword", 0xFF8800}});
    ASSERT_NE(nullptr, plain->style("keyword"));
    ASSERT_NE(nullptr, custom->style("keyword"));
    EXPECT_NE(plain->style("keyword")->color(), custom->style("keyword")->color());
    EXPECT_EQ(plain->style("string")->color(), custom->style("string")->color());
    EXPECT_NE(nullptr, custom->style("problem")) << "the squiggle style is always there";
}

TEST(PythonFolds, aBlockFoldsFromItsHeaderToItsLastLine) {
    const std::wstring text = L"def f(a):\n    if a:\n        return 1\n    return 2\n\nx = 1\n";
    const auto folds = curvy::pythonFolds(text);
    ASSERT_EQ(folds.size(), 2u);
    const std::size_t headerEnd = text.find(L"\n");
    EXPECT_EQ(folds[0].start, headerEnd);
    EXPECT_EQ(folds[0].end(), text.find(L"return 2") + 8);
    EXPECT_EQ(folds[1].start, text.find(L"if a:") + 5);
    EXPECT_EQ(folds[1].end(), text.find(L"return 1") + 8);
    EXPECT_FALSE(folds[0].collapsed);
}

TEST(PythonFolds, anElseStartsItsOwnBlock) {
    const std::wstring text = L"if a:\n    b\nelse:\n    c\n";
    EXPECT_EQ(curvy::pythonFolds(text).size(), 2u);
}

TEST(PythonFolds, aDocstringLineAtColumnZeroDoesNotEndTheBlock) {
    const std::wstring text = L"def f():\n    \"\"\"doc\ntext at column zero\n    \"\"\"\n    return 1\n";
    const auto folds = curvy::pythonFolds(text);
    ASSERT_EQ(folds.size(), 1u);
    EXPECT_EQ(folds[0].end(), text.find(L"return 1") + 8);
}

TEST(PythonFolds, bracketContinuationLinesAreNotBlocks) {
    const std::wstring text = L"x = foo(\n1,\n2)\ndef g():\n    pass\n";
    const auto folds = curvy::pythonFolds(text);
    ASSERT_EQ(folds.size(), 1u);
    EXPECT_EQ(folds[0].start, text.find(L"def g():") + 8);
}

TEST(PythonFolds, anEmptyOrOneLineBlockHasNoFold) {
    EXPECT_TRUE(curvy::pythonFolds(L"def f():\nx = 1\n").empty());
    EXPECT_TRUE(curvy::pythonFolds(L"if a: pass\n").empty());
}

TEST(PythonFolds, trailingBlankLinesAndCommentsAreOutsideTheBlock) {
    const std::wstring text = L"def f():\n    return 1\n\n# after\nx = 2\n";
    const auto folds = curvy::pythonFolds(text);
    ASSERT_EQ(folds.size(), 1u);
    EXPECT_EQ(folds[0].end(), text.find(L"return 1") + 8);
}
