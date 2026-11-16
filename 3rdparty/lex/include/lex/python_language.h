#pragma once

#include "highlight.h"

namespace lex {

// Python highlighting: the default TokenClass styles, plus the name after `def` or `class` as PropertyName and a decorator as
// Preprocessor. A triple-quoted string, or one continued by a backslash, carries across lines in LexState (see python_lexer.h).
const Language& pythonLanguage();

}  // namespace lex
