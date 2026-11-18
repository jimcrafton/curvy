#include "curvy/assetlist.h"

#include <newui/fontmanager.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <any>
#include <string>

namespace curvy {

    namespace {
        const BLRgba32 kMuted(0xFFA0A0A0u);
        const BLRgba32 kAmber(0xFFFFB454u);
        const BLRgba32 kChipFill(0xFF2E2E2Eu);
        const BLRgba32 kChipBorder(0xFF4A4A4Au);
        constexpr float kBadgeFontSize = 10.0f;
        constexpr double kChipPadding = 7.0;
        constexpr double kChipHeight = 16.0;

        double textWidth(const BLFont& font, const std::string& text) {
            BLGlyphBuffer glyphs;
            glyphs.set_utf8_text(text.c_str(), text.size());
            font.shape(glyphs);
            BLTextMetrics metrics;
            font.get_text_metrics(glyphs, metrics);
            return metrics.advance.x;
        }

        // Baseline that centres the font's line on `centerY`.
        double baselineFor(const BLFont& font, double centerY) {
            const BLFontMetrics& metrics = font.metrics();
            return centerY - (metrics.ascent + metrics.descent) * 0.5 + metrics.ascent;
        }

        void drawText(BLContext& ctx, const BLFont& font, double x, double centerY, const std::string& text, BLRgba32 color) {
            ctx.set_fill_style(color);
            ctx.fill_utf8_text(BLPoint(x, baselineFor(font, centerY)), font, text.c_str(), text.size());
        }
    }

    void AssetListModel::setRows(std::vector<AssetListRow> rows) {
        rows_ = std::move(rows);
        onChanged(*this);
    }

    void AssetListModel::clear() {
        rows_.clear();
        newui::ListModel::clear();
    }

    std::any AssetListModel::value(const std::any& key) {
        const std::size_t* index = std::any_cast<std::size_t>(&key);
        if (index == nullptr || *index >= rows_.size()) {
            return std::any();
        }
        return rows_[*index].name;
    }

    void AssetRowItem::paint(BLContext& ctx, const newui::Rect& rect, std::size_t index, newui::ListController& controller) {
        Item::paint(ctx, rect);
        auto* model = dynamic_cast<AssetListModel*>(controller.model());
        const AssetListRow* row = model != nullptr ? model->row(index) : nullptr;
        newui::Font font = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
        BLFont* base = font.blFont();
        if (row == nullptr || base == nullptr || !base->is_valid()) {
            return;
        }
        BLFont badgeFont = *base;
        badgeFont.set_size(kBadgeFontSize);

        const newui::Rect& bounds = clientBounds();
        const double centerY = bounds.top() + bounds.size().height * 0.5;
        double right = bounds.left() + bounds.size().width - 8.0;

        ctx.save();
        const newui::UIColorRole role = isSelected() ? newui::UIColorRole::HighlightText : newui::UIColorRole::ControlText;
        const BLRgba32 textColor = newui::UIColorManager::colorFor(role).toBLRgba32();
        const std::string version = "v" + std::to_string(row->version);
        right -= textWidth(*base, version);
        drawText(ctx, *base, right, centerY, version, isSelected() ? textColor : kMuted);   // grey would not read on the selection colour
        right -= 10.0;
        for (std::size_t b = row->badges.size(); b-- > 0;) {
            const std::string& badge = row->badges[b];
            const bool warn = badge.rfind("Outdated", 0) == 0;
            const double width = textWidth(badgeFont, badge) + 2.0 * kChipPadding;
            right -= width;
            const BLRoundRect chip(right, centerY - kChipHeight * 0.5, width, kChipHeight, kChipHeight * 0.5);
            ctx.set_fill_style(kChipFill);
            ctx.fill_round_rect(chip);
            ctx.set_stroke_style(warn ? kAmber : kChipBorder);
            ctx.set_stroke_width(1.0);
            ctx.stroke_round_rect(chip);
            drawText(ctx, badgeFont, right + kChipPadding, centerY, badge, warn ? kAmber : kMuted);
            right -= 4.0;
        }
        const double nameLeft = bounds.left() + 8.0;
        ctx.clip_to_rect(BLRect(nameLeft, bounds.top(), (std::max)(right - 4.0 - nameLeft, 0.0), bounds.size().height));
        drawText(ctx, *base, nameLeft, centerY, row->name, textColor);
        ctx.restore();
    }

}
