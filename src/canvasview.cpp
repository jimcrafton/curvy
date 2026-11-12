#include "curvy/canvasview.h"

#include <newui/rootview.h>

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace curvy {

    using namespace newui;

    CanvasView::CanvasView() {
        setName("CanvasView");
        setVisible(true);
        setAcceptsFocus(true);
        onMouseDown.add(this, &CanvasView::handleMouseDown);
        onKeyDown.add(this, &CanvasView::handleKeyDown);
        onMouseWheel.add(this, &CanvasView::handleMouseWheel);
    }

    void CanvasView::setPasteboardColor(std::uint32_t argb) {
        pasteboardColor_ = argb;
        style().markDirty();
    }

    void CanvasView::setPan(float x, float y) {
        centered_ = false;
        panX_ = x;
        panY_ = y;
        style().markDirty();
    }

    void CanvasView::setZoom(float zoom) {
        zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
        style().markDirty();
        onZoomChanged(*this);
    }

    BLPoint CanvasView::toDocument(const Point& viewPoint) const {
        const Rect client = getClientBounds();
        return BLPoint((double(viewPoint.x) - client.left() - double(panX_)) / double(zoom_), (double(viewPoint.y) - client.top() - double(panY_)) / double(zoom_));
    }

    BLPoint CanvasView::toView(const BLPoint& documentPoint) const {
        const Rect client = getClientBounds();
        return BLPoint(client.left() + double(panX_) + documentPoint.x * double(zoom_), client.top() + double(panY_) + documentPoint.y * double(zoom_));
    }

    void CanvasView::zoomAt(const Point& anchor, float newZoom) {
        const BLPoint before = toDocument(anchor);
        zoom_ = std::clamp(newZoom, kMinZoom, kMaxZoom);
        const BLPoint after = toView(before);
        centered_ = false;
        panX_ += float(double(anchor.x) - after.x);
        panY_ += float(double(anchor.y) - after.y);
        style().markDirty();
        onZoomChanged(*this);
    }

    void CanvasView::zoomToFit(float largest) {
        centered_ = true;
        if (document_ == nullptr) {
            return;
        }
        constexpr float margin = 40.0f;
        const Rect client = getClientBounds();
        const float availW = client.width() - 2.0f * margin, availH = client.height() - 2.0f * margin;
        if (availW > 0.0f && availH > 0.0f) {
            const float fit = (std::min)(availW / document_->pageWidth(), availH / document_->pageHeight());
            setZoom((std::min)(fit, largest));
        }
        style().markDirty();
    }

    // The page is centred (and pinned near the top when it is taller than the view) until the first pan or zoom by hand.
    void CanvasView::centre(const Rect& client) {
        const float w = document_->pageWidth() * zoom_, h = document_->pageHeight() * zoom_;
        panX_ = (client.width() - w) * 0.5f;
        panY_ = h < client.height() - 40.0f ? (client.height() - h) * 0.5f : 20.0f;
    }

    SyncReturn CanvasView::handleMouseWheel(View&, const Point& pt, float delta) {
        const float notches = delta / 120.0f;   // WM_MOUSEWHEEL: 120 per notch, positive away from the user
        if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            zoomAt(pt, zoom_ * std::pow(1.15f, notches));
        } else if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            panBy(notches * 40.0f, 0.0f);
        } else {
            panBy(0.0f, notches * 40.0f);
        }
        return SyncReturn::Handled;
    }

    void CanvasView::setDocument(SketchDocument* document) {
        document_ = document;
        style().markDirty();
    }

    void CanvasView::paintPage(BLContext& ctx, const Rect& client) const {
        const double w = double(document_->pageWidth()) * double(zoom_), h = double(document_->pageHeight()) * double(zoom_);
        const double x = double(client.left()) + double(panX_);
        const double y = double(client.top()) + double(panY_);

        ctx.set_fill_style(BLRgba32(0x40000000u));
        ctx.fill_rect(BLRect(x + 3.0, y + 4.0, w, h));

        if (document_->pageTransparent()) {
            constexpr double cell = 8.0;
            ctx.save();
            ctx.clip_to_rect(BLRect(x, y, w, h));
            ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
            ctx.fill_rect(BLRect(x, y, w, h));
            ctx.set_fill_style(BLRgba32(0xFFD0D0D0u));
            for (double row = 0.0; row * cell < h; row += 1.0) {
                for (double col = std::fmod(row, 2.0); col * cell < w; col += 2.0) {
                    ctx.fill_rect(BLRect(x + col * cell, y + row * cell, cell, cell));
                }
            }
            ctx.restore();
        } else {
            ctx.set_fill_style(document_->pageColor().toBLRgba32());
            ctx.fill_rect(BLRect(x, y, w, h));
        }
    }

    void CanvasView::renderArtwork(BLContext& ctx, const Rect& client) {
        renderArtworkAtScale(ctx, *document_, double(client.left()) + double(panX_), double(client.top()) + double(panY_), double(zoom_));
    }

    void CanvasView::renderArtworkAtScale(BLContext& ctx, SketchDocument& document, double originX, double originY, double scale) {
        const jitnodes::GeometryHandle artwork = document.artwork();
        if (artwork == nullptr || artwork->primitiveCount() == 0 || !artwork->hasPointAttrib("x")) {
            return;
        }
        const jitnodes::Geometry& geo = *artwork;
        const float* px = geo.x();
        const float* py = geo.y();
        const auto column = [&](const char* name) -> const float* {
            const jitnodes::AttribArray* a = geo.findAttrib(jitnodes::AttribClass::primitive, name);
            return a != nullptr ? a->floats().data() : nullptr;
        };
        const float* fillR = column("fill.r");
        const float* fillG = column("fill.g");
        const float* fillB = column("fill.b");
        const float* fillA = column("fill.a");
        const float* strokeR = column("stroke.r");
        const float* strokeG = column("stroke.g");
        const float* strokeB = column("stroke.b");
        const float* strokeA = column("stroke.a");
        const float* strokeW = column("stroke.width");
        const float* evenOdd = column("fill.evenOdd");

        const double w = double(document.pageWidth()) * scale, h = double(document.pageHeight()) * scale;
        const auto packed = [](float r, float g, float b, float a) {
            const auto byte = [](float v) { return std::uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return BLRgba32((byte(a) << 24) | (byte(r) << 16) | (byte(g) << 8) | byte(b));
        };

        ctx.save();
        ctx.clip_to_rect(BLRect(originX, originY, w, h));
        const auto& prims = geo.primitives();
        for (std::size_t i = 0; i < prims.size(); ++i) {
            const jitnodes::Primitive& prim = prims[i];
            if (prim.count < 2) {
                continue;
            }
            const auto at = [&](std::uint32_t k) { return BLPoint(originX + double(px[prim.start + k]) * scale, originY + double(py[prim.start + k]) * scale); };
            BLPath path;
            path.move_to(at(0));
            if (prim.kind == jitnodes::PrimKind::cubic) {
                const std::uint32_t segments = jitnodes::cubicSegments(prim);
                for (std::uint32_t s = 0; s < segments; ++s) {
                    const std::uint32_t k = s * 3;
                    path.cubic_to(at(k + 1), at(k + 2), at((k + 3) % prim.count));
                }
            } else {
                for (std::uint32_t k = 1; k < prim.count; ++k) {
                    path.line_to(at(k));
                }
            }
            if (prim.closed) {
                path.close();
            }
            if (fillA != nullptr && fillA[i] > 0.0f) {
                ctx.set_fill_rule(evenOdd != nullptr && evenOdd[i] != 0.0f ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
                ctx.fill_path(path, packed(fillR[i], fillG[i], fillB[i], fillA[i]));
            }
            if (strokeA != nullptr && strokeA[i] > 0.0f && strokeW[i] > 0.0f) {
                ctx.set_stroke_width(double(strokeW[i]) * scale);
                ctx.stroke_path(path, packed(strokeR[i], strokeG[i], strokeB[i], strokeA[i]));
            }
        }
        ctx.restore();
    }

    SyncReturn CanvasView::handleMouseDown(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        if (RootView* root = rootView()) {
            root->setFocusedSubView(this);
        }
        return onPressFilter && onPressFilter(pt, buttons, keys) ? SyncReturn::Handled : SyncReturn::Ignored;
    }

    SyncReturn CanvasView::handleKeyDown(View&, std::uint32_t keyMask, int, int, std::uint32_t vkCode) {
        return onKeyFilter && onKeyFilter(keyMask, vkCode) ? SyncReturn::Handled : SyncReturn::Ignored;
    }

    void CanvasView::paint(BLContext& ctx) {
        const Rect client = getClientBounds();
        ctx.set_fill_style(BLRgba32(pasteboardColor_));
        ctx.fill_rect(BLRect(client.left(), client.top(), client.width(), client.height()));
        if (document_ != nullptr) {
            if (centered_) {
                centre(client);
            }
            paintPage(ctx, client);
            renderArtwork(ctx, client);
        }
        if (onPaintTop) {
            onPaintTop(ctx);
        }
    }

}
