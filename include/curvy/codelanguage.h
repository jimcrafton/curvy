#pragma once

#include <newui/text.h>
#include <newui/textstyle.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace curvy {

    // What a code editor (CodeEditorView) needs to know about a language. This base class is plain text: no colours, no folds, and a new line
    // that keeps the line's own indentation. A language overrides what it has (PythonLanguage, WrangleLanguage).
    // @reflect ignore=true
    class CodeLanguage {
    public:
        virtual ~CodeLanguage() = default;

        // The style sheet in the light or dark look, with the user's colours (0xRRGGBB by style name) on top; null keeps the control's own.
        virtual std::shared_ptr<const newui::TextStyleSheet> styleSheet(bool dark, const std::map<std::string, std::uint32_t>& colors) const;
        // The coloured runs of a text, and the squiggles under malformed tokens.
        virtual std::vector<newui::text::TextStyleRange> styleRanges(const std::wstring& text) const;
        // What a new line starts with after Enter at the end of `lineBeforeCaret` (an indent level is `indentWidth` spaces).
        virtual std::wstring newLineIndent(const std::wstring& lineBeforeCaret, std::size_t indentWidth) const;
        // The blocks that can be folded, none collapsed.
        virtual std::vector<newui::text::TextFold> folds(const std::wstring& text) const;
    };

}
