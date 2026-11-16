#include "curvy/pythonhighlight.h"

#include <lex/highlight.h>
#include <lex/python_language.h>
#include <lex/python_lexer.h>

#include <algorithm>

namespace curvy {

    namespace {

        newui::Color colorFromArgb(std::uint32_t argb) {
            return newui::Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f);
        }

        const char* styleName(lex::StyleId style) {
            switch (style) {
                case lex::StyleId::Comment:
                    return "comment";
                case lex::StyleId::Keyword:
                    return "keyword";
                case lex::StyleId::Constant:
                    return "constant";
                case lex::StyleId::String:
                    return "string";
                case lex::StyleId::Number:
                    return "number";
                case lex::StyleId::Operator:
                    return "operator";
                case lex::StyleId::Punctuation:
                    return "punctuation";
                case lex::StyleId::Preprocessor:
                    return "decorator";
                case lex::StyleId::Identifier:
                    return "identifier";
                case lex::StyleId::PropertyName:
                    return "definition";
                case lex::StyleId::Error:
                    return "error";
                default:
                    return nullptr;   // Default: the control's own look
            }
        }

        constexpr const char* kProblemStyle = "problem";

        // What a physical line is for working out blocks.
        struct LineInfo {
            std::size_t start = 0;
            std::size_t end = 0;        // end of its text, before the line break
            int indent = 0;
            bool blank = false;         // only whitespace, or only a comment
            bool continuation = false;  // inside a multi-line string or brackets
            bool opensBlock = false;    // its code ends in ':'
        };

        int indentOf(std::wstring_view line) {
            int width = 0;
            for (const wchar_t c : line) {
                if (c == L' ') {
                    ++width;
                } else if (c == L'\t') {
                    width = (width / 4 + 1) * 4;
                } else {
                    break;
                }
            }
            return width;
        }

    }

    const std::vector<SyntaxStyleInfo>& pythonSyntaxStyles() {
        static const std::vector<SyntaxStyleInfo> styles = {
            {"keyword", "Keyword"},       {"constant", "True, False, None"}, {"string", "String"},      {"number", "Number"},
            {"comment", "Comment"},       {"identifier", "Name"},            {"definition", "Function, class"}, {"decorator", "Decorator"},
            {"operator", "Operator"},     {"punctuation", "Punctuation"},
        };
        return styles;
    }

    const std::vector<SyntaxStyleInfo>& pythonBaseColors() {
        static const std::vector<SyntaxStyleInfo> colors = {{"text", "Text"}, {"background", "Background"}};
        return colors;
    }

    std::uint32_t pythonDefaultColor(const std::string& style, bool dark) {
        const lex::Theme theme = dark ? lex::Theme::dark() : lex::Theme::light();
        if (style == "text") {
            return theme.style(lex::StyleId::Default).foreground & 0xFFFFFFu;
        }
        if (style == "background") {
            return theme.background & 0xFFFFFFu;
        }
        for (std::size_t i = 0; i < lex::kStyleCount; ++i) {
            const lex::StyleId id = static_cast<lex::StyleId>(i);
            const char* name = styleName(id);
            if (name != nullptr && style == name) {
                return theme.style(id).foreground & 0xFFFFFFu;
            }
        }
        return 0;
    }

    std::shared_ptr<const newui::TextStyleSheet> pythonStyleSheet(bool dark, const std::map<std::string, std::uint32_t>& colors) {
        const lex::Theme theme = dark ? lex::Theme::dark() : lex::Theme::light();
        auto sheet = std::make_shared<newui::TextStyleSheet>();
        for (std::size_t i = 0; i < lex::kStyleCount; ++i) {
            const lex::StyleId id = static_cast<lex::StyleId>(i);
            const char* name = styleName(id);
            if (name == nullptr) {
                continue;
            }
            const lex::TextStyle& look = theme.style(id);
            auto* style = new newui::TextStyle(name);
            const auto chosen = colors.find(name);
            style->setColor(colorFromArgb(chosen != colors.end() ? (0xFF000000u | chosen->second) : look.foreground));
            style->setBold((look.font & lex::FontFlag_Bold) != 0);
            style->setItalic((look.font & lex::FontFlag_Italic) != 0);
            sheet->addStyle(style);
        }
        auto* problem = new newui::TextStyle(kProblemStyle);
        problem->setDecoration(newui::text::TextDecorationKind::Squiggle);
        problem->setDecorationColor(colorFromArgb(theme.problemUnderline));
        sheet->addStyle(problem);
        return sheet;
    }

    std::vector<newui::text::TextStyleRange> pythonStyleRanges(const std::wstring& text) {
        std::vector<newui::text::TextStyleRange> ranges;
        if (text.empty()) {
            return ranges;
        }
        lex::SyntaxHighlighter highlighter(lex::pythonLanguage());
        highlighter.setText(text);
        for (std::size_t line = 0; line < highlighter.lineCount(); ++line) {
            const std::size_t lineStart = highlighter.lineStart(line);
            for (const lex::HighlightSpan& span : highlighter.spans(line)) {
                if (span.length == 0) {
                    continue;
                }
                if (span.isProblem()) {
                    ranges.push_back({lineStart + span.column, span.length, kProblemStyle, ""});
                }
                if (const char* name = styleName(span.style)) {
                    ranges.push_back({lineStart + span.column, span.length, name, ""});
                }
            }
        }
        return ranges;
    }

    std::wstring pythonNewLineIndent(const std::wstring& lineBeforeCaret, std::size_t indentWidth) {
        std::size_t width = 0;
        while (width < lineBeforeCaret.size() && (lineBeforeCaret[width] == L' ' || lineBeforeCaret[width] == L'\t')) {
            ++width;
        }
        std::wstring indent = lineBeforeCaret.substr(0, width);

        const std::vector<lex::HighlightSpan> spans = lex::highlightLine(lex::pythonLanguage(), lineBeforeCaret);
        const lex::HighlightSpan* first = nullptr;
        const lex::HighlightSpan* last = nullptr;
        int open = 0;
        for (const lex::HighlightSpan& span : spans) {
            if (span.kind == lex::kind::LineComment) {
                continue;
            }
            if (first == nullptr) {
                first = &span;
            }
            last = &span;
            if (span.kind == lex::python::LParen || span.kind == lex::python::LBracket || span.kind == lex::python::LBrace) {
                ++open;
            } else if (span.kind == lex::python::RParen || span.kind == lex::python::RBracket || span.kind == lex::python::RBrace) {
                --open;
            }
        }
        // A colon inside a bracket (a dictionary entry, a slice) does not open a block.
        if (last != nullptr && last->kind == lex::python::Colon && open <= 0) {
            return indent + std::wstring(indentWidth, L' ');
        }
        if (first != nullptr && first->kind == lex::python::Keyword) {
            const std::wstring word = lineBeforeCaret.substr(first->column, first->length);
            if (word == L"pass" || word == L"return" || word == L"break" || word == L"continue" || word == L"raise") {
                if (!indent.empty() && indent.back() == L'\t') {
                    indent.pop_back();
                } else if (indent.size() >= indentWidth) {
                    indent.resize(indent.size() - indentWidth);
                }
            }
        }
        return indent;
    }

    std::vector<newui::text::TextFold> pythonFolds(const std::wstring& text) {
        std::vector<newui::text::TextFold> folds;
        if (text.empty()) {
            return folds;
        }
        lex::SyntaxHighlighter highlighter(lex::pythonLanguage());
        highlighter.setText(text);

        std::vector<LineInfo> lines(highlighter.lineCount());
        int depth = 0;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            LineInfo& info = lines[i];
            const std::wstring_view content = highlighter.lineText(i);
            info.start = highlighter.lineStart(i);
            info.end = info.start + content.size();
            info.indent = indentOf(content);
            info.continuation = highlighter.entryState(i).mode != lex::python::ModeCode || depth > 0;
            bool onlyComment = true;
            const lex::HighlightSpan* last = nullptr;
            for (const lex::HighlightSpan& span : highlighter.spans(i)) {
                if (span.kind == lex::kind::LineComment) {
                    continue;
                }
                onlyComment = false;
                last = &span;
                if (span.kind == lex::python::LParen || span.kind == lex::python::LBracket || span.kind == lex::python::LBrace) {
                    ++depth;
                } else if ((span.kind == lex::python::RParen || span.kind == lex::python::RBracket || span.kind == lex::python::RBrace) && depth > 0) {
                    --depth;
                }
            }
            info.blank = onlyComment;
            info.opensBlock = !info.continuation && last != nullptr && last->kind == lex::python::Colon && depth == 0;
        }

        struct Open {
            std::size_t line;
            int indent;
        };
        std::vector<Open> open;
        // The last line before `before` that has code (or a continuation of it): where a block that ended there stops.
        auto lastCodeLine = [&](std::size_t before) {
            std::size_t line = before;
            while (line > 0) {
                --line;
                if (!lines[line].blank || lines[line].continuation) {
                    return line;
                }
            }
            return std::size_t(0);
        };
        auto close = [&](const Open& block, std::size_t before) {
            const std::size_t endLine = lastCodeLine(before);
            if (endLine <= block.line) {
                return;
            }
            newui::text::TextFold fold;
            fold.start = lines[block.line].end;
            fold.length = lines[endLine].end - fold.start;
            if (fold.length > 0) {
                folds.push_back(std::move(fold));
            }
        };

        for (std::size_t i = 0; i < lines.size(); ++i) {
            const LineInfo& info = lines[i];
            if (info.blank || info.continuation) {
                continue;
            }
            while (!open.empty() && open.back().indent >= info.indent) {
                close(open.back(), i);
                open.pop_back();
            }
            if (info.opensBlock) {
                open.push_back({i, info.indent});
            }
        }
        while (!open.empty()) {
            close(open.back(), lines.size());
            open.pop_back();
        }
        std::sort(folds.begin(), folds.end(), [](const newui::text::TextFold& a, const newui::text::TextFold& b) {
            return a.start != b.start ? a.start < b.start : a.length > b.length;
        });
        return folds;
    }

}
