#include "curvy/wranglehighlight.h"

#include "curvy/pythonhighlight.h"
#include "curvy/wranglecheck.h"

#include <algorithm>
#include <cwctype>
#include <set>
#include <string>

namespace curvy {

    namespace {

        bool isIdentifierStart(wchar_t c) {
            return c == L'_' || (c < 128 && std::iswalpha(static_cast<wint_t>(c)) != 0);
        }

        bool isIdentifierPart(wchar_t c) {
            return isIdentifierStart(c) || (c >= L'0' && c <= L'9');
        }

        bool isDigit(wchar_t c) {
            return c >= L'0' && c <= L'9';
        }

        bool isSpace(wchar_t c) {
            return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
        }

        bool isFunction(const std::wstring& word) {
            for (const WrangleFunction& function : wrangleFunctions()) {
                if (word == std::wstring(function.name, function.name + std::char_traits<char>::length(function.name))) {
                    return true;
                }
            }
            return false;
        }

        // The first character after whitespace and comments from `at`, or 0 at the end.
        wchar_t nextSignificant(const std::wstring& text, std::size_t at) {
            while (at < text.size()) {
                if (isSpace(text[at])) {
                    ++at;
                } else if (text[at] == L'#') {
                    while (at < text.size() && text[at] != L'\n') {
                        ++at;
                    }
                } else {
                    return text[at];
                }
            }
            return 0;
        }

    }

    std::vector<newui::text::TextStyleRange> wrangleStyleRanges(const std::wstring& text) {
        std::vector<newui::text::TextStyleRange> ranges;
        const auto add = [&ranges](std::size_t start, std::size_t length, const char* style) {
            ranges.push_back({start, length, style, ""});
        };
        std::set<std::wstring> declared;
        bool atStatementStart = true;
        bool assigned = false;           // an assignment was seen: a '|' after it is not a declaration
        bool inDeclaration = false;      // between the bars of | name = value(min, max), ... |
        bool expectDeclaredName = false;
        int parenDepth = 0;              // inside the limits of one declaration
        std::size_t i = 0;
        const std::size_t size = text.size();
        while (i < size) {
            const wchar_t c = text[i];
            if (isSpace(c)) {
                ++i;
                continue;
            }
            if (c == L'#') {
                std::size_t end = i;
                while (end < size && text[end] != L'\n') {
                    ++end;
                }
                add(i, end - i, "comment");
                i = end;
                continue;
            }
            const bool startedStatement = atStatementStart;
            atStatementStart = false;
            if (startedStatement && !assigned && c == L'|') {
                inDeclaration = true;
                expectDeclaredName = true;
                parenDepth = 0;
                add(i, 1, "keyword");
                ++i;
                continue;
            }
            if (inDeclaration && c == L'|') {
                inDeclaration = false;
                expectDeclaredName = false;
                atStatementStart = true;   // another list may follow, until the first assignment
                add(i, 1, "keyword");
                ++i;
                continue;
            }
            if (startedStatement) {
                assigned = true;
            }
            if (c == L'"') {
                std::size_t end = i + 1;
                bool closed = false;
                while (end < size && text[end] != L'\n') {
                    if (text[end] == L'\\' && end + 1 < size && (text[end + 1] == L'"' || text[end + 1] == L'\\')) {
                        end += 2;
                        continue;
                    }
                    if (text[end] == L'"') {
                        closed = true;
                        ++end;
                        break;
                    }
                    ++end;
                }
                add(i, end - i, "string");
                if (!closed) {
                    add(i, end - i, "problem");
                }
                i = end;
                continue;
            }
            if (isDigit(c) || c == L'.') {
                std::size_t end = i;
                while (end < size && isDigit(text[end])) {
                    ++end;
                }
                if (end < size && text[end] == L'.') {
                    ++end;
                    while (end < size && isDigit(text[end])) {
                        ++end;
                    }
                }
                if (end < size && (text[end] == L'e' || text[end] == L'E')) {
                    std::size_t exponent = end + 1;
                    if (exponent < size && (text[exponent] == L'+' || text[exponent] == L'-')) {
                        ++exponent;
                    }
                    if (exponent < size && isDigit(text[exponent])) {
                        while (exponent < size && isDigit(text[exponent])) {
                            ++exponent;
                        }
                        end = exponent;
                    }
                }
                if (end == i + 1 && c == L'.') {
                    add(i, 1, "problem");   // a lone dot is not a number
                    i = end;
                    continue;
                }
                add(i, end - i, "number");
                if (end < size && isIdentifierStart(text[end])) {
                    std::size_t suffix = end;
                    while (suffix < size && isIdentifierPart(text[suffix])) {
                        ++suffix;
                    }
                    add(end, suffix - end, "problem");   // 1.5f: the language has no suffixes
                    end = suffix;
                }
                i = end;
                continue;
            }
            if (isIdentifierStart(c)) {
                std::size_t end = i + 1;
                while (end < size) {
                    if (isIdentifierPart(text[end])) {
                        ++end;
                    } else if (text[end] == L'.' && end + 1 < size && isIdentifierStart(text[end + 1]) && text[end + 1] != L'_') {
                        ++end;   // N.x, group.top: one name, the way the parser reads it
                    } else {
                        break;
                    }
                }
                const std::wstring word = text.substr(i, end - i);
                const wchar_t after = nextSignificant(text, end);
                if (inDeclaration && expectDeclaredName && parenDepth == 0) {
                    expectDeclaredName = false;
                    declared.insert(word);
                    add(i, end - i, "constant");
                } else if (after == L'(') {
                    add(i, end - i, "definition");
                    if (!isFunction(word)) {
                        add(i, end - i, "problem");   // unknown function
                    }
                } else if (word == L"a" || word == L"b" || word == L"c" || word == L"d" || word == L"sampleIndex" || declared.count(word) != 0) {
                    add(i, end - i, "constant");
                } else {
                    add(i, end - i, "identifier");
                }
                i = end;
                continue;
            }
            // Operators; a doubled one is a single token.
            const wchar_t next = i + 1 < size ? text[i + 1] : 0;
            if ((c == L'=' || c == L'!' || c == L'<' || c == L'>') && next == L'=') {
                add(i, 2, "operator");
                i += 2;
                continue;
            }
            if ((c == L'&' && next == L'&') || (c == L'|' && next == L'|')) {
                add(i, 2, "operator");
                i += 2;
                continue;
            }
            if (c == L'=' || c == L'<' || c == L'>' || c == L'!' || c == L'?' || c == L':' || c == L'+' || c == L'-' || c == L'*' || c == L'/') {
                add(i, 1, "operator");
            } else if (c == L'(' || c == L')' || c == L',') {
                if (inDeclaration) {
                    if (c == L'(') {
                        ++parenDepth;
                    } else if (c == L')' && parenDepth > 0) {
                        --parenDepth;
                    } else if (c == L',' && parenDepth == 0) {
                        expectDeclaredName = true;   // the next name is another declaration
                    }
                }
                add(i, 1, "punctuation");
            } else if (c == L';') {
                add(i, 1, "punctuation");
                atStatementStart = true;
                expectDeclaredName = false;
            } else {
                add(i, 1, "problem");
            }
            ++i;
        }
        return ranges;
    }

    std::shared_ptr<const CodeLanguage> WrangleLanguage::instance() {
        static const std::shared_ptr<const CodeLanguage> language = std::make_shared<const WrangleLanguage>();
        return language;
    }

    std::shared_ptr<const newui::TextStyleSheet> WrangleLanguage::styleSheet(bool dark, const std::map<std::string, std::uint32_t>& colors) const {
        return pythonStyleSheet(dark, colors);   // the same style names, so the same colours and the user's overrides
    }

    std::vector<newui::text::TextStyleRange> WrangleLanguage::styleRanges(const std::wstring& text) const {
        return wrangleStyleRanges(text);
    }

}
