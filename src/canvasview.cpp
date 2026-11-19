#include "curvy/canvasview.h"

#include "curvy/blendmodes.h"
#include "curvy/objectnode.h"
#include "curvy/shapepaths.h"

#include <newui/rootview.h>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace curvy {

    using namespace newui;

    CanvasView::CanvasView() {
        setName("CanvasView");
        setVisible(true);
        setAcceptsFocus(true);
        setWantsTabKey(true);   // newui keeps Tab for focus navigation otherwise; the canvas uses it for the graph and for stepping through shapes
        onMouseDown.add(this, &CanvasView::handleMouseDown);
        onMouseDblClick.add(this, &CanvasView::handleMouseDoubleClick);
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

    std::optional<ShapeHit> CanvasView::shapeAt(const Point& viewPoint, float tolerancePx) const {
        if (document_ == nullptr) {
            return std::nullopt;
        }
        const jitnodes::GeometryHandle artwork = document_->artwork();
        if (artwork == nullptr) {
            return std::nullopt;
        }
        const BLPoint at = toDocument(viewPoint);
        if (at.x < 0.0 || at.y < 0.0 || at.x > double(document_->pageWidth()) || at.y > double(document_->pageHeight())) {
            return std::nullopt;   // the artwork is clipped to the page, so what lies beyond it is not drawn and cannot be clicked
        }
        return curvy::shapeAt(*artwork, float(at.x), float(at.y), tolerancePx / zoom_);
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
        renderArtworkAtScale(ctx, *document_, double(client.left()) + double(panX_), double(client.top()) + double(panY_), double(zoom_), shapeDimmed);
    }

    void CanvasView::renderArtworkAtScale(BLContext& ctx, SketchDocument& document, double originX, double originY, double scale,
                                          const std::function<bool(const std::string&)>& dimmed) {
        const jitnodes::GeometryHandle artwork = document.artwork();
        if (artwork == nullptr || artwork->primitiveCount() == 0 || !artwork->hasPointAttrib("x")) {
            return;
        }
        const jitnodes::Geometry& geo = *artwork;
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

        // One path per shape, so the primitives of a compound shape (a ring and its counter) fill together and leave the hole open; the shape
        // takes the look of its first primitive.
        const std::vector<jitnodes::ShapeRange> shapes = geo.shapes();
        std::vector<BLPath> paths;
        paths.reserve(shapes.size());
        for (const jitnodes::ShapeRange& shape : shapes) {
            paths.push_back(shapePath(geo, shape, originX, originY, scale));
        }

        // The objects with an opacity below 1 or a blend other than Normal that each shape lies in, outermost first. The shape's key holds the
        // uids of the subnets around it, so the cooked geometry needs nothing stamped.
        struct Effect {
            std::uint32_t uid = 0;
            float opacity = 1.0f;
            int blend = 0;
        };
        std::map<std::uint32_t, std::optional<Effect>> effectOfUid;
        const auto effectFor = [&](std::uint32_t uid) -> const std::optional<Effect>& {
            auto found = effectOfUid.find(uid);
            if (found != effectOfUid.end()) {
                return found->second;
            }
            std::optional<Effect> effect;
            const NodeRef ref = document.nodeOfUid(uid);
            if (ref.valid() && document.hasNode(ref) && document.node(ref).typeName() == objectNodeType) {
                const jitnodes::Node& node = document.node(ref);
                Effect candidate;
                candidate.uid = uid;
                candidate.opacity = std::clamp(node.getFloat("opacity"), 0.0f, 1.0f);
                candidate.blend = int(node.getFloat("blend") + 0.5f);
                if (candidate.opacity < 1.0f || candidate.blend != 0) {
                    effect = candidate;
                }
            }
            return effectOfUid.emplace(uid, effect).first->second;
        };
        std::vector<std::vector<Effect>> effects(shapes.size());
        bool anyEffect = false;
        for (std::size_t s = 0; s < shapes.size(); ++s) {
            const std::vector<std::uint32_t> uids = SketchDocument::uidsOfShapeKey(std::string(shapes[s].key));
            for (std::size_t k = 0; k + 1 < uids.size(); ++k) {   // the last uid is the generator itself
                if (const std::optional<Effect>& effect = effectFor(uids[k])) {
                    effects[s].push_back(*effect);
                    anyEffect = true;
                }
            }
        }

        std::vector<char> faded;
        if (dimmed) {
            faded.reserve(shapes.size());
            for (const jitnodes::ShapeRange& shape : shapes) {
                faded.push_back(dimmed(std::string(shape.key)) ? 1 : 0);
            }
        }
        constexpr double fadedAlpha = 0.3;

        const auto paintShape = [&](BLContext& target, std::size_t s) {
            const std::size_t i = shapes[s].first;
            const BLPath& path = paths[s];
            if (path.is_empty()) {
                return;
            }
            if (!faded.empty() && faded[s] != 0) {
                target.set_global_alpha(fadedAlpha);
            }
            if (fillA != nullptr && fillA[i] > 0.0f) {
                target.set_fill_rule(evenOdd != nullptr && evenOdd[i] != 0.0f ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
                target.fill_path(path, packed(fillR[i], fillG[i], fillB[i], fillA[i]));
            }
            if (strokeA != nullptr && strokeA[i] > 0.0f && strokeW[i] > 0.0f) {
                target.set_stroke_width(double(strokeW[i]) * scale);
                target.stroke_path(path, packed(strokeR[i], strokeG[i], strokeB[i], strokeA[i]));
            }
            target.set_global_alpha(1.0);
        };

        // Paints shapes [first, last) at `depth` into `target`, whose pixel (0, 0) is (offsetX, offsetY) of the page-space the paths are in.
        // A run of consecutive shapes in one effect object is painted into a layer of its own (the run's bounds, inside `limit`) and
        // composited with the object's opacity and blend, so the object fades and blends as one.
        const std::function<void(BLContext&, std::size_t, std::size_t, std::size_t, const BLBox&)> paintRange =
            [&](BLContext& target, std::size_t first, std::size_t last, std::size_t depth, const BLBox& limit) {
                std::size_t s = first;
                while (s < last) {
                    if (effects[s].size() <= depth) {
                        paintShape(target, s);
                        ++s;
                        continue;
                    }
                    const Effect& effect = effects[s][depth];
                    std::size_t end = s + 1;
                    while (end < last && effects[end].size() > depth && effects[end][depth].uid == effect.uid) {
                        ++end;
                    }
                    BLBox box(1e30, 1e30, -1e30, -1e30);
                    for (std::size_t k = s; k < end; ++k) {
                        BLBox one;
                        if (!paths[k].is_empty() && paths[k].get_bounding_box(&one) == BL_SUCCESS) {
                            const double grow = strokeW != nullptr && strokeA != nullptr && strokeA[shapes[k].first] > 0.0f ? double(strokeW[shapes[k].first]) * scale : 0.0;
                            box.x0 = (std::min)(box.x0, one.x0 - grow);
                            box.y0 = (std::min)(box.y0, one.y0 - grow);
                            box.x1 = (std::max)(box.x1, one.x1 + grow);
                            box.y1 = (std::max)(box.y1, one.y1 + grow);
                        }
                    }
                    box.x0 = (std::max)(std::floor(box.x0), limit.x0);
                    box.y0 = (std::max)(std::floor(box.y0), limit.y0);
                    box.x1 = (std::min)(std::ceil(box.x1), limit.x1);
                    box.y1 = (std::min)(std::ceil(box.y1), limit.y1);
                    if (box.x1 > box.x0 && box.y1 > box.y0) {
                        BLImage layer(int(box.x1 - box.x0), int(box.y1 - box.y0), BL_FORMAT_PRGB32);
                        BLContext inner(layer);
                        inner.clear_all();
                        inner.translate(-box.x0, -box.y0);
                        paintRange(inner, s, end, depth + 1, box);
                        inner.end();
                        target.set_comp_op(BlendModes::compOp(effect.blend));
                        target.set_global_alpha(double(effect.opacity));
                        target.blit_image(BLPointI(int(box.x0), int(box.y0)), layer);
                        target.set_global_alpha(1.0);
                        target.set_comp_op(BL_COMP_OP_SRC_OVER);
                    }
                    s = end;
                }
            };

        ctx.save();
        ctx.clip_to_rect(BLRect(originX, originY, w, h));
        if (!anyEffect) {
            for (std::size_t s = 0; s < shapes.size(); ++s) {
                paintShape(ctx, s);
            }
        } else {
            // Layers are kept inside the page and the target's own pixels; the page is what the clip shows anyway.
            paintRange(ctx, 0, shapes.size(), 0, BLBox(originX, originY, originX + w, originY + h));
        }
        ctx.restore();
    }

    SyncReturn CanvasView::handleMouseDown(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        if (RootView* root = rootView()) {
            root->setFocusedSubView(this);
        }
        return onPressFilter && onPressFilter(pt, buttons, keys) ? SyncReturn::Handled : SyncReturn::Ignored;
    }

    SyncReturn CanvasView::handleMouseDoubleClick(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        return onDoubleClickFilter && onDoubleClickFilter(pt, buttons, keys) ? SyncReturn::Handled : SyncReturn::Ignored;
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
