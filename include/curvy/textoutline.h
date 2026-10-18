#pragma once

#include <blend2d/blend2d.h>

#include <cstddef>
#include <string>

namespace curvy {

    // The glyph outlines of text, as a path: what a Text object draws, strokes and hit-tests like any other shape.

    // How a text is set besides its font and size: weight and slant, where lines sit against each other (alignment), the extra
    // space after every character (tracking, in points) and the line distance as a multiple of the font's own (leading).
    // @reflect ignore=true
    struct TextStyle {
        enum Align { Left = 0, Center = 1, Right = 2 };
        bool bold = false;
        bool italic = false;
        int align = Left;
        double tracking = 0.0;
        double leading = 1.0;
        bool isDefault() const { return !bold && !italic && align == Left && tracking == 0.0 && leading == 1.0; }
        bool operator==(const TextStyle& o) const {
            return bold == o.bold && italic == o.italic && align == o.align && tracking == o.tracking && leading == o.leading;
        }
        bool operator!=(const TextStyle& o) const { return !(*this == o); }
    };

    // The font a new text uses when none is named (the system's message font).
    std::string defaultTextFontName();

    // Outlines of `text` (UTF-8; '\n' starts a new line) in the named font ("" = the default) at `fontSize` points. The first
    // line's baseline sits on y = 0 and the block starts at x = 0 (lines are shifted right by their alignment); further lines are
    // placed one line height lower (y grows downward). Spaces make no outline, so text of only blanks (or no text) returns an empty path.
    BLPath textOutline(const std::string& text, const std::string& fontName, double fontSize, const TextStyle& style = TextStyle());

    // The distance between two lines of the named font at `fontSize` (the leading applied).
    double textLineHeight(const std::string& fontName, double fontSize, const TextStyle& style = TextStyle());

    // The width of one line of text (no '\n' in it), tracking included - where the editing caret sits at the end of the line.
    double textAdvance(const std::string& line, const std::string& fontName, double fontSize, const TextStyle& style = TextStyle());

    // How far line number `lineIndex` of `text` starts from the left edge of the block: 0 for left alignment, and for the others the
    // room that is left over beside it.
    double textLineOffset(const std::string& text, std::size_t lineIndex, const std::string& fontName, double fontSize, const TextStyle& style);

}
