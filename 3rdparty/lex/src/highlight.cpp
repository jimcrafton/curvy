#include <lex/highlight.h>

#include <algorithm>

namespace lex {

// ---------------------------------------------------------------------------
// Styles and themes
// ---------------------------------------------------------------------------

StyleId styleForClass(TokenClass cls) noexcept {
    switch (cls) {
    case TokenClass::Comment:      return StyleId::Comment;
    case TokenClass::Identifier:   return StyleId::Identifier;
    case TokenClass::Keyword:      return StyleId::Keyword;
    case TokenClass::Constant:     return StyleId::Constant;
    case TokenClass::String:       return StyleId::String;
    case TokenClass::Number:       return StyleId::Number;
    case TokenClass::Operator:     return StyleId::Operator;
    case TokenClass::Punctuation:  return StyleId::Punctuation;
    case TokenClass::Preprocessor: return StyleId::Preprocessor;
    case TokenClass::Error:        return StyleId::Error;
    case TokenClass::Whitespace:
    case TokenClass::Newline:
    case TokenClass::Eof:          break;
    }
    return StyleId::Default;
}

namespace {

void setStyle(Theme& t, StyleId id, std::uint32_t fg, std::uint8_t font = FontFlag_None) {
    TextStyle& s = t.style(id);
    s.foreground = fg;
    s.background = 0;
    s.font = font;
}

}  // namespace

Theme Theme::light() {
    Theme t;
    t.background = 0xFFFFFFFF;
    t.problemUnderline = 0xFFE51400;
    setStyle(t, StyleId::Default,      0xFF000000);
    setStyle(t, StyleId::Comment,      0xFF008000, FontFlag_Italic);
    setStyle(t, StyleId::Keyword,      0xFF0000FF);
    setStyle(t, StyleId::Constant,     0xFF0000FF);
    setStyle(t, StyleId::String,       0xFFA31515);
    setStyle(t, StyleId::Number,       0xFF098658);
    setStyle(t, StyleId::Operator,     0xFF000000);
    setStyle(t, StyleId::Punctuation,  0xFF000000);
    setStyle(t, StyleId::Preprocessor, 0xFFAF00DB);
    setStyle(t, StyleId::Identifier,   0xFF001080);
    setStyle(t, StyleId::PropertyName, 0xFF0451A5);
    setStyle(t, StyleId::Error,        0xFFE51400);
    return t;
}

Theme Theme::dark() {
    Theme t;
    t.background = 0xFF1E1E1E;
    t.problemUnderline = 0xFFF44747;
    setStyle(t, StyleId::Default,      0xFFD4D4D4);
    setStyle(t, StyleId::Comment,      0xFF6A9955, FontFlag_Italic);
    setStyle(t, StyleId::Keyword,      0xFF569CD6);
    setStyle(t, StyleId::Constant,     0xFF569CD6);
    setStyle(t, StyleId::String,       0xFFCE9178);
    setStyle(t, StyleId::Number,       0xFFB5CEA8);
    setStyle(t, StyleId::Operator,     0xFFD4D4D4);
    setStyle(t, StyleId::Punctuation,  0xFFD4D4D4);
    setStyle(t, StyleId::Preprocessor, 0xFFC586C0);
    setStyle(t, StyleId::Identifier,   0xFF9CDCFE);
    setStyle(t, StyleId::PropertyName, 0xFF9CDCFE);
    setStyle(t, StyleId::Error,        0xFFF44747);
    return t;
}

// ---------------------------------------------------------------------------
// Language
// ---------------------------------------------------------------------------

void Language::styleLine(std::wstring_view, const Token* tokens, std::size_t count, StyleId* out) const {
    for (std::size_t i = 0; i < count; ++i) out[i] = styleForClass(tokens[i].cls);
}

// ---------------------------------------------------------------------------
// Line lexing shared by highlightLine() and SyntaxHighlighter
// ---------------------------------------------------------------------------
namespace {

// Lex the line that starts at `cp`, appending its styled spans to `out`.
// Returns the lexer state at the end of the line.
LexState lexLine(const Language& language, LexerBase& lexer, const Checkpoint& cp, std::wstring_view text,
                 std::vector<Token>& tokens, std::vector<StyleId>& styles, std::vector<HighlightSpan>& out) {
    tokens.clear();
    lexer.restore(cp);
    for (;;) {
        const Token t = lexer.next();
        if (t.isEof()) break;
        tokens.push_back(t);
        // In split mode a line ends with its Newline token; the endLine test only
        // guards against a lexer that ignores split mode.
        if (t.cls == TokenClass::Newline || t.endLine > cp.line) break;
    }

    styles.assign(tokens.size(), StyleId::Default);
    language.styleLine(text, tokens.data(), tokens.size(), styles.data());

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (isTrivia(t.cls)) continue;
        HighlightSpan s;
        s.column = t.column;
        s.length = static_cast<std::uint32_t>(t.length);
        s.kind = t.kind;
        s.style = styles[i];
        s.flags = t.flags;
        out.push_back(s);
    }
    return lexer.state();
}

LexerOptions splitOptions() {
    LexerOptions o;
    o.splitAtNewlines = true;
    return o;
}

}  // namespace

std::vector<HighlightSpan> highlightLine(const Language& language, std::wstring_view lineText,
                                         LexState entry, LexState* exitState) {
    std::unique_ptr<LexerBase> lexer = language.createLexer(lineText, splitOptions());
    std::vector<Token> tokens;
    std::vector<StyleId> styles;
    std::vector<HighlightSpan> spans;
    Checkpoint cp;
    cp.state = entry;
    const LexState exit = lexLine(language, *lexer, cp, lineText, tokens, styles, spans);
    if (exitState) *exitState = exit;
    return spans;
}

// ---------------------------------------------------------------------------
// SyntaxHighlighter
// ---------------------------------------------------------------------------

SyntaxHighlighter::SyntaxHighlighter(const Language& language)
    : language_(language), index_(std::wstring_view{}) {
    // The lexer decides what counts as a line break; the line index must agree.
    unicodeLineSeparators_ = language_.createLexer(std::wstring_view{}, splitOptions())
                                 ->options().unicodeLineSeparators;
    resetAll();
}

void SyntaxHighlighter::resetAll() {
    index_ = LineIndex(text_, unicodeLineSeparators_);
    lines_.assign(index_.lineCount(), Line{});
    firstDirty_ = 0;
}

void SyntaxHighlighter::setText(std::wstring text) {
    text_ = std::move(text);
    resetAll();
}

std::wstring_view SyntaxHighlighter::lineText(std::size_t line) const noexcept {
    const std::size_t start = index_.lineStart(line);
    return std::wstring_view(text_).substr(start, index_.lineEnd(line) - start);
}

void SyntaxHighlighter::replace(std::size_t offset, std::size_t removeLength, std::wstring_view insert) {
    offset = std::min(offset, text_.size());
    removeLength = std::min(removeLength, text_.size() - offset);

    // Lines of the OLD text touched by the edit.
    const std::size_t oldCount = lines_.size();
    std::size_t first = index_.lineOf(offset);
    std::size_t last = index_.lineOf(offset + removeLength);

    // "\r\n" is one break: an edit that lands between the two characters, or that
    // creates / destroys such a pair, changes the neighbouring line too.
    if (first > 0 && offset > 0 && text_[offset - 1] == L'\r') --first;
    const std::size_t end = offset + removeLength;
    if (last + 1 < oldCount && end < text_.size() && text_[end] == L'\n') ++last;

    const std::size_t suffix = oldCount - 1 - last;  // untouched lines after the edit

    text_.replace(offset, removeLength, insert.data(), insert.size());
    index_.update(text_, offset, removeLength, insert.size());
    const std::size_t newCount = index_.lineCount();

    if (newCount < first + suffix + 1) {  // structure isn't what we expect: start over
        lines_.assign(newCount, Line{});
        firstDirty_ = 0;
        return;
    }

    const std::size_t fresh = newCount - first - suffix;
    lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(first),
                 lines_.begin() + static_cast<std::ptrdiff_t>(last + 1));
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(first), fresh, Line{});
    firstDirty_ = std::min(firstDirty_, first);
}

void SyntaxHighlighter::ensure(std::size_t line) {
    if (line >= lines_.size() || firstDirty_ > line) return;

    std::unique_ptr<LexerBase> lexer = language_.createLexer(text_, splitOptions());

    while (firstDirty_ <= line && firstDirty_ < lines_.size()) {
        const std::size_t i = firstDirty_;
        Line& cur = lines_[i];

        cur.entry = i == 0 ? LexState{} : lines_[i - 1].exit;
        cur.spans.clear();

        Checkpoint cp;
        cp.offset = cp.lineStart = index_.lineStart(i);
        cp.line = static_cast<std::uint32_t>(i);
        cp.state = cur.entry;
        cur.exit = lexLine(language_, *lexer, cp, text_, scratchTokens_, scratchStyles_, cur.spans);
        cur.textDirty = false;
        ++relexed_;

        // Skip following lines whose text is unchanged and whose cached entry state
        // already equals what we just produced: their spans and exit state stand.
        LexState prevExit = cur.exit;
        std::size_t j = i + 1;
        while (j < lines_.size() && !lines_[j].textDirty && lines_[j].entry == prevExit) {
            prevExit = lines_[j].exit;
            ++j;
        }
        firstDirty_ = j;
    }
}

const std::vector<HighlightSpan>& SyntaxHighlighter::spans(std::size_t line) {
    static const std::vector<HighlightSpan> empty;
    if (line >= lines_.size()) return empty;
    ensure(line);
    return lines_[line].spans;
}

LexState SyntaxHighlighter::entryState(std::size_t line) {
    if (line >= lines_.size()) return {};
    ensure(line);
    return lines_[line].entry;
}

LexState SyntaxHighlighter::exitState(std::size_t line) {
    if (line >= lines_.size()) return {};
    ensure(line);
    return lines_[line].exit;
}

}  // namespace lex
