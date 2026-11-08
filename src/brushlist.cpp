#include "curvy/brushlist.h"

#include "curvy/graphbrush.h"
#include "curvy/sketchobject.h"
#include "curvy/theme.h"

#include <newui/fontmanager.h>
#include <newui/mouse_constants.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <cmath>

namespace curvy {

    using namespace newui;

    BrushList::BrushList() {
        setName("BrushList");
        setVisible(true);
        onMouseDown.add(this, &BrushList::handleMouseDown);
        onMouseMove.add(this, &BrushList::handleMouseMove);
        onMouseUp.add(this, &BrushList::handleMouseUp);
        onMouseWheel.add(this, &BrushList::handleMouseWheel);
    }

    void BrushList::setSections(std::vector<Section> sections, std::size_t active) {
        sections_ = std::move(sections);
        active_ = active;
        // Samples of brushes that are gone (or were redefined: a new definition is a new object) are dropped.
        for (auto it = thumbnails_.begin(); it != thumbnails_.end();) {
            bool kept = false;
            for (const Section& section : sections_) {
                for (const std::shared_ptr<const BrushDef>& brush : section.brushes) {
                    kept = kept || brush.get() == it->first.first;
                }
            }
            it = kept ? std::next(it) : thumbnails_.erase(it);
        }
        layout();
        style().markDirty();
    }

    void BrushList::layout() {
        rows_.clear();
        float y = 0.0f;
        for (std::size_t s = 0; s < sections_.size(); ++s) {
            Row header;
            header.header = true;
            header.section = s;
            header.y = y;
            header.height = kHeaderHeight;
            rows_.push_back(header);
            y += kHeaderHeight;
            if (folded_.count(sections_[s].title) != 0) {
                continue;
            }
            for (const std::shared_ptr<const BrushDef>& brush : sections_[s].brushes) {
                Row row;
                row.section = s;
                row.brush = brush;
                row.y = y;
                row.height = kRowHeight;
                rows_.push_back(row);
                y += kRowHeight;
            }
        }
        contentHeight_ = y;
        setScroll(scroll_);
    }

    void BrushList::setScroll(float offset) {
        const float most = std::max(0.0f, contentHeight_ - getClientBounds().height());
        scroll_ = std::min(std::max(offset, 0.0f), most);
        style().markDirty();
    }

    int BrushList::rowAt(float y) const {
        const float at = y + scroll_;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (at >= rows_[i].y && at < rows_[i].y + rows_[i].height) {
                return int(i);
            }
        }
        return -1;
    }

    std::pair<float, float> BrushList::thumb() const {
        const float view = getClientBounds().height();
        if (!scrollable() || view <= 0.0f) {
            return {0.0f, 0.0f};
        }
        const float length = std::max(24.0f, view * view / contentHeight_);
        const float most = contentHeight_ - view;
        return {most > 0.0f ? scroll_ / most * (view - length) : 0.0f, length};
    }

    SyncReturn BrushList::handleMouseMove(View&, const Point& pt, std::uint32_t, std::uint32_t) {
        if (!draggingThumb_) {
            return SyncReturn::Ignored;
        }
        const float view = getClientBounds().height(), length = thumb().second;
        const float room = view - length;
        if (room > 0.0f) {
            setScroll((pt.y - getClientBounds().top() - thumbGrab_) / room * (contentHeight_ - view));
        }
        return SyncReturn::Handled;
    }

    SyncReturn BrushList::handleMouseUp(View&, const Point&, std::uint32_t, std::uint32_t) {
        const bool was = draggingThumb_;
        draggingThumb_ = false;
        style().markDirty();
        return was ? SyncReturn::Handled : SyncReturn::Ignored;
    }

    SyncReturn BrushList::handleMouseDown(View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
        if ((buttons & mbmLeftButton) == 0) {
            return SyncReturn::Ignored;
        }
        const Rect client = getClientBounds();
        if (scrollable() && pt.x - client.left() >= client.width() - kBarWidth) {   // the scroll bar: drag the thumb, or page towards the click
            const float y = pt.y - client.top();
            const auto [top, length] = thumb();
            if (y >= top && y <= top + length) {
                draggingThumb_ = true;
                thumbGrab_ = y - top;
                style().markDirty();
            } else {
                setScroll(scroll_ + (y < top ? -client.height() : client.height()) * 0.9f);
            }
            return SyncReturn::Handled;
        }
        const int index = rowAt(pt.y - client.top());
        if (index < 0) {
            return SyncReturn::Ignored;
        }
        const Row& row = rows_[std::size_t(index)];
        if (row.header) {
            const std::string title = sections_[row.section].title;
            if (folded_.erase(title) == 0) {
                folded_.insert(title);
            }
            layout();
            return SyncReturn::Handled;
        }
        picked_ = row.brush->id;
        pickedStar_ = pt.x - client.left() >= client.width() - kStarWidth - (scrollable() ? kBarWidth : 0.0f);
        onBrushPicked(*this);
        return SyncReturn::Handled;
    }

    SyncReturn BrushList::handleMouseWheel(View&, const Point&, float delta) {
        if (contentHeight_ <= getClientBounds().height()) {
            return SyncReturn::Ignored;
        }
        setScroll(scroll_ - delta * kRowHeight);
        return SyncReturn::Handled;
    }

    // A sample stroke made with the brush on a white tile: a gentle S across it, the artwork as big as the tile allows.
    const BLImage& BrushList::thumbnail(const std::shared_ptr<const BrushDef>& brush, int width, int height) {
        const auto key = std::make_pair(brush.get(), width);
        auto found = thumbnails_.find(key);
        if (found != thumbnails_.end()) {
            return found->second;
        }
        BLImage image(width, height, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
        ctx.fill_all();
        BLPath curve;
        const double margin = 8.0, mid = height * 0.5, rise = height * 0.22;
        curve.move_to(margin, mid + rise);
        curve.cubic_to(width * 0.4, mid + rise, width * 0.6, mid - rise, width - margin, mid - rise);
        std::unique_ptr<SketchObject> sample(SketchObject::makePath(curve));
        sample->setFill(SketchPaint::none());
        sample->setStroke(SketchPaint::solid(SketchColor::gray(0.0f)));
        StrokeOptions options;
        options.brush = brush;
        sample->setStrokeOptions(options);
        double weight = 6.0;
        if (brush->kind == BrushKind::Graph) {
            weight = (height - 6.0) / std::max(brush->reach(1.0) * 2.0, 1e-6);   // as wide as the row allows
        } else if (brush->kind != BrushKind::Calligraphic) {
            weight = std::min(1.0, (height - 6.0) / std::max(brush->reach(1.0) * 2.0, 1e-6));
        }
        sample->setStrokeWidth(weight);
        const GraphBrushBlockingScope waitForBrushes;   // the tile is cached, so it must not keep the hairline of a cook still queued
        sample->paint(ctx);
        ctx.end();
        return thumbnails_.emplace(key, std::move(image)).first->second;
    }

    void BrushList::paint(BLContext& ctx) {
        const Rect client = getClientBounds();
        if (client.width() <= 0.0f || client.height() <= 0.0f) {
            return;
        }
        Font font = FontManager::getSystemFont(SystemUIFont::Status);
        BLFont* blFont = font.blFont();
        const bool haveFont = blFont != nullptr && blFont->is_valid();
        const double ascent = haveFont ? blFont->metrics().ascent : 10.0;
        const double textHeight = haveFont ? ascent + blFont->metrics().descent : 12.0;
        ctx.save();
        ctx.clip_to_rect(BLRect(client.left(), client.top(), client.width(), client.height()));
        const Theme& theme = Theme::current();
        const auto rgb = [](std::uint32_t color, std::uint32_t alpha = 255) { return BLRgba32(Theme::argb(color, alpha)); };
        ctx.set_fill_style(rgb(theme.bgApp));
        ctx.fill_rect(BLRect(client.left(), client.top(), client.width(), client.height()));
        const float listWidth = client.width() - (scrollable() ? kBarWidth : 0.0f);   // rows run up to the scroll bar
        const int thumbWidth = std::max(8, int(listWidth - kStarWidth - 2.0f));
        for (const Row& row : rows_) {
            const double y = client.top() + row.y - scroll_;
            if (y + row.height < client.top() || y > client.top() + client.height()) {
                continue;
            }
            if (row.header) {
                ctx.set_fill_style(rgb(theme.slot));
                ctx.fill_rect(BLRect(client.left(), y, listWidth, row.height));
                if (haveFont) {
                    const std::string title = (folded_.count(sections_[row.section].title) != 0 ? "+ " : "- ") + sections_[row.section].title;
                    ctx.set_fill_style(rgb(theme.textPrimary));
                    ctx.fill_utf8_text(BLPoint(client.left() + 6.0, y + (row.height - textHeight) * 0.5 + ascent), *blFont, title.c_str(), title.size());
                }
                continue;
            }
            if (row.brush->id == active_) {
                ctx.set_fill_style(rgb(theme.accent));
                ctx.fill_rect(BLRect(client.left(), y, listWidth, row.height));
            }
            const BLImage& tile = thumbnail(row.brush, thumbWidth, int(row.height) - 2);
            ctx.blit_image(BLPoint(client.left() + 1.0, y + 1.0), tile);
            if (haveFont) {   // the name sits in a small white label at the left of the sample
                const std::string& name = row.brush->name;
                BLGlyphBuffer glyphs;
                glyphs.set_utf8_text(name.c_str(), name.size());
                blFont->shape(glyphs);
                BLTextMetrics metrics;
                blFont->get_text_metrics(glyphs, metrics);
                ctx.set_fill_style(BLRgba32(0xCCFFFFFFu));
                ctx.fill_rect(BLRect(client.left() + 1.0, y + 1.0, metrics.advance.x + 8.0, 14.0));
                ctx.set_fill_style(BLRgba32(0xFF222222u));
                ctx.fill_utf8_text(BLPoint(client.left() + 5.0, y + 1.0 + (14.0 - textHeight) * 0.5 + ascent), *blFont, name.c_str(), name.size());
            }
            // The star: filled when the brush is a favorite.
            const double cx = client.left() + listWidth - kStarWidth * 0.5, cy = y + row.height * 0.5;
            BLPath star;
            for (int i = 0; i < 10; ++i) {
                const double a = -1.5707963267948966 + i * 0.6283185307179586, r = i % 2 == 0 ? 7.0 : 3.0;
                if (i == 0) {
                    star.move_to(cx + r * std::cos(a), cy + r * std::sin(a));
                } else {
                    star.line_to(cx + r * std::cos(a), cy + r * std::sin(a));
                }
            }
            star.close();
            if (row.brush->favorite) {
                ctx.set_fill_style(rgb(theme.accent));
                ctx.fill_path(star);
            }
            ctx.set_stroke_style(rgb(row.brush->favorite ? theme.accent : theme.textMuted));
            ctx.set_stroke_width(1.0);
            ctx.stroke_path(star);
        }
        // The scroll bar: a track in the theme's slot colour, and a thumb that turns amber while it is dragged.
        if (scrollable()) {
            const double x = client.left() + client.width() - kBarWidth;
            ctx.set_fill_style(rgb(theme.slot));
            ctx.fill_rect(BLRect(x, client.top(), kBarWidth, client.height()));
            const auto [top, length] = thumb();
            ctx.set_fill_style(rgb(draggingThumb_ ? theme.accent : theme.border));
            ctx.fill_round_rect(BLRoundRect(x + 2.0, client.top() + top + 1.0, kBarWidth - 4.0, length - 2.0, 3.0));
        }
        ctx.restore();
    }

}
