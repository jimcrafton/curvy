#pragma once

#include "curvy/codelanguage.h"

#include <newui/text.h>
#include <newui/textstyle.h>

#include <memory>
#include <string>
#include <vector>

namespace curvy {

    // Colours for wrangle code (docs/wrangle.md), by the same style names as the Python editor so the user's colours apply to both:
    // "comment" (# to the end of the line), "keyword" (the bars of a declaration list `| gain = 1(0, 2), steps |` at the top),
    // "definition" (the 13 functions, when called), "constant" (a b c d, sampleIndex and the declared names, also in their later uses),
    // "identifier" (attributes), "number", "string" (the literal in
    // `label == "tree"`), "operator" and "punctuation". A squiggle ("problem") marks what the parser rejects at the character level: a
    // character that is not part of the language (% { [ ' ...), a single & or |, a number with a suffix (1.5f), an unterminated string
    // and a call of a function that does not exist. Syntax errors beyond that (a missing semicolon) come from checkWrangle().
    std::vector<newui::text::TextStyleRange> wrangleStyleRanges(const std::wstring& text);

    // The wrangle language for a CodeEditorView: colours, no folds, and a new line keeps the line's indentation.
    // @reflect ignore=true
    class WrangleLanguage : public CodeLanguage {
    public:
        static std::shared_ptr<const CodeLanguage> instance();

        std::shared_ptr<const newui::TextStyleSheet> styleSheet(bool dark, const std::map<std::string, std::uint32_t>& colors) const override;
        std::vector<newui::text::TextStyleRange> styleRanges(const std::wstring& text) const override;
    };

}
