#include "curvy/colorwheel.h"

#include <newui/mouse_constants.h>

#include <algorithm>
#include <cmath>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kRingWidth = 16.0;
        constexpr double kGap = 6.0;   // between the ring and the square

        BLRgba32 fromHsb(double h, double s, double b) {
            h = h - std::floor(h);
            const double sector = h * 6.0;
            const int i = int(std::floor(sector));
            const double f = sector - double(i);
            const double p = b * (1.0 - s), q = b * (1.0 - s * f), t = b * (1.0 - s * (1.0 - f));
            double r = b, g = t, bl = p;
            switch (i % 6) {
                case 0: r = b; g = t; bl = p; break;
                case 1: r = q; g = b; bl = p; break;
                case 2: r = p; g = b; bl = t; break;
                case 3: r = p; g = q; bl = b; break;
                case 4: r = t; g = p; bl = b; break;
                default: r = b; g = p; bl = q; break;
            }
            return BLRgba32(std::uint32_t(r * 255.0 + 0.5), std::uint32_t(g * 255.0 + 0.5), std::uint32_t(bl * 255.0 + 0.5));
        }
    }

    ColorWheel::ColorWheel() {
        setVisible(true);
        onMouseDown.add(this, &ColorWheel::handleMouseDown);
        onMouseMove.add(this, &ColorWheel::handleMouseMove);
        onMouseUp.add(this, &ColorWheel::handleMouseUp);
    }

    void ColorWheel::setHsb(double hue, double saturation, double brightness, bool hueDefined) {
        if (hueDefined) {
            hue_ = hue - std::floor(hue);
        }
        saturation_ = std::clamp(saturation, 0.0, 1.0);
        brightness_ = std::clamp(brightness, 0.0, 1.0);
        style().markDirty();
    }

    bool ColorWheel::geometry(Geometry& out) const {
        const Rect client = getClientBounds();
        const double extent = std::min(double(client.width()), double(client.height()));
        if (extent < 2.0 * (kRingWidth + kGap + 8.0)) {
            return false;
        }
        out.cx = double(client.left()) + double(client.width()) * 0.5;
        out.cy = double(client.top()) + double(client.height()) * 0.5;
        out.outer = extent * 0.5 - 2.0;
        out.inner = out.outer - kRingWidth;
        // The biggest square that fits inside the ring's inner edge, less the gap.
        out.side = (out.inner - kGap) * std::sqrt(2.0);
        out.left = out.cx - out.side * 0.5;
        out.top = out.cy - out.side * 0.5;
        return out.side > 4.0;
    }

    void ColorWheel::updateFrom(const Point& pt) {
        Geometry g;
        if (!geometry(g)) {
            return;
        }
        if (drag_ == Drag::Hue) {
            double turns = std::atan2(double(pt.y) - g.cy, double(pt.x) - g.cx) / (2.0 * kPi);
            hue_ = turns - std::floor(turns);
        } else if (drag_ == Drag::Square) {
            saturation_ = std::clamp((double(pt.x) - g.left) / g.side, 0.0, 1.0);
            brightness_ = 1.0 - std::clamp((double(pt.y) - g.top) / g.side, 0.0, 1.0);
        }
        style().markDirty();
        onChanged(*this);
    }

    SyncReturn ColorWheel::handleMouseDown(View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
        Geometry g;
        if ((buttons & mbmLeftButton) == 0 || !geometry(g)) {
            return SyncReturn::Ignored;
        }
        const double distance = std::hypot(double(pt.x) - g.cx, double(pt.y) - g.cy);
        const bool inSquare = double(pt.x) >= g.left - 3.0 && double(pt.x) <= g.left + g.side + 3.0 && double(pt.y) >= g.top - 3.0 && double(pt.y) <= g.top + g.side + 3.0;
        if (distance >= g.inner - 2.0 && distance <= g.outer + 2.0) {
            drag_ = Drag::Hue;
        } else if (inSquare) {
            drag_ = Drag::Square;
        } else {
            return SyncReturn::Ignored;
        }
        onDragStarted(*this);
        updateFrom(pt);
        return SyncReturn::Handled;
    }

    SyncReturn ColorWheel::handleMouseMove(View&, const Point& pt, std::uint32_t, std::uint32_t) {
        if (drag_ == Drag::None) {
            return SyncReturn::Ignored;
        }
        updateFrom(pt);
        return SyncReturn::Handled;
    }

    SyncReturn ColorWheel::handleMouseUp(View&, const Point&, std::uint32_t, std::uint32_t) {
        if (drag_ == Drag::None) {
            return SyncReturn::Ignored;
        }
        drag_ = Drag::None;
        onDragFinished(*this);
        return SyncReturn::Handled;
    }

    void ColorWheel::paint(BLContext& ctx) {
        Geometry g;
        if (!geometry(g)) {
            return;
        }
        ctx.save();

        // The ring: a conic gradient through the hues, stroked as a circle of the ring's width.
        BLGradient ring(BLConicGradientValues(g.cx, g.cy, 0.0));
        for (int i = 0; i <= 6; ++i) {
            ring.add_stop(double(i) / 6.0, fromHsb(double(i) / 6.0, 1.0, 1.0));
        }
        ctx.set_stroke_style(ring);
        ctx.set_stroke_width(kRingWidth);
        ctx.stroke_circle(BLCircle(g.cx, g.cy, g.outer - kRingWidth * 0.5));

        // The square: the hue fading to white across, and to black down.
        const BLRgba32 pure = fromHsb(hue_, 1.0, 1.0);
        BLGradient across(BLLinearGradientValues(g.left, 0.0, g.left + g.side, 0.0));
        across.add_stop(0.0, BLRgba32(0xFFFFFFFFu));
        across.add_stop(1.0, pure);
        ctx.set_fill_style(across);
        ctx.fill_rect(BLRect(g.left, g.top, g.side, g.side));
        BLGradient down(BLLinearGradientValues(0.0, g.top, 0.0, g.top + g.side));
        down.add_stop(0.0, BLRgba32(0x00000000u));
        down.add_stop(1.0, BLRgba32(0xFF000000u));
        ctx.set_fill_style(down);
        ctx.fill_rect(BLRect(g.left, g.top, g.side, g.side));

        // The two markers: a ring around the current hue, and a ring on the colour in the square (dark outside, light inside so it shows on either).
        const double angle = hue_ * 2.0 * kPi;
        const double mr = g.outer - kRingWidth * 0.5;
        auto marker = [&](double x, double y, BLRgba32 fill) {
            ctx.set_fill_style(fill);
            ctx.fill_circle(BLCircle(x, y, 5.0));
            ctx.set_stroke_width(2.0);
            ctx.set_stroke_style(BLRgba32(0xFFFFFFFFu));
            ctx.stroke_circle(BLCircle(x, y, 5.0));
            ctx.set_stroke_width(1.0);
            ctx.set_stroke_style(BLRgba32(0xFF000000u));
            ctx.stroke_circle(BLCircle(x, y, 6.5));
        };
        marker(g.cx + std::cos(angle) * mr, g.cy + std::sin(angle) * mr, pure);
        marker(g.left + saturation_ * g.side, g.top + (1.0 - brightness_) * g.side, fromHsb(hue_, saturation_, brightness_));
        ctx.restore();
    }

}
