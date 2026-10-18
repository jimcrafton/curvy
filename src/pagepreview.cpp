#include "curvy/pagepreview.h"

#include <newui/fontmanager.h>

#include <cmath>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    PagePreview::PagePreview() {
        setName("PagePreview");
        setVisible(true);
    }

    void PagePreview::setPageWidth(float width) {
        if (width <= 0.0f) {
            throw std::runtime_error("PagePreview::setPageWidth: width must be positive");
        }
        pageWidth_ = width;
        style().markDirty();
    }

    void PagePreview::setPageHeight(float height) {
        if (height <= 0.0f) {
            throw std::runtime_error("PagePreview::setPageHeight: height must be positive");
        }
        pageHeight_ = height;
        style().markDirty();
    }

    void PagePreview::setWidthLabel(const std::string& text) {
        widthLabel_ = text;
        style().markDirty();
    }

    void PagePreview::setHeightLabel(const std::string& text) {
        heightLabel_ = text;
        style().markDirty();
    }

    // A small dark pill with white text, centered on (centerX, centerY) - readable on both the
    // gray pasteboard and the white page.
    void PagePreview::paintLabel(BLContext& ctx, const std::string& text, double centerX, double centerY) const {
        if (text.empty()) {
            return;
        }
        Font font = FontManager::getSystemFont(SystemUIFont::Status);
        BLFont* blFont = font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            return;
        }

        BLGlyphBuffer glyphs;
        glyphs.set_utf8_text(text.c_str(), text.size());
        blFont->shape(glyphs);
        BLTextMetrics metrics;
        blFont->get_text_metrics(glyphs, metrics);
        const BLFontMetrics& fontMetrics = blFont->metrics();
        double textW = metrics.advance.x;
        double textH = fontMetrics.ascent + fontMetrics.descent;

        ctx.save();
        ctx.set_fill_style(BLRgba32(0x99000000u));
        ctx.fill_round_rect(BLRoundRect(centerX - textW * 0.5 - 5.0, centerY - textH * 0.5 - 1.0, textW + 10.0, textH + 2.0, 2.0));
        ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
        ctx.fill_utf8_text(BLPoint(centerX - textW * 0.5, centerY - textH * 0.5 + fontMetrics.ascent), *blFont, text.c_str(), text.size());
        ctx.restore();
    }

    void PagePreview::paint(BLContext& ctx) {
        Rect client = getClientBounds();
        if (client.width() <= 0.0f || client.height() <= 0.0f) {
            return;
        }

        // Pasteboard.
        ctx.save();
        ctx.set_fill_style(BLRgba32(0xFF7A7A7Au));
        ctx.fill_rect(BLRect(client.left(), client.top(), client.width(), client.height()));

        // Page: as large as fits inside the margin, keeping the aspect ratio, centered.
        double availW = double(client.width()) - 2.0 * double(kMargin);
        double availH = double(client.height()) - 2.0 * double(kMargin);
        if (availW < 8.0 || availH < 8.0) {
            ctx.restore();
            return;
        }
        double scale = std::fmin(availW / double(pageWidth_), availH / double(pageHeight_));
        double pageW = std::fmax(8.0, double(pageWidth_) * scale);
        double pageH = std::fmax(8.0, double(pageHeight_) * scale);
        double pageLeft = client.left() + (double(client.width()) - pageW) * 0.5;
        double pageTop = client.top() + (double(client.height()) - pageH) * 0.5;

        // Drop shadow, page, then a light grid so a blank page doesn't look like a hole.
        ctx.set_fill_style(BLRgba32(0x55000000u));
        ctx.fill_rect(BLRect(pageLeft + 2.0, pageTop + 3.0, pageW, pageH));
        ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
        ctx.fill_rect(BLRect(pageLeft, pageTop, pageW, pageH));

        BLPath grid;
        for (double x = 12.0; x < pageW; x += 12.0) {
            double px = std::floor(pageLeft + x) + 0.5;
            grid.move_to(px, pageTop);
            grid.line_to(px, pageTop + pageH);
        }
        for (double y = 12.0; y < pageH; y += 12.0) {
            double py = std::floor(pageTop + y) + 0.5;
            grid.move_to(pageLeft, py);
            grid.line_to(pageLeft + pageW, py);
        }
        ctx.set_stroke_style(BLRgba32(0xFFEEEEEEu));
        ctx.set_stroke_width(1.0);
        ctx.stroke_path(grid);
        ctx.restore();

        paintLabel(ctx, widthLabel_, pageLeft + pageW * 0.5, pageTop + pageH + 14.0);
        paintLabel(ctx, heightLabel_, pageLeft + pageW + 4.0 + 18.0, pageTop + pageH * 0.5);
    }

}
