#include <lex/python_lexer.h>

namespace lex {

namespace {

constexpr bool isNameStart(wchar_t c) noexcept {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_' ||
           (c >= 0x80 && !chars::isHorizontalSpace(c) && c != 0x2028 && c != 0x2029);
}

constexpr bool isNamePart(wchar_t c) noexcept { return isNameStart(c) || chars::isDigit(c); }

constexpr bool isQuote(wchar_t c) noexcept { return c == L'"' || c == L'\''; }

// r, u, b, f, br, rb, fr, rf in any case.
bool isStringPrefix(std::wstring_view word) noexcept {
    if (word.empty() || word.size() > 2) {
        return false;
    }
    auto lower = [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c; };
    const wchar_t a = lower(word[0]);
    if (word.size() == 1) {
        return a == L'r' || a == L'u' || a == L'b' || a == L'f';
    }
    const wchar_t b = lower(word[1]);
    return ((a == L'b' || a == L'f') && b == L'r') || (a == L'r' && (b == L'b' || b == L'f'));
}

const KeywordTable& keywords() {
    static const KeywordTable table = {
        {L"False", python::Constant, TokenClass::Constant},
        {L"None", python::Constant, TokenClass::Constant},
        {L"True", python::Constant, TokenClass::Constant},
        {L"and", python::Keyword, TokenClass::Keyword},
        {L"as", python::Keyword, TokenClass::Keyword},
        {L"assert", python::Keyword, TokenClass::Keyword},
        {L"async", python::Keyword, TokenClass::Keyword},
        {L"await", python::Keyword, TokenClass::Keyword},
        {L"break", python::Keyword, TokenClass::Keyword},
        {L"class", python::Keyword, TokenClass::Keyword},
        {L"continue", python::Keyword, TokenClass::Keyword},
        {L"def", python::Keyword, TokenClass::Keyword},
        {L"del", python::Keyword, TokenClass::Keyword},
        {L"elif", python::Keyword, TokenClass::Keyword},
        {L"else", python::Keyword, TokenClass::Keyword},
        {L"except", python::Keyword, TokenClass::Keyword},
        {L"finally", python::Keyword, TokenClass::Keyword},
        {L"for", python::Keyword, TokenClass::Keyword},
        {L"from", python::Keyword, TokenClass::Keyword},
        {L"global", python::Keyword, TokenClass::Keyword},
        {L"if", python::Keyword, TokenClass::Keyword},
        {L"import", python::Keyword, TokenClass::Keyword},
        {L"in", python::Keyword, TokenClass::Keyword},
        {L"is", python::Keyword, TokenClass::Keyword},
        {L"lambda", python::Keyword, TokenClass::Keyword},
        {L"nonlocal", python::Keyword, TokenClass::Keyword},
        {L"not", python::Keyword, TokenClass::Keyword},
        {L"or", python::Keyword, TokenClass::Keyword},
        {L"pass", python::Keyword, TokenClass::Keyword},
        {L"raise", python::Keyword, TokenClass::Keyword},
        {L"return", python::Keyword, TokenClass::Keyword},
        {L"try", python::Keyword, TokenClass::Keyword},
        {L"while", python::Keyword, TokenClass::Keyword},
        {L"with", python::Keyword, TokenClass::Keyword},
        {L"yield", python::Keyword, TokenClass::Keyword},
    };
    return table;
}

}  // namespace

PythonLexer::PythonLexer(std::wstring_view source, LexerOptions options) noexcept : LexerBase(source, options) {}

bool PythonLexer::onlySpaceBeforeOnLine() const noexcept {
    const std::wstring_view text = source();
    std::size_t i = pos();
    while (i > 0) {
        const wchar_t c = text[i - 1];
        if (c == L'\n' || c == L'\r') {
            return true;
        }
        if (c != L' ' && c != L'\t' && c != L'\f') {
            return false;
        }
        --i;
    }
    return true;
}

LexerBase::Scan PythonLexer::scanStringBody(wchar_t quote, bool triple, bool resumed) {
    std::uint8_t flags = resumed ? TokenFlag_Resumed : TokenFlag_None;
    const std::wstring closer(triple ? 3 : 1, quote);
    while (!atEnd()) {
        if (atNewline()) {
            if (!triple) {
                // A bare line break ends a single-quoted string; only a backslash (handled below) continues it.
                state_ = {};
                return {python::String, TokenClass::String, static_cast<std::uint8_t>(flags | TokenFlag_Unterminated)};
            }
            if (options().splitAtNewlines) {
                state_.mode = python::InTripleString;
                state_.aux = static_cast<std::uint32_t>(quote);
                return {python::String, TokenClass::String, static_cast<std::uint8_t>(flags | TokenFlag_Continued)};
            }
            consumeNewline();
            continue;
        }
        if (lookingAt(closer)) {
            accept(closer);
            state_ = {};
            return {python::String, TokenClass::String, flags};
        }
        const wchar_t c = advance();
        if (c == L'\\' && !atEnd()) {
            if (atNewline()) {
                // In a triple string the next loop iteration deals with the line break.
                if (!triple && options().splitAtNewlines) {
                    state_.mode = python::InContinuedString;
                    state_.aux = static_cast<std::uint32_t>(quote);
                    return {python::String, TokenClass::String, static_cast<std::uint8_t>(flags | TokenFlag_Continued)};
                }
                if (!triple) {
                    consumeNewline();   // the string goes on past the line break
                }
            } else {
                advance();
            }
        }
    }
    state_ = {};
    return {python::String, TokenClass::String, static_cast<std::uint8_t>(flags | TokenFlag_Unterminated)};
}

LexerBase::Scan PythonLexer::scanStringStart() {
    const wchar_t quote = cur();
    if (peekAt(1) == quote && peekAt(2) == quote) {
        advanceBy(3);
        return scanStringBody(quote, true, false);
    }
    advance();
    return scanStringBody(quote, false, false);
}

LexerBase::Scan PythonLexer::scanNumber() {
    std::uint8_t flags = TokenFlag_None;
    auto digits = [this](auto isDigitFn) {
        std::size_t count = 0;
        while (!atEnd() && (isDigitFn(cur()) || (cur() == L'_' && isDigitFn(peekAt(1))))) {
            advance();
            ++count;
        }
        return count;
    };
    if (cur() == L'0' && (peekAt(1) == L'x' || peekAt(1) == L'X')) {
        advanceBy(2);
        if (digits(chars::isHexDigit) == 0) {
            flags |= TokenFlag_Invalid;
        }
    } else if (cur() == L'0' && (peekAt(1) == L'o' || peekAt(1) == L'O')) {
        advanceBy(2);
        if (digits([](wchar_t c) { return c >= L'0' && c <= L'7'; }) == 0) {
            flags |= TokenFlag_Invalid;
        }
    } else if (cur() == L'0' && (peekAt(1) == L'b' || peekAt(1) == L'B')) {
        advanceBy(2);
        if (digits([](wchar_t c) { return c == L'0' || c == L'1'; }) == 0) {
            flags |= TokenFlag_Invalid;
        }
    } else {
        digits(chars::isDigit);
        if (cur() == L'.') {
            advance();
            digits(chars::isDigit);
        }
        if (cur() == L'e' || cur() == L'E') {
            const wchar_t after = peekAt(1);
            if (chars::isDigit(after) || ((after == L'+' || after == L'-') && chars::isDigit(peekAt(2)))) {
                advanceBy(after == L'+' || after == L'-' ? 2 : 1);
                digits(chars::isDigit);
            } else {
                advance();
                flags |= TokenFlag_Invalid;
            }
        }
        if (cur() == L'j' || cur() == L'J') {
            advance();
        }
    }
    return {python::Number, TokenClass::Number, flags};
}

LexerBase::Scan PythonLexer::scanWord() {
    const std::size_t start = pos();
    acceptWhile(isNamePart);
    const std::wstring_view word = source().substr(start, pos() - start);
    if (isQuote(cur()) && isStringPrefix(word)) {
        return scanStringStart();
    }
    if (const KeywordTable::Entry* entry = keywords().find(word)) {
        return {entry->kind, entry->cls};
    }
    return {python::Identifier, TokenClass::Identifier};
}

LexerBase::Scan PythonLexer::scanOperator() {
    static const std::wstring_view spellings[] = {
        L"**=", L"//=", L">>=", L"<<=", L"->", L":=", L"==", L"!=", L"<=", L">=", L"+=", L"-=", L"*=", L"/=", L"%=", L"&=",
        L"|=", L"^=", L"@=", L"**", L"//", L"<<", L">>", L"+", L"-", L"*", L"/", L"%", L"&", L"|", L"^", L"~", L"<", L">",
        L"=", L"@",
    };
    if (lookingAt(L"...")) {
        advanceBy(3);
        return {python::Operator, TokenClass::Operator};
    }
    const std::size_t length = matchLongest(spellings);
    if (length != 0) {
        advanceBy(length);
        return {python::Operator, TokenClass::Operator};
    }
    advance();
    return {kind::Error, TokenClass::Error, TokenFlag_Invalid};
}

LexerBase::Scan PythonLexer::scanToken() {
    if (state_.mode == python::InTripleString) {
        return scanStringBody(static_cast<wchar_t>(state_.aux), true, true);
    }
    if (state_.mode == python::InContinuedString) {
        return scanStringBody(static_cast<wchar_t>(state_.aux), false, true);
    }

    const wchar_t c = cur();
    if (chars::isHorizontalSpace(c)) {
        return scanWhitespace();
    }
    if (c == L'\\' && (isNewlineChar(peekAt(1)) || pos() + 1 >= source().size())) {
        advance();
        return {kind::Whitespace, TokenClass::Whitespace};
    }
    if (c == L'#') {
        acceptWhile([this](wchar_t ch) { return !isNewlineChar(ch); });
        return {kind::LineComment, TokenClass::Comment};
    }
    if (chars::isDigit(c) || (c == L'.' && chars::isDigit(peekAt(1)))) {
        return scanNumber();
    }
    if (isNameStart(c)) {
        return scanWord();
    }
    if (isQuote(c)) {
        return scanStringStart();
    }
    if (c == L'@' && onlySpaceBeforeOnLine()) {
        advance();
        acceptWhile([](wchar_t ch) { return isNamePart(ch) || ch == L'.'; });
        return {python::Decorator, TokenClass::Preprocessor};
    }

    switch (c) {
        case L'(':
            advance();
            return {python::LParen, TokenClass::Punctuation};
        case L')':
            advance();
            return {python::RParen, TokenClass::Punctuation};
        case L'[':
            advance();
            return {python::LBracket, TokenClass::Punctuation};
        case L']':
            advance();
            return {python::RBracket, TokenClass::Punctuation};
        case L'{':
            advance();
            return {python::LBrace, TokenClass::Punctuation};
        case L'}':
            advance();
            return {python::RBrace, TokenClass::Punctuation};
        case L',':
            advance();
            return {python::Comma, TokenClass::Punctuation};
        case L';':
            advance();
            return {python::Semicolon, TokenClass::Punctuation};
        case L':':
            if (peekAt(1) == L'=') {
                break;   // the walrus operator
            }
            advance();
            return {python::Colon, TokenClass::Punctuation};
        case L'.':
            if (peekAt(1) == L'.' && peekAt(2) == L'.') {
                break;   // an ellipsis
            }
            advance();
            return {python::Dot, TokenClass::Punctuation};
        default:
            break;
    }
    return scanOperator();
}

}  // namespace lex
