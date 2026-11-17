#include "curvy/wranglecheck.h"

#include <gtest/gtest.h>

namespace {

    curvy::SyntaxProblem problemOf(const std::string& code) {
        const auto problem = curvy::checkWrangle(code);
        EXPECT_TRUE(problem.has_value()) << code;
        return problem.value_or(curvy::SyntaxProblem());
    }

}

TEST(WrangleCheck, programsThatParseHaveNoProblem) {
    EXPECT_FALSE(curvy::checkWrangle(""));
    EXPECT_FALSE(curvy::checkWrangle("# only a comment"));
    EXPECT_FALSE(curvy::checkWrangle("x = x * 2; y = y + (x > a ? b : 0);"));
    EXPECT_FALSE(curvy::checkWrangle("# wave\n| amount = 0.35(0, 2), speed = 3, steps(1,), ratio(,1), plain |\nx = x + sin(y * 2 + speed) * amount; # done\n"));
    EXPECT_FALSE(curvy::checkWrangle("param = 3;"));   // an attribute that happens to be called param
    EXPECT_FALSE(curvy::checkWrangle("x = a || b;"));
}

TEST(WrangleCheck, theProblemSaysWhereAndWhat) {
    const curvy::SyntaxProblem missing = problemOf("x = 1");
    EXPECT_EQ(missing.line, 1);
    EXPECT_EQ(missing.column, 6);   // just after the last character
    EXPECT_EQ(missing.message, "expected ';'");

    const curvy::SyntaxProblem stray = problemOf("x = 1 % 2;");
    EXPECT_EQ(stray.line, 1);
    EXPECT_EQ(stray.column, 7);   // the '%'
    EXPECT_EQ(stray.message, "expected ';'");
}

TEST(WrangleCheck, linesCountCommentsAndBlankLines) {
    const curvy::SyntaxProblem problem = problemOf("# a comment\n\nx = 1;\ny = (1 + ;\n");
    EXPECT_EQ(problem.line, 4);
    EXPECT_EQ(problem.column, 10);
    EXPECT_EQ(problem.endLine, 4);
}

TEST(WrangleCheck, theColumnCountsCharactersNotBytes) {
    // The accented letter takes two bytes in UTF-8 but one column; the parser's offset counts bytes.
    const curvy::SyntaxProblem problem = problemOf("lab = label == \"\xC3\xA9\" +;");
    EXPECT_EQ(problem.line, 1);
    EXPECT_EQ(problem.column, 20);
    EXPECT_EQ(problem.message, "expected ';'");
}

TEST(WrangleCheck, theErrorsOfTheLanguageReference) {
    EXPECT_EQ(problemOf("; x = 1;").message, "expected assignment target");
    EXPECT_EQ(problemOf("x 1;").message, "expected '='");
    EXPECT_EQ(problemOf("a = 1;").message, "cannot assign to parameter 'a'");
    EXPECT_EQ(problemOf("x = $;").message, "unexpected character '$'");
    EXPECT_EQ(problemOf("y = x +").message, "unexpected end of input");
    EXPECT_EQ(problemOf("y = foo(1);").message, "unknown function 'foo'");
    EXPECT_EQ(problemOf("y = max(1);").message, "max() expects 2 argument(s)");
    EXPECT_EQ(problemOf("y = (1 + 2;").message, "expected ')'");
    EXPECT_EQ(problemOf("y = 1 < x < 3;").message, "expected ';'");
    EXPECT_EQ(problemOf("// not a comment\nx = 1;").message, "expected assignment target");   // only # starts a comment
}

TEST(WrangleCheck, parameterDeclarationsAreChecked) {
    EXPECT_EQ(problemOf("| g = 1 |\ng = 2;").message, "cannot assign to parameter 'g'");
    EXPECT_EQ(problemOf("| a |").message, "cannot declare parameter 'a'");
    EXPECT_EQ(problemOf("| g(5, 2) |").message, "the limits of 'g' have the minimum above the maximum");
    EXPECT_EQ(problemOf("| |").message, "expected a parameter name");
    EXPECT_EQ(problemOf("| g").message, "expected '|'");

    const curvy::SyntaxProblem late = problemOf("x = 1;\n| g |");
    EXPECT_EQ(late.message, "declarations must come before the first assignment");
    EXPECT_EQ(late.line, 2);
    EXPECT_EQ(late.column, 1);
}

TEST(WrangleCheck, theFunctionListMatchesTheParser) {
    // Every listed function must parse with its arity, and one more argument must not.
    for (const curvy::WrangleFunction& function : curvy::wrangleFunctions()) {
        std::string call = std::string("y = ") + function.name + "(";
        for (int i = 0; i < function.arity; ++i) {
            call += i == 0 ? "x" : ", x";
        }
        EXPECT_FALSE(curvy::checkWrangle(call + ");")) << function.name;
        EXPECT_TRUE(curvy::checkWrangle(call + ", x);").has_value()) << function.name;
    }
    EXPECT_EQ(curvy::wrangleFunctions().size(), 13u);
}
