#pragma once

// Base tokenizer for the JSON5 diff visualizer (lossless parser) and for
// syntax highlighting (C++ / JSON5).
//
// Design rules:
//  * Lossless: every source character belongs to exactly one token, including
//    whitespace, newlines and comments. Concatenating token texts reproduces the
//    source, so the parser can keep comments and the highlighter can paint
//    everything.
//  * Never fails: bad input becomes an Error token or a token flagged
//    Unterminated. next() always makes progress and always terminates with Eof.
//  * Non-owning: tokens are (offset, length) into the source, which must outlive
//    the lexer.
//  * Resumable: the scan position plus a small LexState can be captured and
//    restored, so an editor can re-lex from any line start. With
//    LexerOptions::splitAtNewlines, multi-line tokens (block comments, raw
//    strings, continued strings) are emitted one segment per line and the
//    "inside a multi-line token" state is carried in LexState.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lex {

// ---------------------------------------------------------------------------
// Token model
// ---------------------------------------------------------------------------

// Language-specific token identity (e.g. Json5::LBrace, Cpp::KwIf).
// Values below kind::FirstLanguage are reserved for the common kinds.
using TokenKind = std::uint16_t;

namespace kind {
inline constexpr TokenKind Eof          = 0;
inline constexpr TokenKind Error        = 1;
inline constexpr TokenKind Whitespace   = 2;
inline constexpr TokenKind Newline      = 3;
inline constexpr TokenKind LineComment  = 4;
inline constexpr TokenKind BlockComment = 5;
inline constexpr TokenKind FirstLanguage = 32;
}  // namespace kind

// Language-neutral category: what a highlighter themes by and what a parser
// uses to skip trivia without knowing the language.
enum class TokenClass : std::uint8_t {
    Eof,
    Whitespace,
    Newline,
    Comment,
    Identifier,
    Keyword,
    Constant,      // true / false / null / nullptr / NaN / Infinity
    String,
    Number,
    Operator,
    Punctuation,
    Preprocessor,  // C++ '#include', '#define', ...
    Error,
};

constexpr bool isTrivia(TokenClass c) noexcept {
    return c == TokenClass::Whitespace || c == TokenClass::Newline;
}

enum TokenFlags : std::uint8_t {
    TokenFlag_None         = 0,
    TokenFlag_Unterminated = 1,  // string / block comment hit EOF (or a bare newline) before closing
    TokenFlag_Continued    = 2,  // split mode: this segment continues on the next line
    TokenFlag_Resumed      = 4,  // split mode: this segment continues a token begun on an earlier line
    TokenFlag_Invalid      = 8,  // lexically malformed but still one token (bad number, bad escape, ...)
};

struct Token {
    TokenKind     kind  = kind::Eof;
    TokenClass    cls   = TokenClass::Eof;
    std::uint8_t  flags = TokenFlag_None;

    std::size_t   offset = 0;   // in source code units (UTF-16 on Windows)
    std::size_t   length = 0;
    std::uint32_t line    = 0;  // 0-based line of the first character
    std::uint32_t column  = 0;  // 0-based code-unit column of the first character
    std::uint32_t endLine = 0;  // 0-based line of the last character (== line for single-line tokens)

    std::size_t end() const noexcept { return offset + length; }
    bool isEof() const noexcept { return kind == kind::Eof; }
    bool has(TokenFlags f) const noexcept { return (flags & f) != 0; }
};

// ---------------------------------------------------------------------------
// Resumable lexer state
// ---------------------------------------------------------------------------

// Opaque to the base class; the derived lexer decides what the values mean.
// Default (0, 0) is the "ordinary code" state. Examples:
//   C++   : mode = InBlockComment | InRawString | InPreprocessorContinuation,
//           aux  = raw-string delimiter id
//   JSON5 : mode = InBlockComment | InString,  aux = quote character
struct LexState {
    std::uint32_t mode = 0;
    std::uint32_t aux  = 0;
};

constexpr bool operator==(const LexState& a, const LexState& b) noexcept {
    return a.mode == b.mode && a.aux == b.aux;
}
constexpr bool operator!=(const LexState& a, const LexState& b) noexcept { return !(a == b); }

// Everything needed to resume scanning at an arbitrary point. `lineStart` is the
// offset of the start of `line` (used to compute columns).
struct Checkpoint {
    std::size_t   offset    = 0;
    std::uint32_t line      = 0;
    std::size_t   lineStart = 0;
    LexState      state{};
};

struct LexerOptions {
    // Emit multi-line tokens as one segment per line, carrying state across the
    // newline. Wanted by the highlighter; leave off for the parser so a block
    // comment is one token.
    bool splitAtNewlines = false;

    // Treat U+2028 / U+2029 as line terminators (JSON5 / ECMAScript do, C++ does not).
    bool unicodeLineSeparators = false;
};

// ---------------------------------------------------------------------------
// Character classification helpers (no locale, no allocation)
// ---------------------------------------------------------------------------
namespace chars {

constexpr bool isDigit(wchar_t c) noexcept { return c >= L'0' && c <= L'9'; }

constexpr bool isHexDigit(wchar_t c) noexcept {
    return isDigit(c) || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

// Horizontal whitespace only; line terminators are handled by the base class.
// Superset of what C++ and JSON5 accept (ES5 WhiteSpace + Zs).
constexpr bool isHorizontalSpace(wchar_t c) noexcept {
    switch (c) {
    case L' ': case L'\t': case L'\v': case L'\f':
    case 0x00A0: case 0x1680: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
        return true;
    default:
        return c >= 0x2000 && c <= 0x200A;
    }
}

// Approximation of ID_Start / ID_Continue: ASCII rules plus any non-ASCII code
// unit that isn't whitespace or a line separator (surrogates included, so
// astral identifiers stay in one token). Good enough for highlighting; a parser
// that needs exact Unicode identifiers can tighten this in the derived lexer.
constexpr bool isIdentStart(wchar_t c) noexcept {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_' || c == L'$' ||
           (c >= 0x80 && !isHorizontalSpace(c) && c != 0x2028 && c != 0x2029);
}

constexpr bool isIdentPart(wchar_t c) noexcept { return isIdentStart(c) || isDigit(c); }

}  // namespace chars

// ---------------------------------------------------------------------------
// Keyword table: identifier spelling -> (kind, class). Build once per language
// (static local), then look up each scanned identifier. Handles C++ keywords,
// JSON5 literals (true/false/null/NaN/Infinity), contextual words, etc.
// ---------------------------------------------------------------------------

class KeywordTable {
public:
    struct Entry {
        std::wstring_view text;
        TokenKind         kind;
        TokenClass        cls;
    };

    KeywordTable(std::initializer_list<Entry> entries) {
        map_.reserve(entries.size());
        for (const Entry& e : entries) map_.emplace(e.text, e);
    }

    // nullptr if `word` is a plain identifier.
    const Entry* find(std::wstring_view word) const noexcept {
        auto it = map_.find(word);
        return it == map_.end() ? nullptr : &it->second;
    }

private:
    std::unordered_map<std::wstring_view, Entry> map_;
};

// ---------------------------------------------------------------------------
// LexerBase
//
// Extending it (JSON5 now, C++ later): derive, implement scanToken(), and use
// the protected helpers. A C++ lexer maps onto the base like so:
//   * operators/punctuators  -> matchLongest() with a spelling table (maximal munch)
//   * keywords / true/false  -> KeywordTable
//   * /* */ comments         -> consumeThrough(L"*/") + state_.mode (split mode)
//   * R"delim( ... )delim"   -> state_.aux holds an interned delimiter id;
//                               consumeThrough() the ")delim\"" closer
//   * #directives, "\"-newline splices -> Preprocessor class + state_.mode
//   * u8"" L"" u"" U"" R""   -> prefix decided in scanToken, class String
// The base has no per-language knowledge; everything language-specific lives
// in the derived scanToken() and its tables.
// ---------------------------------------------------------------------------

class LexerBase {
public:
    explicit LexerBase(std::wstring_view source, LexerOptions options = {}) noexcept;
    virtual ~LexerBase() = default;

    // Returns the next token. Repeats Eof forever once the input is consumed.
    Token next();

    // Next token without consuming it.
    Token peek();

    // Lex the whole remaining input; the Eof token is not included.
    std::vector<Token> tokenize();

    bool atEnd() const noexcept { return pos_ >= src_.size(); }

    std::wstring_view source() const noexcept { return src_; }
    std::wstring_view text(const Token& t) const noexcept { return src_.substr(t.offset, t.length); }
    const LexerOptions& options() const noexcept { return opts_; }

    // Backtracking / incremental re-lex. To restart the highlighter at line N,
    // build a Checkpoint from that line's start offset and its cached entry state.
    Checkpoint checkpoint() const noexcept { return {pos_, line_, lineStart_, state_}; }
    void restore(const Checkpoint& cp) noexcept;
    LexState state() const noexcept { return state_; }

protected:
    // What scanToken() reports. `kind` is the language-specific identity, `cls`
    // the language-neutral category.
    struct Scan {
        TokenKind    kind  = kind::Error;
        TokenClass   cls   = TokenClass::Error;
        std::uint8_t flags = TokenFlag_None;
    };

    // Implemented by the language. Called only when input remains and the
    // cursor is NOT on a newline (the base emits Newline tokens itself, so
    // derived code never sees one at a token start). Consume the token and return
    // its description. If nothing is consumed the base eats one code unit as an
    // Error token, so a derived lexer can't cause an infinite loop.
    //
    // In split mode, check `state_` FIRST (before whitespace, identifiers, ...)
    // and, if inside a multi-line token, continue it -- leading spaces of a
    // continuation line belong to that token.
    virtual Scan scanToken() = 0;

    // --- cursor ---------------------------------------------------------
    wchar_t cur() const noexcept { return pos_ < src_.size() ? src_[pos_] : L'\0'; }
    wchar_t peekAt(std::size_t n) const noexcept {
        return pos_ + n < src_.size() ? src_[pos_ + n] : L'\0';
    }
    std::size_t pos() const noexcept { return pos_; }

    // Consume one code unit, maintaining line/column bookkeeping. Prefer this to
    // touching the position directly. Must not be called at end of input.
    wchar_t advance() noexcept {
        const wchar_t c = src_[pos_++];
        if (isLineBreakUnit(c)) {
            ++line_;
            lineStart_ = pos_;
            lastNewlineEnd_ = pos_;
        }
        return c;
    }

    bool accept(wchar_t c) noexcept {
        if (!atEnd() && src_[pos_] == c) { advance(); return true; }
        return false;
    }

    // Consume `s` if the input continues with it. `s` must not contain newlines.
    bool accept(std::wstring_view s) noexcept {
        if (!lookingAt(s)) return false;
        for (std::size_t i = 0; i < s.size(); ++i) advance();
        return true;
    }

    bool lookingAt(std::wstring_view s) const noexcept {
        return pos_ + s.size() <= src_.size() && src_.compare(pos_, s.size(), s) == 0;
    }

    // Consume `n` code units known not to contain a line break (operator spellings, etc.).
    void advanceBy(std::size_t n) noexcept { while (n--) advance(); }

    // Length of the longest spelling in `table` that the input continues with
    // (maximal munch), or 0. Table order does not matter; spellings must not
    // contain newlines. Follow with advanceBy(n).
    std::size_t matchLongest(const std::wstring_view* table, std::size_t count) const noexcept {
        std::size_t best = 0;
        for (std::size_t i = 0; i < count; ++i)
            if (table[i].size() > best && lookingAt(table[i])) best = table[i].size();
        return best;
    }
    template <std::size_t N>
    std::size_t matchLongest(const std::wstring_view (&table)[N]) const noexcept {
        return matchLongest(table, N);
    }

    // Consume while pred(cur()) holds (never crosses end of input). Returns count consumed.
    template <class Pred>
    std::size_t acceptWhile(Pred pred) noexcept(noexcept(pred(L'x'))) {
        const std::size_t start = pos_;
        while (!atEnd() && pred(src_[pos_])) advance();
        return pos_ - start;
    }

    // --- newlines -------------------------------------------------------
    bool atNewline() const noexcept { return !atEnd() && isNewlineChar(src_[pos_]); }

    // Consume one line terminator: "\r\n" as a unit, or a lone \n, \r, (U+2028/9).
    void consumeNewline() noexcept {
        if (cur() == L'\r' && peekAt(1) == L'\n') advance();
        advance();
    }

    // Consume up to and including `closer` (no newlines in `closer`), for block
    // comments, raw strings, etc. Returns true if `closer` was found. Returns
    // false at EOF and -- in split mode -- when it stops in front of a newline,
    // so the caller can distinguish by checking atEnd() and set state / flags.
    bool consumeThrough(std::wstring_view closer) noexcept {
        while (!atEnd()) {
            if (lookingAt(closer)) { accept(closer); return true; }
            if (opts_.splitAtNewlines && atNewline()) return false;
            advance();
        }
        return false;
    }

    // Standard result for a run of horizontal whitespace.
    Scan scanWhitespace() noexcept {
        acceptWhile(chars::isHorizontalSpace);
        return {kind::Whitespace, TokenClass::Whitespace};
    }

    bool isNewlineChar(wchar_t c) const noexcept {
        return c == L'\n' || c == L'\r' ||
               (opts_.unicodeLineSeparators && (c == 0x2028 || c == 0x2029));
    }

    // Persistent state, readable/writable by the derived lexer. Only meaningful
    // at token boundaries; with splitAtNewlines it is what lets a line be lexed
    // in isolation.
    LexState state_{};

private:
    // A code unit that ends a line. '\r' only counts when NOT followed by '\n',
    // so "\r\n" is a single line break, counted at the '\n'.
    bool isLineBreakUnit(wchar_t c) const noexcept {
        if (c == L'\r') return cur() != L'\n';  // pos_ already advanced past '\r'
        return c == L'\n' || (opts_.unicodeLineSeparators && (c == 0x2028 || c == 0x2029));
    }

    std::wstring_view src_;
    LexerOptions      opts_;

    std::size_t   pos_       = 0;
    std::uint32_t line_      = 0;
    std::size_t   lineStart_ = 0;
    // Offset just past the most recently consumed line break; lets next() tell
    // whether a token's last character was that break (for Token::endLine).
    std::size_t   lastNewlineEnd_ = static_cast<std::size_t>(-1);
};

// ---------------------------------------------------------------------------
// LineIndex: offset <-> (line, column) for text that isn't tied to a lexer run
// (AST node offsets, diff gutter, resuming the highlighter at line N).
// ---------------------------------------------------------------------------

class LineIndex {
public:
    struct Position {
        std::uint32_t line;
        std::uint32_t column;
    };

    explicit LineIndex(std::wstring_view source, bool unicodeLineSeparators = false);

    std::size_t lineCount() const noexcept { return starts_.size(); }
    std::size_t lineStart(std::size_t line) const noexcept { return starts_[line]; }
    // End of the line's content, excluding its terminator.
    std::size_t lineEnd(std::size_t line) const noexcept;

    std::uint32_t lineOf(std::size_t offset) const noexcept;
    Position positionOf(std::size_t offset) const noexcept;

    // Incremental update after an edit. `newSource` is the text AFTER replacing
    // `removedLength` code units at `offset` with `insertedLength` new ones; the
    // index still describes the text before the edit. Rescans only the lines the
    // edit touches and shifts the rest, so the cost is O(edited lines + line count)
    // rather than O(document size). The result equals LineIndex(newSource).
    void update(std::wstring_view newSource, std::size_t offset, std::size_t removedLength,
                std::size_t insertedLength);

private:
    // Append the start of every line that begins after a break located in [from, to).
    void scan(std::size_t from, std::size_t to, std::vector<std::size_t>& out) const;

    std::wstring_view        src_;
    bool                     unicodeLineSeparators_ = false;
    std::vector<std::size_t> starts_;  // starts_[0] == 0; one entry per line
};

}  // namespace lex
