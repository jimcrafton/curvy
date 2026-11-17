#pragma once

#include <string>

namespace curvy {

    // Where a language's checker refuses a text: 1-based line and column of the offending token (the end is the same place when the checker
    // gives none), and its own message ("invalid syntax", "unterminated string literal ...", "wrangle parse error ...").
    struct SyntaxProblem {
        int line = 1;
        int column = 1;
        int endLine = 1;
        int endColumn = 1;
        std::string message;
    };

}
