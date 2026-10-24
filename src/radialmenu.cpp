#include "curvy/radialmenu.h"
#include "curvy/theme.h"

#include <newui/font.h>
#include <newui/fontmanager.h>
#include <newui/mouse_constants.h>

#include <algorithm>
#include <cmath>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        BLRgba32 rgb(std::uint32_t value) { return BLRgba32(Theme::argb(value)); }
    }

    Point RadialLayout::slotCenter(int index, int count) {
        const double angle = -kPi / 2.0 + 2.0 * kPi * double(index) / double(std::max(count, 1));
        return Point(float(ringRadius * std::cos(angle)), float(ringRadius * std::sin(angle)));
    }

    Point RadialLayout::optionBoxCenter(int index, int count) {
        const Point at = slotCenter(index, count);
        const double length = std::hypot(double(at.x), double(at.y));
        const double ux = length > 0.0 ? double(at.x) / length : 0.0, uy = length > 0.0 ? double(at.y) / length : -1.0;
        const double toSide = std::fabs(ux) > 1e-6 ? slotWidth * 0.5 / std::fabs(ux) : 1e9;
        const double toEnd = std::fabs(uy) > 1e-6 ? slotHeight * 0.5 / std::fabs(uy) : 1e9;
        const double out = std::min(toSide, toEnd) + 14.0;
        return Point(float(double(at.x) + ux * out), float(double(at.y) + uy * out));
    }

    int RadialLayout::slotAt(double dx, double dy, int count) {
        const double distance = std::hypot(dx, dy);
        if (distance < hubRadius) {
            return hub;
        }
        if (distance > reach || count <= 0) {
            return outside;
        }
        double turns = (std::atan2(dy, dx) + kPi / 2.0) / (2.0 * kPi);
        turns -= std::floor(turns);
        return int(std::lround(turns * count)) % count;
    }

    RadialMenu::RadialMenu() {
        setName("radialMenu");
        setVisible(false);
        onMouseDown.add(this, &RadialMenu::handleMouseDown);
        onMouseMove.add(this, &RadialMenu::handleMouseMove);
        onMouseUp.add(this, &RadialMenu::handleMouseUp);
    }

    void RadialMenu::open(const RadialRing& ring, const Point& center, bool openedByPress) {
        ring_ = ring;
        openedByPress_ = openedByPress;
        const Rect client = getClientBounds();
        const double margin = RadialLayout::ringRadius + RadialLayout::slotWidth * 0.5 + 26.0;   // room for the option boxes past the slots
        const double x = std::clamp(double(center.x), client.left() + margin, std::max(client.left() + margin, client.left() + client.width() - margin));
        const double y = std::clamp(double(center.y), client.top() + margin, std::max(client.top() + margin, client.top() + client.height() - margin));
        center_ = Point(float(x), float(y));
        hot_ = RadialLayout::hub;
        open_ = true;
        setVisible(true);
        style().markDirty();
    }

    void RadialMenu::close() {
        if (!open_) {
            return;
        }
        open_ = false;
        setVisible(false);
    }

    int RadialMenu::hitAt(const Point& pt) const {
        return RadialLayout::slotAt(double(pt.x) - double(center_.x), double(pt.y) - double(center_.y), int(ring_.slots.size()));
    }

    int RadialMenu::optionBoxAt(const Point& pt) const {
        const int count = int(ring_.slots.size());
        for (int i = 0; i < count; ++i) {
            if (ring_.slots[std::size_t(i)].optionsCommand.empty()) {
                continue;
            }
            const Point box = RadialLayout::optionBoxCenter(i, count);
            if (std::hypot(double(pt.x) - double(center_.x) - double(box.x), double(pt.y) - double(center_.y) - double(box.y)) <= RadialLayout::optionBoxReach) {
                return i;
            }
        }
        return -1;
    }

    void RadialMenu::choose(int hit, bool options) {
        std::string command;
        if (hit == RadialLayout::hub) {
            command = ring_.hubCommand;
        } else if (hit >= 0 && hit < int(ring_.slots.size())) {
            const RadialSlot& slot = ring_.slots[std::size_t(hit)];
            command = options && !slot.optionsCommand.empty() ? slot.optionsCommand : slot.command;
        }
        close();
        if (!command.empty()) {
            onCommand(*this, command);
        }
    }

    bool RadialMenu::handleKey(const std::string& key, bool options) {
        if (!open_) {
            return false;
        }
        for (std::size_t i = 0; i < ring_.slots.size(); ++i) {
            if (!ring_.slots[i].key.empty() && ring_.slots[i].key == key) {
                choose(int(i), options);
                return true;
            }
        }
        return false;
    }

    bool RadialMenu::handleEscape() {
        if (!open_) {
            return false;
        }
        close();
        return true;
    }

    SyncReturn RadialMenu::handleMouseDown(View&, const Point& pt, std::uint32_t, std::uint32_t) {
        if (!open_) {
            return SyncReturn::Ignored;
        }
        if (const int box = optionBoxAt(pt); box >= 0) {
            choose(box, true);
            return SyncReturn::Handled;
        }
        const int hit = hitAt(pt);
        if (hit == RadialLayout::outside) {
            close();
        } else {
            choose(hit);
        }
        return SyncReturn::Handled;
    }

    SyncReturn RadialMenu::handleMouseMove(View&, const Point& pt, std::uint32_t, std::uint32_t) {
        if (!open_) {
            return SyncReturn::Ignored;
        }
        const int hit = hitAt(pt);
        if (hit != hot_) {
            hot_ = hit;
            style().markDirty();
        }
        return SyncReturn::Handled;
    }

    SyncReturn RadialMenu::handleMouseUp(View&, const Point& pt, std::uint32_t, std::uint32_t) {
        if (!open_ || !openedByPress_) {
            return SyncReturn::Ignored;
        }
        // The press that opened the ring is let go: over a slot that is a flick; on the hub or outside it stays open for a tap.
        openedByPress_ = false;
        const int hit = optionBoxAt(pt) >= 0 ? RadialLayout::outside : hitAt(pt);   // letting go on an option box is not a flick onto the slot
        if (hit >= 0) {
            choose(hit);
        }
        return SyncReturn::Handled;
    }

    void RadialMenu::paint(BLContext& ctx) {
        if (!open_) {
            return;
        }
        const Theme& theme = Theme::current();
        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(12.0f);
        font.setBold(true);
        Font tiny = FontManager::getSystemFont(SystemUIFont::Message);
        tiny.setSize(10.0f);
        BLFont* blFont = font.blFont();
        BLFont* blSmall = tiny.blFont();
        const int count = int(ring_.slots.size());
        const double cx = double(center_.x), cy = double(center_.y);

        ctx.set_fill_style(BLRgba32(0x30000000u));
        ctx.fill_circle(BLCircle(cx, cy, RadialLayout::ringRadius + RadialLayout::slotHeight));

        for (int i = 0; i < count; ++i) {
            const RadialSlot& slot = ring_.slots[std::size_t(i)];
            const Point at = RadialLayout::slotCenter(i, count);
            const double w = RadialLayout::slotWidth, h = RadialLayout::slotHeight;
            const double x = cx + double(at.x) - w * 0.5, y = cy + double(at.y) - h * 0.5;
            const bool hot = i == hot_;
            ctx.set_fill_style(hot ? rgb(theme.accent) : rgb(theme.slot));
            ctx.fill_round_rect(BLRoundRect(x, y, w, h, h * 0.5));
            ctx.set_stroke_style(hot ? rgb(theme.accent) : rgb(theme.border));
            ctx.set_stroke_width(1.0);
            ctx.stroke_round_rect(BLRoundRect(x + 0.5, y + 0.5, w - 1.0, h - 1.0, h * 0.5));
            if (blFont != nullptr && blFont->is_valid()) {
                const float labelWidth = font.measureText(slot.label).width;
                const float keyWidth = slot.key.empty() || blSmall == nullptr ? 0.0f : tiny.measureText(slot.key).width + 6.0f;
                const double tx = x + (w - double(labelWidth + keyWidth)) * 0.5;
                ctx.set_fill_style(rgb(hot ? theme.onAccent : theme.textPrimary));
                ctx.fill_utf8_text(BLPoint(tx, y + h * 0.5 + 4.0), *blFont, slot.label.c_str(), slot.label.size());
                if (keyWidth > 0.0f) {
                    ctx.set_fill_style(hot ? rgb(theme.onAccent) : rgb(theme.textMuted));
                    ctx.fill_utf8_text(BLPoint(tx + double(labelWidth) + 6.0, y + h * 0.5 + 4.0), *blSmall, slot.key.c_str(), slot.key.size());
                }
            }
        }

        // The option box (a tiny dialog glyph, heavier top edge) at the outer end of a slot that has options; it is a tap target.
        for (int i = 0; i < count; ++i) {
            if (ring_.slots[std::size_t(i)].optionsCommand.empty()) {
                continue;
            }
            const Point box = RadialLayout::optionBoxCenter(i, count);
            const double bx = cx + double(box.x) - 8.0, by = cy + double(box.y) - 6.75;
            ctx.set_fill_style(rgb(theme.bgSurface));
            ctx.fill_round_rect(BLRoundRect(bx, by, 16.0, 13.5, 2.7));
            ctx.set_stroke_style(rgb(theme.highlight));
            ctx.set_stroke_width(1.35);
            ctx.stroke_round_rect(BLRoundRect(bx + 0.7, by + 0.7, 14.6, 12.1, 2.7));
            ctx.set_fill_style(rgb(theme.highlight));
            ctx.fill_rect(BLRect(bx, by, 16.0, 3.6));
        }

        ctx.set_fill_style(hot_ == RadialLayout::hub ? rgb(theme.slot) : rgb(theme.bgSurface));
        ctx.fill_circle(BLCircle(cx, cy, RadialLayout::hubRadius));
        ctx.set_stroke_style(rgb(theme.highlight));
        ctx.set_stroke_width(2.0);
        ctx.stroke_circle(BLCircle(cx, cy, RadialLayout::hubRadius - 1.0));
        if (blFont != nullptr && blFont->is_valid()) {
            const float titleWidth = font.measureText(ring_.title).width;
            ctx.set_fill_style(rgb(theme.textPrimary));
            ctx.fill_utf8_text(BLPoint(cx - double(titleWidth) * 0.5, cy - 6.0), *blFont, ring_.title.c_str(), ring_.title.size());
            ctx.set_fill_style(rgb(theme.highlight));
            ctx.fill_utf8_text(BLPoint(cx - 6.0, cy + 8.0), *blFont, "...", 3);
            if (blSmall != nullptr && blSmall->is_valid() && !ring_.caption.empty()) {
                const float captionWidth = tiny.measureText(ring_.caption).width;
                ctx.set_fill_style(rgb(theme.textMuted));
                ctx.fill_utf8_text(BLPoint(cx - double(captionWidth) * 0.5, cy + 22.0), *blSmall, ring_.caption.c_str(), ring_.caption.size());
            }
        }
    }

}
