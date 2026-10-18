#include "curvy/textoutline.h"

#include <newui/font.h>
#include <newui/fontmanager.h>

#include <algorithm>
#include <vector>

namespace curvy {

    namespace {
        // The BLFont for the named font and size; null when it cannot be resolved.
        BLFont* fontFor(const std::string& fontName, double fontSize, const TextStyle& style, newui::Font& holder) {
            holder = newui::Font(fontName.empty() ? defaultTextFontName() : fontName, float(std::max(1.0, fontSize)));
            holder.setBold(style.bold);
            holder.setItalic(style.italic);
            BLFont* font = holder.blFont();
            return font != nullptr && font->is_valid() ? font : nullptr;
        }

        // The characters of a UTF-8 line, each as its own substring.
        std::vector<std::string> characters(const std::string& line) {
            std::vector<std::string> out;
            for (std::size_t i = 0; i < line.size();) {
                std::size_t n = 1;
                unsigned char c = static_cast<unsigned char>(line[i]);
                if (c >= 0xF0) { n = 4; } else if (c >= 0xE0) { n = 3; } else if (c >= 0xC0) { n = 2; }
                out.push_back(line.substr(i, n));
                i += n;
            }
            return out;
        }

        double advanceOf(BLFont& font, const std::string& piece) {
            BLGlyphBuffer glyphs;
            glyphs.set_utf8_text(piece.c_str(), piece.size());
            font.shape(glyphs);
            BLTextMetrics metrics;
            font.get_text_metrics(glyphs, metrics);
            return metrics.advance.x;
        }

        // The width of a line: shaped as a whole (kerning kept) when there is no tracking, else character by character.
        double lineWidth(BLFont& font, const std::string& line, const TextStyle& style) {
            if (line.empty()) {
                return 0.0;
            }
            if (style.tracking == 0.0) {
                return advanceOf(font, line);
            }
            double width = 0.0;
            for (const std::string& c : characters(line)) {
                width += advanceOf(font, c) + style.tracking;
            }
            return width;
        }

        std::vector<std::string> linesOf(const std::string& text) {
            std::vector<std::string> lines;
            std::size_t start = 0;
            for (;;) {
                std::size_t end = text.find('\n', start);
                lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            return lines;
        }

        double shiftFor(int align, double blockWidth, double width) {
            return align == TextStyle::Center ? (blockWidth - width) * 0.5 : (align == TextStyle::Right ? blockWidth - width : 0.0);
        }
    }

    std::string defaultTextFontName() {
        return newui::FontManager::getSystemFont(newui::SystemUIFont::Message).name();
    }

    double textLineHeight(const std::string& fontName, double fontSize, const TextStyle& style) {
        newui::Font holder;
        BLFont* font = fontFor(fontName, fontSize, style, holder);
        if (font == nullptr) {
            return fontSize * 1.2 * style.leading;
        }
        const BLFontMetrics& m = font->metrics();
        return (m.ascent + m.descent + m.line_gap) * style.leading;
    }

    double textAdvance(const std::string& line, const std::string& fontName, double fontSize, const TextStyle& style) {
        newui::Font holder;
        BLFont* font = fontFor(fontName, fontSize, style, holder);
        if (font == nullptr || line.empty()) {
            return 0.0;
        }
        return lineWidth(*font, line, style);
    }

    double textLineOffset(const std::string& text, std::size_t lineIndex, const std::string& fontName, double fontSize, const TextStyle& style) {
        if (style.align == TextStyle::Left) {
            return 0.0;
        }
        newui::Font holder;
        BLFont* font = fontFor(fontName, fontSize, style, holder);
        if (font == nullptr) {
            return 0.0;
        }
        std::vector<std::string> lines = linesOf(text);
        double block = 0.0;
        for (const std::string& line : lines) {
            block = std::max(block, lineWidth(*font, line, style));
        }
        return lineIndex < lines.size() ? shiftFor(style.align, block, lineWidth(*font, lines[lineIndex], style)) : 0.0;
    }

    BLPath textOutline(const std::string& text, const std::string& fontName, double fontSize, const TextStyle& style) {
        BLPath result;
        newui::Font holder;
        BLFont* font = fontFor(fontName, fontSize, style, holder);
        if (font == nullptr) {
            return result;
        }
        const double lineHeight = textLineHeight(fontName, fontSize, style);
        std::vector<std::string> lines = linesOf(text);
        double block = 0.0;
        std::vector<double> widths;
        for (const std::string& line : lines) {
            widths.push_back(lineWidth(*font, line, style));
            block = std::max(block, widths.back());
        }
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].empty()) {
                continue;
            }
            const double x = shiftFor(style.align, block, widths[i]);
            const double y = double(i) * lineHeight;
            if (style.tracking == 0.0) {
                BLGlyphBuffer glyphs;
                glyphs.set_utf8_text(lines[i].c_str(), lines[i].size());
                font->shape(glyphs);
                BLPath one;
                font->get_glyph_run_outlines(glyphs.glyph_run(), one);
                one.transform(BLMatrix2D::make_translation(x, y));
                result.add_path(one);
            } else {
                double pen = x;
                for (const std::string& c : characters(lines[i])) {
                    BLGlyphBuffer glyphs;
                    glyphs.set_utf8_text(c.c_str(), c.size());
                    font->shape(glyphs);
                    BLPath one;
                    font->get_glyph_run_outlines(glyphs.glyph_run(), one);
                    one.transform(BLMatrix2D::make_translation(pen, y));
                    result.add_path(one);
                    pen += advanceOf(*font, c) + style.tracking;
                }
            }
        }
        return result;
    }

}
