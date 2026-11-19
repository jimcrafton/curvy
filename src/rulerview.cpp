#include "curvy/rulerview.h"

#include "curvy/canvasview.h"
#include "curvy/theme.h"
#include "curvy/units.h"

#include <newui/font.h>
#include <newui/fontmanager.h>

#include <cmath>
#include <string>
#include <vector>

namespace curvy {

    using namespace newui;

    RulerView::RulerView(CanvasView& canvas, bool horizontal) : canvas_(canvas), horizontal_(horizontal) {
        setName(horizontal ? "TopRuler" : "LeftRuler");
        setVisible(true);
        onMouseDown += [this](View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
            return canvas_.onPressFilter && canvas_.onPressFilter(toCanvas(pt), buttons, keys) ? SyncReturn::Handled : SyncReturn::Ignored;
        };
        onMouseMove += [this](View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
            const Point at = toCanvas(pt);
            canvas_.setPointer(at);
            canvas_.onMouseMove(canvas_, at, buttons, keys);
            return SyncReturn::Handled;
        };
        onMouseUp += [this](View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
            canvas_.onMouseUp(canvas_, toCanvas(pt), buttons, keys);
            return SyncReturn::Handled;
        };
    }

    Point RulerView::toCanvas(const Point& local) const {
        return Point(local.x + bounds().left(), local.y + bounds().top());
    }

    void RulerView::setMarker(std::optional<float> along) {
        if (along != marker_) {
            marker_ = along;
            redraw();
        }
    }

    void RulerView::paint(BLContext& ctx) {
        const SketchDocument* document = canvas_.document();
        if (document == nullptr) {
            return;
        }
        const Theme& theme = Theme::current();
        const double width = double(bounds().width()), height = double(bounds().height());
        const double thickness = double(Rulers::kThickness);
        const BLRgba32 edge(Theme::argb(theme.border)), ink(Theme::argb(theme.textMuted));

        ctx.set_fill_style(BLRgba32(Theme::argb(theme.bgSurface)));
        ctx.fill_rect(BLRect(0.0, 0.0, width, height));
        ctx.set_stroke_width(1.0);
        ctx.set_stroke_style(edge);
        if (horizontal_) {
            ctx.stroke_line(BLPoint(0.0, height - 0.5), BLPoint(width, height - 0.5));
        } else {
            ctx.stroke_line(BLPoint(width - 0.5, 0.0), BLPoint(width - 0.5, height));
        }

        const double zoom = double(canvas_.zoom());
        const BLPoint page = canvas_.toView(BLPoint(0.0, 0.0));
        const double offset = horizontal_ ? page.x - double(bounds().left()) : page.y - double(bounds().top());
        const double extent = horizontal_ ? width : height;
        const std::vector<Rulers::Tick> ticks = Rulers::ticks(-offset / zoom, (extent - offset) / zoom, zoom, document->units().length, double(document->ppi()), 5.0,
                                                                    horizontal_ ? document->rulerOrigin().x : document->rulerOrigin().y);

        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(9.0f);
        const BLFont* blFont = font.blFont();
        const bool canWrite = blFont != nullptr && blFont->is_valid();

        ctx.set_stroke_style(ink);
        for (const Rulers::Tick& tick : ticks) {
            const double at = std::floor(offset + tick.points * zoom) + 0.5;
            const double length = tick.major ? thickness - 2.0 : 5.0;
            if (horizontal_) {
                ctx.stroke_line(BLPoint(at, thickness - length), BLPoint(at, thickness - 1.0));
            } else {
                ctx.stroke_line(BLPoint(thickness - length, at), BLPoint(thickness - 1.0, at));
            }
            if (tick.major && canWrite) {
                const std::string label = Units::number(tick.value, 4);
                ctx.set_fill_style(ink);
                if (horizontal_) {
                    ctx.fill_utf8_text(BLPoint(at + 3.0, 10.0), *blFont, label.c_str(), label.size());
                } else {
                    ctx.save();
                    ctx.translate(10.0, at + 3.0);
                    ctx.rotate(1.5707963267948966);
                    ctx.fill_utf8_text(BLPoint(0.0, 0.0), *blFont, label.c_str(), label.size());
                    ctx.restore();
                }
            }
        }

        if (marker_) {
            const double along = std::floor(double(*marker_) - (horizontal_ ? double(bounds().left()) : double(bounds().top()))) + 0.5;
            ctx.set_stroke_style(BLRgba32(Theme::argb(theme.accent)));
            if (horizontal_) {
                ctx.stroke_line(BLPoint(along, 0.0), BLPoint(along, height));
            } else {
                ctx.stroke_line(BLPoint(0.0, along), BLPoint(width, along));
            }
        }
    }

}
