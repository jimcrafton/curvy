#pragma once

// Syntax-highlighting support, independent of any UI toolkit.
//
//   Language            what a language plugs in: its lexer, and how one line's
//                       tokens map to styles (Json5Language now, a C++ one later)
//   StyleId / Theme     the styling vocabulary and colours (0xAARRGGBB, same layout
//                       as Blend2D's BLRgba32; GDI's COLORREF is BGR, convert there)
//   HighlightSpan       one styled run on one line
//   SyntaxHighlighter   per-document, incremental: edit the text, ask for the
//                       spans of the lines you are about to draw
//   highlightLine()     one-shot, for text that isn't an editable document (the
//                       diff view's pretty-printed lines)
//
// How incremental highlighting works: the lexer runs in split mode, so every
// token lies on one line and a line is fully described by (its text, its entry
// LexState). The highlighter caches each line's entry/exit state and spans. After
// an edit only the touched lines are marked dirty; lines are re-lexed lazily, on
// demand, and re-lexing stops as soon as a line's exit state matches the next
// line's cached entry state (so opening a block comment re-lexes to the end of the
// comment, but a plain edit re-lexes one line).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lexer_base.h"

namespace lex {

// ---------------------------------------------------------------------------
// Styles
// ---------------------------------------------------------------------------

enum class StyleId : std::uint8_t {
    Default,
    Comment,
    Keyword,
    Constant,      // true / false / null
    String,
    Number,
    Operator,
    Punctuation,
    Preprocessor,
    Identifier,
    PropertyName,  // object keys (JSON5); a language may refine other things too
    Error,
    Count
};

constexpr std::size_t kStyleCount = static_cast<std::size_t>(StyleId::Count);

// Default TokenClass -> StyleId mapping.
StyleId styleForClass(TokenClass cls) noexcept;

struct HighlightSpan {
    std::uint32_t column = 0;  // 0-based code units from the start of the line
    std::uint32_t length = 0;
    TokenKind     kind   = kind::Error;
    StyleId       style  = StyleId::Default;
    std::uint8_t  flags  = TokenFlag_None;  // TokenFlags from the token

    // Worth a squiggle: an error token, or a malformed / unterminated token.
    bool isProblem() const noexcept {
        return style == StyleId::Error || (flags & (TokenFlag_Invalid | TokenFlag_Unterminated)) != 0;
    }
};

inline bool operator==(const HighlightSpan& a, const HighlightSpan& b) noexcept {
    return a.column == b.column && a.length == b.length && a.kind == b.kind &&
           a.style == b.style && a.flags == b.flags;
}
inline bool operator!=(const HighlightSpan& a, const HighlightSpan& b) noexcept { return !(a == b); }

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

enum FontFlags : std::uint8_t {
    FontFlag_None      = 0,
    FontFlag_Bold      = 1,
    FontFlag_Italic    = 2,
    FontFlag_Underline = 4,
};

struct TextStyle {
    std::uint32_t foreground = 0xFF000000;  // 0xAARRGGBB
    std::uint32_t background = 0x00000000;  // alpha 0 = none
    std::uint8_t  font       = FontFlag_None;
};

class Theme {
public:
    static Theme light();
    static Theme dark();

    const TextStyle& style(StyleId id) const noexcept { return styles_[static_cast<std::size_t>(id)]; }
    TextStyle& style(StyleId id) noexcept { return styles_[static_cast<std::size_t>(id)]; }

    std::uint32_t background = 0xFFFFFFFF;       // editor / view background
    std::uint32_t problemUnderline = 0xFFE51400; // squiggle colour for HighlightSpan::isProblem()

private:
    TextStyle styles_[kStyleCount];
};

// ---------------------------------------------------------------------------
// Language
// ---------------------------------------------------------------------------

class Language {
public:
    virtual ~Language() = default;

    virtual const wchar_t* name() const noexcept = 0;

    // A lexer over `text`. The highlighter passes options with splitAtNewlines
    // set; the lexer may add its own (JSON5 forces unicodeLineSeparators).
    virtual std::unique_ptr<LexerBase> createLexer(std::wstring_view text, LexerOptions options) const = 0;

    // Style one line's tokens. `tokens` are all the tokens of the line (trivia
    // included, offsets/columns as lexed); write one StyleId per token into `out`.
    // `text` is the whole document (for looking up token text). The default is the
    // TokenClass mapping; override to refine using neighbouring tokens (a JSON5 key
    // is a token followed by ':', a C++ function is an identifier followed by '(').
    virtual void styleLine(std::wstring_view text, const Token* tokens, std::size_t count,
                           StyleId* out) const;
};

// ---------------------------------------------------------------------------
// One-shot line highlighting
// ---------------------------------------------------------------------------

// Highlight the first line of `lineText` starting in lexer state `entry`.
// Returns its spans (trivia omitted); `*exitState`, if given, receives the state
// to feed to the next line. Text after the first line break is ignored.
std::vector<HighlightSpan> highlightLine(const Language& language, std::wstring_view lineText,
                                         LexState entry = {}, LexState* exitState = nullptr);

// ---------------------------------------------------------------------------
// Incremental document highlighter
// ---------------------------------------------------------------------------

class SyntaxHighlighter {
public:
    explicit SyntaxHighlighter(const Language& language);

    // Replace the whole document.
    void setText(std::wstring text);

    // Replace `removeLength` code units at `offset` with `insert` (both clamped
    // to the document). Only the affected lines are invalidated.
    void replace(std::size_t offset, std::size_t removeLength, std::wstring_view insert);

    const std::wstring& text() const noexcept { return text_; }
    std::size_t lineCount() const noexcept { return lines_.size(); }
    std::size_t lineStart(std::size_t line) const noexcept { return index_.lineStart(line); }
    // Line content without its terminator.
    std::wstring_view lineText(std::size_t line) const noexcept;

    // Styled spans of `line` (trivia omitted, gaps are default-styled). Lexes
    // whatever is stale up to and including that line. The reference is valid until
    // the next replace()/setText() or the next call that highlights further lines.
    const std::vector<HighlightSpan>& spans(std::size_t line);

    // Lexer state at the start / end of a line (e.g. "inside a block comment").
    LexState entryState(std::size_t line);
    LexState exitState(std::size_t line);

    // How many line lexes have happened so far (diagnostics and tests).
    std::size_t relexedLineCount() const noexcept { return relexed_; }

private:
    struct Line {
        LexState entry;
        LexState exit;
        std::vector<HighlightSpan> spans;
        bool textDirty = true;  // text changed (or never lexed): must be re-lexed
    };

    void ensure(std::size_t line);
    void resetAll();

    const Language& language_;
    bool unicodeLineSeparators_ = false;

    std::wstring text_;
    LineIndex index_;
    std::vector<Line> lines_;
    // Lines before this index are up to date; later lines may need re-lexing.
    std::size_t firstDirty_ = 0;
    std::size_t relexed_ = 0;

    std::vector<Token> scratchTokens_;
    std::vector<StyleId> scratchStyles_;
};

}  // namespace lex
