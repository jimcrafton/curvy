#pragma once

// Python lexer on top of lex::LexerBase - lexical only, for highlighting (no indentation tokens, no f-string expression parsing).
//
//  * Lossless and never fails, like the base.
//  * Keywords are Keyword; True / False / None are Constant. Names that follow `def` or `class` are styled by the language (see
//    python_language.h), not here.
//  * Numbers: decimal, 0x / 0o / 0b, underscores, floats with exponents, imaginary (1j). Obviously malformed ones (0x, 1e) are flagged
//    TokenFlag_Invalid.
//  * Strings with any prefix (r, u, b, f and their pairs, any case): '...', "...", '''...''' and """...""". A backslash escapes the next
//    character, quotes included, raw strings too. An unclosed single-quoted string ends at the line break and is flagged Unterminated;
//    a backslash before the break continues it. f-strings are plain strings here.
//  * '#' comments.
//  * A decorator ('@' first on its line, with its dotted name) is one Decorator token (class Preprocessor, so it gets its own colour).
//    Elsewhere '@' is the matrix-multiply operator.
//  * A backslash right before a line break is a line continuation: trivia, like C++'s splice.
//  * splitAtNewlines: a triple-quoted string, and a string continued by a backslash, come out one segment per line; LexState is
//    { mode = python::Mode, aux = the quote character }.

#include "lexer_base.h"

#include <string>

namespace lex {

namespace python {

enum Kind : TokenKind {
    Identifier = kind::FirstLanguage,
    Keyword,
    Constant,   // True / False / None
    Number,
    String,
    Decorator,
    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    Comma,
    Colon,
    Semicolon,
    Dot,
    Operator,   // every other operator, longest match ("**=", "//=", ":=", "->", "...")
};

enum Mode : std::uint32_t {
    ModeCode = 0,
    InTripleString = 1,   // aux = the quote character
    InContinuedString = 2,  // a single-quoted string continued by a backslash; aux = the quote character
};

}  // namespace python

class PythonLexer : public LexerBase {
public:
    explicit PythonLexer(std::wstring_view source, LexerOptions options = {}) noexcept;

protected:
    Scan scanToken() override;

private:
    // The cursor is on the opening quote (a prefix is already consumed).
    Scan scanStringStart();
    // Continues a string body: `triple` says whether it ends at three quotes. `resumed` = a later line of the same token.
    Scan scanStringBody(wchar_t quote, bool triple, bool resumed);
    Scan scanNumber();
    Scan scanWord();
    Scan scanOperator();
    bool onlySpaceBeforeOnLine() const noexcept;
};

}  // namespace lex
