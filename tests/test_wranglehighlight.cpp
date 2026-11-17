#include "curvy/wranglehighlight.h"

#include <gtest/gtest.h>

namespace {

    // The names of the styles covering the character at `offset` in order ("" when none): a problem comes after the colour.
    std::vector<std::string> stylesAt(const std::vector<newui::text::TextStyleRange>& ranges, std::size_t offset) {
        std::vector<std::string> found;
        for (const auto& range : ranges) {
            if (offset >= range.start && offset < range.start + range.length) {
                found.push_back(range.style);
            }
        }
        return found;
    }

    std::vector<std::string> stylesOf(const std::wstring& text, const std::wstring& word, std::size_t occurrence = 0) {
        std::size_t at = 0;
        for (std::size_t i = 0; i <= occurrence; ++i) {
            at = text.find(word, i == 0 ? 0 : at + 1);
            if (at == std::wstring::npos) {
                return {"<not found>"};
            }
        }
        return stylesAt(curvy::wrangleStyleRanges(text), at);
    }

    std::string styleOf(const std::wstring& text, const std::wstring& word, std::size_t occurrence = 0) {
        const std::vector<std::string> styles = stylesOf(text, word, occurrence);
        return styles.empty() ? std::string() : styles.front();
    }

    bool hasProblem(const std::wstring& text, const std::wstring& word) {
        const std::vector<std::string> styles = stylesOf(text, word);
        return std::find(styles.begin(), styles.end(), "problem") != styles.end();
    }

}

TEST(WrangleHighlight, aCommentOnItsOwnLineAndAfterAStatement) {
    const std::wstring text = L"# double it\nx = x * 2.5 + a; # tail\ny = x >= 1e3 ? 1 : 0;\n";
    EXPECT_EQ(styleOf(text, L"# double it"), "comment");
    EXPECT_EQ(styleOf(text, L"double"), "comment");   // words in a comment are not names
    EXPECT_EQ(styleOf(text, L"# tail"), "comment");
    EXPECT_EQ(styleOf(text, L"1e3"), "number");
}

TEST(WrangleHighlight, aCommentRunsToTheEndOfTheLineOnly) {
    const std::wstring text = L"x = 1; # note\ny = 2;";
    EXPECT_EQ(styleOf(text, L"# note"), "comment");
    EXPECT_EQ(styleOf(text, L"y"), "identifier");
    EXPECT_EQ(styleOf(text, L"2"), "number");
}

TEST(WrangleHighlight, tokensOfAnAssignment) {
    const std::wstring text = L"x = x * 2.5 + a;";
    EXPECT_EQ(styleOf(text, L"x"), "identifier");
    EXPECT_EQ(styleOf(text, L"="), "operator");
    EXPECT_EQ(styleOf(text, L"*"), "operator");
    EXPECT_EQ(styleOf(text, L"2.5"), "number");
    EXPECT_EQ(styleOf(text, L"a"), "constant");
    EXPECT_EQ(styleOf(text, L";"), "punctuation");
}

TEST(WrangleHighlight, doubledOperatorsAreOneToken) {
    const std::wstring text = L"k = x >= 1 && x != 3 || !x;";
    const auto ranges = curvy::wrangleStyleRanges(text);
    const auto covering = [&](std::size_t at) {
        for (const auto& range : ranges) {
            if (at >= range.start && at < range.start + range.length) {
                return range;
            }
        }
        return newui::text::TextStyleRange{};
    };
    EXPECT_EQ(covering(text.find(L">=")).length, 2u);
    EXPECT_EQ(covering(text.find(L"&&")).length, 2u);
    EXPECT_EQ(covering(text.find(L"!=")).length, 2u);
    EXPECT_EQ(covering(text.find(L"||")).length, 2u);
    EXPECT_EQ(styleOf(text, L"!x"), "operator");
}

TEST(WrangleHighlight, functionsAreColouredWhenCalled) {
    const std::wstring text = L"y = lerp(a, b, clamp(x, 0, 1)); sin = 2;";
    EXPECT_EQ(styleOf(text, L"lerp"), "definition");
    EXPECT_EQ(styleOf(text, L"clamp"), "definition");
    EXPECT_EQ(styleOf(text, L"sin"), "identifier");   // not a call: an attribute that shares the name
    EXPECT_FALSE(hasProblem(text, L"lerp"));
}

TEST(WrangleHighlight, aCallOfAnUnknownFunctionIsMarked) {
    EXPECT_TRUE(hasProblem(L"y = foo(1);", L"foo"));
    EXPECT_EQ(styleOf(L"y = foo(1);", L"foo"), "definition");
}

TEST(WrangleHighlight, dottedNamesAreOneNameAndParametersAreConstants) {
    const std::wstring text = L"N.x = Cd.y + group.top + sampleIndex + b;";
    const auto ranges = curvy::wrangleStyleRanges(text);
    const std::size_t at = text.find(L"N.x");
    EXPECT_EQ(styleOf(text, L"N.x"), "identifier");
    EXPECT_EQ(stylesAt(ranges, at + 2), stylesAt(ranges, at));   // the x of N.x is part of the same name
    EXPECT_EQ(styleOf(text, L"Cd.y"), "identifier");
    EXPECT_EQ(styleOf(text, L"sampleIndex"), "constant");
    EXPECT_EQ(styleOf(text, L"b"), "constant");
}

TEST(WrangleHighlight, declarationsBetweenBarsAreColouredAsParameters) {
    const std::wstring text = L"| gain = 1.5(0, 10), count(1,), plain |\nx = x * gain + count;\ny = a || b;\n";
    EXPECT_EQ(styleOf(text, L"|"), "keyword");        // the opening bar
    EXPECT_EQ(styleOf(text, L"|", 1), "keyword");     // the closing bar
    EXPECT_EQ(styleOf(text, L"||"), "operator");      // an or is still an or
    EXPECT_EQ(styleOf(text, L"gain"), "constant");    // the declared name
    EXPECT_EQ(styleOf(text, L"gain", 1), "constant"); // and its later uses
    EXPECT_EQ(styleOf(text, L"count"), "constant");   // not mistaken for a call of a function named count
    EXPECT_FALSE(hasProblem(text, L"count"));
    EXPECT_EQ(styleOf(text, L"plain"), "constant");
    EXPECT_EQ(styleOf(text, L"1.5"), "number");
    EXPECT_EQ(styleOf(text, L"10"), "number");
    EXPECT_EQ(styleOf(text, L"x"), "identifier");
}

TEST(WrangleHighlight, aCommentMayComeInsideTheBars) {
    const std::wstring text = L"# first\n| # tweak\n  speed = 3 |\nx = speed;";
    EXPECT_EQ(styleOf(text, L"|"), "keyword");
    EXPECT_EQ(styleOf(text, L"speed"), "constant");
    EXPECT_EQ(styleOf(text, L"speed", 1), "constant");
}

TEST(WrangleHighlight, aBarAfterTheFirstAssignmentIsNotADeclaration) {
    const std::wstring text = L"x = 1;\n| g |\n";
    EXPECT_TRUE(hasProblem(text, L"|"));
    EXPECT_EQ(styleOf(text, L"g"), "identifier");
}

TEST(WrangleHighlight, paramIsAnOrdinaryName) {
    EXPECT_EQ(styleOf(L"param = 3;", L"param"), "identifier");
}

TEST(WrangleHighlight, stringLiteralsAreColouredAndKeepTheirHash) {
    const std::wstring text = L"t = label == \"a # b\"; # real comment";
    EXPECT_EQ(styleOf(text, L"\"a # b\""), "string");
    EXPECT_EQ(styleOf(text, L"# b"), "string");
    EXPECT_EQ(styleOf(text, L"# real comment"), "comment");
    EXPECT_FALSE(hasProblem(text, L"\"a # b\""));
}

TEST(WrangleHighlight, anUnterminatedStringIsMarkedAndEndsAtTheLine) {
    const std::wstring text = L"t = label == \"oops;\nx = 1;";
    EXPECT_TRUE(hasProblem(text, L"\"oops;"));
    EXPECT_EQ(styleOf(text, L"x"), "identifier");
    EXPECT_EQ(styleOf(text, L"1"), "number");
}

TEST(WrangleHighlight, whatTheParserRejectsIsMarked) {
    EXPECT_TRUE(hasProblem(L"x = 1.5f;", L"f"));     // a suffix
    EXPECT_EQ(styleOf(L"x = 1.5f;", L"1.5"), "number");
    EXPECT_TRUE(hasProblem(L"x = 5 % 2;", L"%"));
    EXPECT_TRUE(hasProblem(L"x = a & b;", L"&"));
    EXPECT_TRUE(hasProblem(L"x = a | b;", L"|"));
    EXPECT_TRUE(hasProblem(L"x = [1];", L"["));
    EXPECT_TRUE(hasProblem(L"x = 1 + .;", L"."));   // a lone dot is not a number
    EXPECT_FALSE(hasProblem(L"x = 1 + .5;", L".5"));
}

TEST(WrangleHighlight, numbersInTheirForms) {
    for (const wchar_t* number : {L"3", L"0.5", L".5", L"5.", L"1e3", L"2.5e-1", L"1E+2"}) {
        const std::wstring text = std::wstring(L"x = ") + number + L";";
        EXPECT_EQ(styleOf(text, number), "number") << std::wstring(number).size();
        EXPECT_FALSE(hasProblem(text, number));
    }
}

TEST(WrangleHighlight, emptyTextHasNothing) {
    EXPECT_TRUE(curvy::wrangleStyleRanges(L"").empty());
}

TEST(WrangleHighlight, theLanguageUsesTheSharedStyleNamesAndKeepsIndentation) {
    const auto language = curvy::WrangleLanguage::instance();
    const auto sheet = language->styleSheet(false, {});
    ASSERT_TRUE(sheet != nullptr);
    EXPECT_TRUE(sheet->style("keyword") != nullptr);
    EXPECT_TRUE(sheet->style("problem") != nullptr);
    EXPECT_EQ(language->newLineIndent(L"    x = 1;", 4), L"    ");
    EXPECT_TRUE(language->folds(L"x = 1;\ny = 2;").empty());
    EXPECT_FALSE(language->styleRanges(L"x = 1;").empty());
}
