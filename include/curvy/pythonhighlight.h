#pragma once

#include "curvy/codelanguage.h"

#include <newui/text.h>
#include <newui/textstyle.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace curvy {

    // Python syntax highlighting for a newui::TextFoldingControl, on the lex library (3rdparty/lex; the same lexer-to-styles bridge the
    // C++ editor of cpp_codetools uses, but synchronous: a script is small, so it is lexed on the spot).

    // The syntax styles a user can recolour, in the order a settings page lists them: the name the style sheet and the settings use
    // ("keyword") and the words to show for it.
    struct SyntaxStyleInfo {
        const char* name;
        const char* label;
    };
    const std::vector<SyntaxStyleInfo>& pythonSyntaxStyles();
    // The two colours besides the syntax styles that the settings can override: "text" (everything not coloured) and "background".
    const std::vector<SyntaxStyleInfo>& pythonBaseColors();

    // The colour (0xRRGGBB) the light or dark look gives a syntax style, or "text" (everything not coloured) or "background". Only pass
    // names from pythonSyntaxStyles(): any other gives 0, which is also black.
    std::uint32_t pythonDefaultColor(const std::string& style, bool dark);

    // One style per syntax style, for the control's setStyleSheet(), in the light or the dark look. `colors` replaces the colour of the
    // styles it names (0xRRGGBB); the others keep the look's.
    std::shared_ptr<const newui::TextStyleSheet> pythonStyleSheet(bool dark, const std::map<std::string, std::uint32_t>& colors = {});

    // The coloured runs of `text`, and a squiggle ("problem") under each malformed token (an unterminated string, a bad number, a stray
    // character), for setStyledRanges().
    std::vector<newui::text::TextStyleRange> pythonStyleRanges(const std::wstring& text);

    // What a new line should start with after pressing Enter at the end of `lineBeforeCaret` (the line's text up to the caret): the line's
    // own indentation, one level (`indentWidth` spaces) deeper when the code on it ends in ':' (a trailing comment does not count), and
    // one level shallower after a line that starts with pass, return, break, continue or raise.
    std::wstring pythonNewLineIndent(const std::wstring& lineBeforeCaret, std::size_t indentWidth = 4);

    // A fold for every block: from the end of a line that ends in ':' to the end of the last line of its indented block, none collapsed.
    // Lines inside a multi-line string or brackets, blank lines and comment-only lines do not decide where a block ends.
    std::vector<newui::text::TextFold> pythonFolds(const std::wstring& text);

    // The functions above as a CodeLanguage, for a CodeEditorView.
    // @reflect ignore=true
    class PythonLanguage : public CodeLanguage {
    public:
        static std::shared_ptr<const CodeLanguage> instance();

        std::shared_ptr<const newui::TextStyleSheet> styleSheet(bool dark, const std::map<std::string, std::uint32_t>& colors) const override;
        std::vector<newui::text::TextStyleRange> styleRanges(const std::wstring& text) const override;
        std::wstring newLineIndent(const std::wstring& lineBeforeCaret, std::size_t indentWidth) const override;
        std::vector<newui::text::TextFold> folds(const std::wstring& text) const override;
    };

}
