#include "curvy/graphoverviews.h"

#include "curvy/canvasview.h"
#include "curvy/graphview.h"
#include "curvy/theme.h"

#include <newui/mouse_constants.h>

#include <algorithm>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr double kMargin = 8.0;
    }

    // Layered rounded rectangles, each a little larger and fainter, offset downward: a soft shadow without blurring.
    void GraphOverlayChrome::paintShadow(BLContext& ctx, const BLRect& box) {
        constexpr int layers = 7;
        for (int i = layers; i >= 1; --i) {
            const double grow = double(i) * 1.6;
            ctx.set_fill_style(BLRgba32(Theme::argb(0x000000, std::uint32_t(8 + (layers - i) * 3))));
            ctx.fill_round_rect(BLRoundRect(box.x - grow, box.y + 3.0 - grow * 0.4, box.w + 2.0 * grow, box.h + 2.0 * grow, 4.0 + grow));
        }
    }

    void GraphOverlayChrome::paintOutline(BLContext& ctx, const Rect& client) {
        const Theme& theme = Theme::current();
        const double x = double(client.left()), y = double(client.top()), w = double(client.width()), h = double(client.height());
        ctx.set_stroke_style(BLRgba32(Theme::argb(theme.textMuted, 150)));
        ctx.set_stroke_width(1.0);
        ctx.stroke_rect(BLRect(x + 0.5, y + 0.5, w - 1.0, h - 1.0));
        ctx.set_fill_style(BLRgba32(Theme::argb(0xFFFFFF, 34)));
        ctx.fill_rect(BLRect(x + 1.0, y + 1.0, w - 2.0, 1.0));
    }

    GraphPreview::GraphPreview() {
        setName("GraphPreview");
        setVisible(true);
    }

    GraphView* GraphPreview::graph() const {
        return dynamic_cast<GraphView*>(parent());
    }

    void GraphPreview::paint(BLContext& ctx) {
        GraphView* graph = this->graph();
        SketchDocument* document = graph != nullptr ? graph->document() : nullptr;
        const Rect client = getClientBounds();
        if (document != nullptr && document->pageWidth() > 0.0f && document->pageHeight() > 0.0f) {
            const double pageW = double(document->pageWidth()), pageH = double(document->pageHeight());
            const double scale = (std::min)((double(client.width()) - 2.0 * kMargin) / pageW, (double(client.height()) - 2.0 * kMargin) / pageH);
            if (scale > 0.0) {
                const double x = double(client.left()) + (double(client.width()) - pageW * scale) * 0.5;
                const double y = double(client.top()) + (double(client.height()) - pageH * scale) * 0.5;
                ctx.set_fill_style(BLRgba32(0x55000000u));   // the page lifts off the box a little
                ctx.fill_rect(BLRect(x + 2.0, y + 3.0, pageW * scale, pageH * scale));
                ctx.set_fill_style(document->pageTransparent() ? BLRgba32(0xFFFFFFFFu) : document->pageColor().toBLRgba32());
                ctx.fill_rect(BLRect(x, y, pageW * scale, pageH * scale));
                CanvasView::renderArtworkAtScale(ctx, *document, x, y, scale);
            }
        }
        GraphOverlayChrome::paintOutline(ctx, client);
    }

    GraphMiniMap::GraphMiniMap() {
        setName("GraphMiniMap");
        setVisible(true);
        onMouseDown.add(this, &GraphMiniMap::handleMouseDown);
        onMouseMove.add(this, &GraphMiniMap::handleMouseMove);
    }

    GraphView* GraphMiniMap::graph() const {
        return dynamic_cast<GraphView*>(parent());
    }

    // The map shows the nodes plus half a screenful of room on every side, so the outline of what is in view always fits on it however far the
    // view is panned among the nodes, and zooming in makes the outline smaller and the nodes larger. It depends on the zoom and the nodes, not on
    // where the view is panned, so it does not rescale while you drag.
    GraphMiniMap::Frame GraphMiniMap::frame() const {
        Frame result;
        const GraphView* view = graph();
        if (view == nullptr) {
            return result;
        }
        const Rect client = getClientBounds();
        const std::vector<BLRect> rects = view->nodeRects();
        if (rects.empty()) {
            return result;
        }
        double left = 1e9, top = 1e9, right = -1e9, bottom = -1e9;
        for (const BLRect& r : rects) {
            left = (std::min)(left, r.x);
            top = (std::min)(top, r.y);
            right = (std::max)(right, r.x + r.w);
            bottom = (std::max)(bottom, r.y + r.h);
        }
        result.left = left;
        result.top = top;
        result.right = right;
        result.bottom = bottom;
        const BLRect seen = view->visibleRect();
        const double wideLeft = left - seen.w * 0.5, wideRight = right + seen.w * 0.5, wideTop = top - seen.h * 0.5, wideBottom = bottom + seen.h * 0.5;
        const double availW = double(client.width()) - 2.0 * kMargin, availH = double(client.height()) - 2.0 * kMargin;
        result.scale = (std::min)(availW / (wideRight - wideLeft), availH / (wideBottom - wideTop));
        result.offsetX = double(client.left()) + kMargin + (availW - (wideRight - wideLeft) * result.scale) * 0.5 - wideLeft * result.scale;
        result.offsetY = double(client.top()) + kMargin + (availH - (wideBottom - wideTop) * result.scale) * 0.5 - wideTop * result.scale;
        return result;
    }

    void GraphMiniMap::paint(BLContext& ctx) {
        const GraphView* view = graph();
        if (view == nullptr || view->document() == nullptr) {
            return;
        }
        const Theme& theme = Theme::current();
        const Rect client = getClientBounds();
        const Frame f = frame();
        ctx.save();
        ctx.clip_to_rect(BLRect(client.left(), client.top(), client.width(), client.height()));
        ctx.set_fill_style(BLRgba32(Theme::argb(theme.textMuted, 200)));
        for (const BLRect& r : view->nodeRects()) {
            ctx.fill_rect(BLRect(f.offsetX + r.x * f.scale, f.offsetY + r.y * f.scale, (std::max)(2.0, r.w * f.scale), (std::max)(2.0, r.h * f.scale)));
        }
        const BLRect seen = view->visibleRect();
        ctx.set_stroke_style(BLRgba32(Theme::argb(theme.accent)));
        ctx.set_stroke_width(1.0);
        ctx.stroke_rect(BLRect(f.offsetX + seen.x * f.scale, f.offsetY + seen.y * f.scale, seen.w * f.scale, seen.h * f.scale));
        ctx.restore();
        GraphOverlayChrome::paintOutline(ctx, client);
    }

    void GraphMiniMap::recentre(const Point& pt) {
        GraphView* view = graph();
        if (view == nullptr) {
            return;
        }
        const Frame f = frame();
        const double x = std::clamp((double(pt.x) - f.offsetX) / f.scale, f.left, f.right);
        const double y = std::clamp((double(pt.y) - f.offsetY) / f.scale, f.top, f.bottom);
        view->centerOn(x, y);
        style().markDirty();
    }

    SyncReturn GraphMiniMap::handleMouseDown(View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
        if ((buttons & mbmLeftButton) != 0) {
            recentre(pt);
        }
        return SyncReturn::Handled;
    }

    SyncReturn GraphMiniMap::handleMouseMove(View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
        if ((buttons & mbmLeftButton) != 0) {
            recentre(pt);
        }
        return SyncReturn::Handled;
    }

}
