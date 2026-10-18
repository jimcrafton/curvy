#include "curvy/canvasview.h"

#include <newui/keyboard_constants.h>

#include <algorithm>
#include <cmath>

// The Artboard tool: drag on empty canvas to make an artboard, click one to make it the active one, drag it to move it, drag its handles to
// resize it. Nothing changes in the document until the pointer is let go (one undo step); the canvas shows the artboard being dragged.

namespace curvy {

    using namespace newui;

    namespace {
        // The eight handles of a box, clockwise from the top left: corners at even indexes, edge middles at odd ones.
        BLPoint handlePoint(const Artboard& b, int handle) {
            const double cx = b.x + b.width * 0.5, cy = b.y + b.height * 0.5, r = b.x + b.width, d = b.y + b.height;
            switch (handle) {
                case 0: return BLPoint(b.x, b.y);
                case 1: return BLPoint(cx, b.y);
                case 2: return BLPoint(r, b.y);
                case 3: return BLPoint(r, cy);
                case 4: return BLPoint(r, d);
                case 5: return BLPoint(cx, d);
                case 6: return BLPoint(b.x, d);
                default: return BLPoint(b.x, cy);
            }
        }
    }

    int CanvasView::artboardHandleAt(const BLPoint& point) const {
        if (document_ == nullptr) {
            return -1;
        }
        const Artboard& board = document_->activeArtboardRect();
        const double reach = 6.0 / double(zoom_);
        for (int handle = 0; handle < 8; ++handle) {
            BLPoint p = handlePoint(board, handle);
            if (std::fabs(p.x - point.x) <= reach && std::fabs(p.y - point.y) <= reach) {
                return handle;
            }
        }
        return -1;
    }

    // A press with the Artboard tool.
    void CanvasView::artboardPress(const BLPoint& point) {
        artboardIndex_ = document_->activeArtboard();
        artboardStart_ = document_->activeArtboardRect();
        artboardLive_ = artboardStart_;
        int handle = artboardHandleAt(point);
        if (handle >= 0) {
            dragHandle_ = handle;
            drag_ = Drag::ArtboardResize;
            return;
        }
        int hit = document_->artboardAt(point);
        if (hit >= 0) {
            artboardIndex_ = std::size_t(hit);
            document_->setActiveArtboard(artboardIndex_);
            artboardStart_ = document_->artboards()[artboardIndex_];
            artboardLive_ = artboardStart_;
            drag_ = Drag::ArtboardMove;
            return;
        }
        drag_ = Drag::ArtboardNew;
        artboardLive_ = Artboard{std::string(), point.x, point.y, 0.0, 0.0};
    }

    void CanvasView::artboardMove(const BLPoint& point, std::uint32_t keys) {
        const double dx = point.x - dragStartDoc_.x, dy = point.y - dragStartDoc_.y;
        switch (drag_) {
            case Drag::ArtboardNew: {
                BLPoint end = snapPoint(point);
                BLPoint start = dragStartDoc_;
                double w = end.x - start.x, h = end.y - start.y;
                if ((keys & kmShift) != 0) {   // a square
                    double side = std::max(std::fabs(w), std::fabs(h));
                    w = w < 0.0 ? -side : side;
                    h = h < 0.0 ? -side : side;
                }
                artboardLive_.x = std::min(start.x, start.x + w);
                artboardLive_.y = std::min(start.y, start.y + h);
                artboardLive_.width = std::fabs(w);
                artboardLive_.height = std::fabs(h);
                break;
            }
            case Drag::ArtboardMove:
                artboardLive_.x = std::round(artboardStart_.x + dx);
                artboardLive_.y = std::round(artboardStart_.y + dy);
                break;
            default: {   // ArtboardResize
                const int h = dragHandle_;
                const bool left = h == 0 || h == 6 || h == 7, right = h == 2 || h == 3 || h == 4;
                const bool top = h == 0 || h == 1 || h == 2, bottom = h == 4 || h == 5 || h == 6;
                double x0 = artboardStart_.x, y0 = artboardStart_.y, x1 = x0 + artboardStart_.width, y1 = y0 + artboardStart_.height;
                if (left) { x0 += dx; }
                if (right) { x1 += dx; }
                if (top) { y0 += dy; }
                if (bottom) { y1 += dy; }
                artboardLive_.x = std::round(std::min(x0, x1));
                artboardLive_.y = std::round(std::min(y0, y1));
                artboardLive_.width = std::max(1.0, std::round(std::fabs(x1 - x0)));
                artboardLive_.height = std::max(1.0, std::round(std::fabs(y1 - y0)));
                break;
            }
        }
        dragMoved_ = true;
        style().markDirty();
    }

    void CanvasView::finishArtboard(Drag kind) {
        if (kind == Drag::ArtboardNew) {
            if (artboardLive_.width >= 4.0 && artboardLive_.height >= 4.0) {
                artboardLive_.name.clear();
                document_->addArtboard(artboardLive_);
            }
        } else if (dragMoved_) {
            Artboard moved = artboardStart_;
            moved.x = artboardLive_.x;
            moved.y = artboardLive_.y;
            moved.width = artboardLive_.width;
            moved.height = artboardLive_.height;
            document_->setArtboard(artboardIndex_, moved);
        }
        style().markDirty();
    }

    // The artboard being dragged and, while the tool is on, the handles of the active one.
    void CanvasView::paintArtboardTool(BLContext& ctx) const {
        if (document_ == nullptr) {
            return;
        }
        const bool dragging = drag_ == Drag::ArtboardNew || drag_ == Drag::ArtboardMove || drag_ == Drag::ArtboardResize;
        const Artboard& board = dragging ? artboardLive_ : document_->activeArtboardRect();
        BLPoint a = toView(BLPoint(board.x, board.y)), b = toView(BLPoint(board.x + board.width, board.y + board.height));
        ctx.save();
        ctx.set_stroke_width(1.0);
        ctx.set_stroke_style(BLRgba32(0xFF1E6FE0u));
        if (dragging) {
            ctx.set_fill_style(BLRgba32(0x181E6FE0u));
            ctx.fill_rect(BLRect(a.x, a.y, b.x - a.x, b.y - a.y));
        }
        ctx.stroke_rect(BLRect(std::floor(a.x) + 0.5, std::floor(a.y) + 0.5, std::floor(b.x - a.x), std::floor(b.y - a.y)));
        if (drag_ != Drag::ArtboardNew) {
            for (int handle = 0; handle < 8; ++handle) {
                BLPoint p = toView(handlePoint(board, handle));
                ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
                ctx.fill_rect(BLRect(std::floor(p.x) - 3.0, std::floor(p.y) - 3.0, 7.0, 7.0));
                ctx.stroke_rect(BLRect(std::floor(p.x) - 2.5, std::floor(p.y) - 2.5, 6.0, 6.0));
            }
        }
        ctx.restore();
    }

}
