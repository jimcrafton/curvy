#pragma once

#include "curvy/syntaxproblem.h"

#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // Compiles wrangle code (jitnodes::parseWrangle, with the parameters a, b, c, d and `param` declarations) without running it, and gives
    // the first problem as a line and column. Line and column are 1-based; the column counts characters, not bytes. The message is the
    // parser's without its "wrangle parse error at offset N:" prefix. Nothing when the code parses (an empty program does).
    std::optional<SyntaxProblem> checkWrangle(const std::string& code);

    // What the editor and the docs list of the language: the 13 functions and the number of arguments each takes.
    struct WrangleFunction {
        const char* name;
        int arity;
    };
    const std::vector<WrangleFunction>& wrangleFunctions();

}
