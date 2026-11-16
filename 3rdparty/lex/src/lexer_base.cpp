#include <lex/lexer_base.h>

#include <algorithm>

namespace lex {

LexerBase::LexerBase(std::wstring_view source, LexerOptions options) noexcept
    : src_(source), opts_(options) {}

Token LexerBase::next() {
    Token t;
    t.offset = pos_;
    t.line = line_;
    t.column = static_cast<std::uint32_t>(pos_ - lineStart_);

    if (atEnd()) {
        t.endLine = line_;
        return t;  // defaults describe Eof
    }

    Scan s;
    if (atNewline()) {
        consumeNewline();
        s = {kind::Newline, TokenClass::Newline};
    } else {
        s = scanToken();
        if (pos_ == t.offset) {
            // Derived lexer didn't recognise the character: swallow it so we always advance.
            advance();
            s = {kind::Error, TokenClass::Error};
        }
    }

    t.kind = s.kind;
    t.cls = s.cls;
    t.flags = s.flags;
    t.length = pos_ - t.offset;
    // If the token ended with a line break, line_ has already moved on; the
    // token's last character still lives on the previous line.
    t.endLine = (lastNewlineEnd_ == pos_) ? line_ - 1 : line_;
    return t;
}

Token LexerBase::peek() {
    const Checkpoint cp = checkpoint();
    Token t = next();
    restore(cp);
    return t;
}

std::vector<Token> LexerBase::tokenize() {
    std::vector<Token> out;
    for (Token t = next(); !t.isEof(); t = next()) out.push_back(t);
    return out;
}

void LexerBase::restore(const Checkpoint& cp) noexcept {
    pos_ = std::min(cp.offset, src_.size());
    line_ = cp.line;
    lineStart_ = cp.lineStart;
    state_ = cp.state;
    lastNewlineEnd_ = static_cast<std::size_t>(-1);
}

// ---------------------------------------------------------------------------

LineIndex::LineIndex(std::wstring_view source, bool unicodeLineSeparators)
    : src_(source), unicodeLineSeparators_(unicodeLineSeparators) {
    starts_.push_back(0);
    scan(0, src_.size(), starts_);
}

void LineIndex::scan(std::size_t from, std::size_t to, std::vector<std::size_t>& out) const {
    for (std::size_t i = from; i < to; ++i) {
        const wchar_t c = src_[i];
        if (c == L'\r') {
            if (i + 1 < src_.size() && src_[i + 1] == L'\n') ++i;  // "\r\n" is one break
            out.push_back(i + 1);
        } else if (c == L'\n' || (unicodeLineSeparators_ && (c == 0x2028 || c == 0x2029))) {
            out.push_back(i + 1);
        }
    }
}

void LineIndex::update(std::wstring_view newSource, std::size_t offset, std::size_t removedLength,
                       std::size_t insertedLength) {
    const std::size_t oldSize = newSource.size() - insertedLength + removedLength;

    // Old lines touched by the edit. Starting one character early catches a '\r'
    // just before `offset` that a newly inserted '\n' now pairs with.
    const std::size_t a = lineOf(offset > 0 ? offset - 1 : 0);
    const std::size_t b = lineOf(offset + removedLength);
    const bool bIsLast = b + 1 == starts_.size();

    const std::size_t oldRegionEnd = bIsLast ? oldSize : starts_[b + 1];
    // Wrapping arithmetic makes this correct for negative deltas too.
    const std::size_t delta = insertedLength - removedLength;
    const std::size_t newRegionEnd = oldRegionEnd + delta;

    src_ = newSource;

    // Line starts inside the region, computed from the new text. A start equal to
    // newRegionEnd is the (shifted) start of old line b+1 and is kept from the old
    // list, unless b was the last line, in which case it is a new trailing line.
    std::vector<std::size_t> fresh;
    scan(starts_[a], newRegionEnd, fresh);
    if (!bIsLast && !fresh.empty() && fresh.back() == newRegionEnd) fresh.pop_back();

    for (std::size_t k = b + 1; k < starts_.size(); ++k) starts_[k] += delta;
    starts_.erase(starts_.begin() + static_cast<std::ptrdiff_t>(a + 1),
                  starts_.begin() + static_cast<std::ptrdiff_t>(b + 1));
    starts_.insert(starts_.begin() + static_cast<std::ptrdiff_t>(a + 1), fresh.begin(), fresh.end());
}

std::size_t LineIndex::lineEnd(std::size_t line) const noexcept {
    std::size_t end = (line + 1 < starts_.size()) ? starts_[line + 1] : src_.size();
    // Trim the terminator (\n, \r\n, \r, U+2028/9 -- each preceded only by content).
    if (end > starts_[line] && line + 1 < starts_.size()) {
        --end;
        if (end > starts_[line] && src_[end] == L'\n' && src_[end - 1] == L'\r') --end;
    }
    return end;
}

std::uint32_t LineIndex::lineOf(std::size_t offset) const noexcept {
    auto it = std::upper_bound(starts_.begin(), starts_.end(), offset);
    return static_cast<std::uint32_t>((it - starts_.begin()) - 1);
}

LineIndex::Position LineIndex::positionOf(std::size_t offset) const noexcept {
    const std::uint32_t line = lineOf(offset);
    return {line, static_cast<std::uint32_t>(offset - starts_[line])};
}

}  // namespace lex
