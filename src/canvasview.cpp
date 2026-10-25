#include "curvy/canvasview.h"
#include "curvy/theme.h"

#include <newui/bundle.h>

#include <newui/newui.h>
#include <newui/color.h>
#include <newui/mouse_constants.h>
#include <newui/fontmanager.h>
#include <newui/uicolormanager.h>

#include <newui/keyboard_constants.h>
#include <newui/rootview.h>
#include <newui/clipboardmgr.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace curvy {

    using namespace newui;

    CanvasView::CanvasView() {
        setName("CanvasView");
        setVisible(true);
        setAcceptsFocus(true);
        onMouseDown.add(this, &CanvasView::handleMouseDown);
        onMouseMove.add(this, &CanvasView::handleMouseMove);
        onMouseUp.add(this, &CanvasView::handleMouseUp);
        onMouseWheel.add(this, &CanvasView::handleMouseWheel);
        onKeyDown.add(this, &CanvasView::handleKeyDown);
        onKeyPress.add(this, &CanvasView::handleKeyPress);
        onMouseDblClick.add(this, &CanvasView::handleMouseDblClick);
        onMouseLeft += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
            if (hasHover_) {
                hasHover_ = false;
                style().markDirty();
            }
            return SyncReturn::Ignored;
        };
    }

    void CanvasView::updateHover() {
        bool has = false;
        std::size_t id = 0;
        ScrubPart part = ScrubPart::None;
        if (document_ != nullptr && tool_ == SketchTool::Select && drag_ == Drag::None && !panning_) {
            const SketchObject* hit = document_->hitTest(pointerDoc_, 4.0 / double(zoom_));
            if (hit != nullptr && !document_->isSelected(hit->id())) {
                part = HoldScrub::partAt(*hit, pointerDoc_, 4.0 / double(zoom_));
                has = part != ScrubPart::None;
                id = hit->id();
            }
        }
        if (has != hasHover_ || (has && (id != hoverId_ || part != hoverPart_))) {
            hasHover_ = has;
            hoverId_ = id;
            hoverPart_ = part;
            style().markDirty();
        }
    }

    void CanvasView::paintHover(BLContext& ctx) const {
        const SketchObject* object = hasHover_ && document_ != nullptr ? document_->findById(hoverId_) : nullptr;
        if (object == nullptr || object->geometry().size() == 0) {
            return;
        }
        const BLPoint origin = toView(BLPoint(0.0, 0.0));
        BLPath path = object->geometry();
        path.transform(BLMatrix2D(double(zoom_), 0.0, 0.0, double(zoom_), origin.x, origin.y));
        const std::uint32_t amber = Theme::current().accent & 0xFFFFFFu;   // the theme's amber, see-through so the shape stays visible under it
        ctx.save();
        if (hoverPart_ == ScrubPart::Fill) {
            ctx.set_fill_style(BLRgba32(0x55000000u | amber));
            ctx.fill_path(path);
        } else {
            ctx.set_stroke_width(std::max(3.0, object->strokeWidth() * double(zoom_)));
            ctx.set_stroke_style(BLRgba32(0xE6000000u | amber));
            ctx.stroke_path(path);
        }
        ctx.restore();
    }

    void CanvasView::setDocument(SketchDocument* document) {
        if (document_ != nullptr) {
            document_->removeView(this);
        }
        textEditing_ = false;   // text being edited belongs to the old document
        textDraft_.reset();
        document_ = document;
        if (document_ != nullptr) {
            document_->addView(this);
            pageWidth_ = document_->pageWidth();
            pageHeight_ = document_->pageHeight();
        }
        style().markDirty();
    }

    void CanvasView::setShowPages(bool value) {
        showPages_ = value;
        style().markDirty();
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

    BLPoint CanvasView::toDocument(const Point& viewPoint) const {
        Rect client = getClientBounds();
        double rs = rulerSize();
        return BLPoint((double(viewPoint.x) - client.left() - leftInset_ - rs - double(panX_)) / double(zoom_),
            (double(viewPoint.y) - client.top() - rs - double(panY_)) / double(zoom_));
    }

    BLPoint CanvasView::toView(const BLPoint& documentPoint) const {
        Rect client = getClientBounds();
        double rs = rulerSize();
        return BLPoint(client.left() + leftInset_ + rs + double(panX_) + documentPoint.x * double(zoom_),
            client.top() + rs + double(panY_) + documentPoint.y * double(zoom_));
    }

    void CanvasView::zoomAt(const Point& anchor, float newZoom) {
        float clamped = newZoom < kMinZoom ? kMinZoom : (newZoom > kMaxZoom ? kMaxZoom : newZoom);
        BLPoint before = toDocument(anchor);
        zoom_ = clamped;
        BLPoint after = toView(before);
        centered_ = false;
        panX_ += float(double(anchor.x) - after.x);
        panY_ += float(double(anchor.y) - after.y);
        style().markDirty();
        onZoomChanged(*this);
    }

    void CanvasView::setTool(SketchTool tool) {
        if (tool == tool_) {
            return;
        }
        if (tool_ == SketchTool::Pen && tool != SketchTool::Pen && document_ != nullptr) {
            finishPen(false);   // leaving the pen keeps what was drawn
        }
        if (tool_ == SketchTool::Text) {
            commitText();   // leaving the Text tool finishes the text
        }
        tool_ = tool;
        hasHover_ = false;
        drag_ = Drag::None;
        refreshCursor(lastPointer_, 0);
        style().markDirty();
        onToolChanged(*this);
    }

    // ---- Selection frame and handles (view space) ----

    BLPoint CanvasView::handlePosition(int handle, const SelectionFrame& frame) const {
        if (handle == kOriginHandle) {
            return toView(document_->selectionOrigin(frame));
        }
        static const double kFraction[8][2] = {{0, 0}, {0.5, 0}, {1, 0}, {1, 0.5}, {1, 1}, {0.5, 1}, {0, 1}, {0, 0.5}};
        QuadFrame quad = frame.asQuad();
        auto at = [&](double u, double v) { return toView(quad.map(u, v)); };
        if (handle != kRotateHandle) {
            return at(kFraction[handle][0], kFraction[handle][1]);
        }
        // The knob sits a fixed pixel distance outward from the top edge's midpoint, along that edge's normal.
        BLPoint topCenter = at(0.5, 0.0), topLeft = at(0.0, 0.0), topRight = at(1.0, 0.0);
        double dx = topRight.x - topLeft.x, dy = topRight.y - topLeft.y;
        double len = std::hypot(dx, dy);
        double nx = len > 1e-9 ? dy / len : 0.0, ny = len > 1e-9 ? -dx / len : -1.0;
        return BLPoint(topCenter.x + nx * kRotateKnobOffset, topCenter.y + ny * kRotateKnobOffset);
    }

    int CanvasView::hitHandle(const Point& viewPoint) const {
        SelectionFrame frame;
        if (document_ == nullptr || !document_->selectionFrame(frame)) {
            return -1;
        }
        // Rotate knob first so it wins over a coincident edge handle on a very short frame.
        for (int handle : {kOriginHandle, kRotateHandle, 0, 1, 2, 3, 4, 5, 6, 7}) {
            BLPoint pos = handlePosition(handle, frame);
            if (std::fabs(double(viewPoint.x) - pos.x) <= kHandleHit && std::fabs(double(viewPoint.y) - pos.y) <= kHandleHit) {
                return handle;
            }
        }
        return -1;
    }

    // Left button belongs to the tools; middle/right (and the hand tool) pan.
    SyncReturn CanvasView::handleMouseDown(View& /*sender*/, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        if (RootView* root = rootView()) {
            root->setFocusedSubView(this);
        }
        if (onPressFilter && onPressFilter(pt, buttons, keys)) {
            return SyncReturn::Handled;
        }
        bool wantsPan = (buttons & (mbmMiddleButton | mbmRightButton)) != 0 ||
            ((buttons & mbmLeftButton) != 0 && tool_ == SketchTool::Hand);
        if (wantsPan) {
            panning_ = true;
            panAnchor_ = pt;
            panStartX_ = panX_;
            panStartY_ = panY_;
            refreshCursor(pt, keys);
            return SyncReturn::Handled;
        }
        if ((buttons & mbmLeftButton) == 0 || document_ == nullptr) {
            return SyncReturn::Ignored;
        }

        if (tool_ == SketchTool::Text) {
            beginText(pt);   // finishes any text being edited first
            return SyncReturn::Handled;
        }
        commitText();

        snapCacheValid_ = false;   // the lines to snap to are collected again on the first snap of this press
        smartX_.clear();
        smartY_.clear();
        if (beginGuideDrag(pt)) {   // a press in a ruler, or on a guide
            return SyncReturn::Handled;
        }
        dragStartDoc_ = toDocument(pt);
        dragNowDoc_ = dragStartDoc_;
        dragNowView_ = pt;
        dragMoved_ = false;
        dragAdditive_ = (keys & kmShift) != 0;

        if (tool_ == SketchTool::Zoom) {  // click zooms in about the cursor, Shift-click out
            zoomAt(pt, zoom_ * ((keys & kmShift) != 0 ? 1.0f / 1.5f : 1.5f));
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Rectangle || tool_ == SketchTool::Ellipse || tool_ == SketchTool::Line || isShapeTool()) {
            dragStartDoc_ = snapPoint(dragStartDoc_);
            drag_ = Drag::Draw;
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Pencil || tool_ == SketchTool::Paintbrush) {
            pencilPoints_.assign(1, dragStartDoc_);
            drag_ = Drag::Pencil;
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Scissors) {
            document_->scissorsCut(dragStartDoc_, 6.0 / double(zoom_));   // a click on a path cuts it there
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Knife || tool_ == SketchTool::Eraser) {
            cutPoints_.assign(1, dragStartDoc_);
            drag_ = tool_ == SketchTool::Knife ? Drag::Knife : Drag::Erase;
            style().markDirty();
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::MagicWand) {
            const bool shift = (keys & kmShift) != 0;
            if (SketchObject* hit = document_->hitTest(dragStartDoc_, 4.0 / double(zoom_))) {
                document_->selectSimilar(hit->id(), shift, 0.08);   // a click selects what has the same fill (within a little)
            } else if (!shift) {
                document_->clearSelection();
            }
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Lasso) {
            cutPoints_.assign(1, dragStartDoc_);
            drag_ = Drag::Lasso;
            style().markDirty();
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Artboard) {
            artboardPress(dragStartDoc_);
            style().markDirty();
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Eyedropper) {
            document_->sampleLook(dragStartDoc_, 4.0 / double(zoom_));   // a click on an object copies its look onto the selection
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::ShapeBuilder) {
            shapeModel_ = document_->shapeBuilderModel();
            shapeHit_.assign(shapeModel_.cells.size(), false);
            shapeErase_ = (keys & kmAlt) != 0;
            cutPoints_.clear();
            drag_ = Drag::ShapeBuild;
            shapeBuildMove(dragStartDoc_);
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Pen) {
            penBegin(pt, keys);
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Gradient) {
            beginGradientDrag(pt, keys);
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Width) {
            beginWidthDrag(pt, keys);
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::DirectSelect) {
            directMouseDown(pt, keys);
            return SyncReturn::Handled;
        }
        if (isTransformTool()) {
            beginTransformTool();
            return SyncReturn::Handled;
        }
        if (tool_ != SketchTool::Select) {
            return SyncReturn::Ignored;  // palette entries not implemented yet
        }

        LiveShape live;
        if (liveShape(live)) {
            int liveHandle = liveHandleAt(pt, live);
            if (liveHandle != kLiveNone) {
                beginLiveDrag(liveHandle, live, pt);
                refreshCursor(pt, keys);
                return SyncReturn::Handled;
            }
        }
        int handle = hitHandle(pt);
        if (handle >= 0) {
            beginResize(handle, keys);
            refreshCursor(pt, keys);
            return SyncReturn::Handled;
        }

        double tolerance = 4.0 / double(zoom_);
        SketchObject* hit = document_->hitTest(dragStartDoc_, tolerance);
        if (hit != nullptr) {
            std::vector<std::size_t> selection = document_->selection();
            bool already = document_->isSelected(hit->id());
            // Clicking a grouped object takes its whole group; Ctrl-click picks the one object.
            bool single = (keys & kmCtrl) != 0;
            std::vector<std::size_t> picked = single ? std::vector<std::size_t>{hit->id()} : document_->withGroupMembers({hit->id()});
            if (dragAdditive_) {
                if (already) {
                    for (std::size_t id : picked) {
                        selection.erase(std::remove(selection.begin(), selection.end(), id), selection.end());
                    }
                } else {
                    selection.insert(selection.end(), picked.begin(), picked.end());
                }
                document_->setSelection(selection);
            } else if (!already || (single && document_->selection().size() > 1)) {
                document_->setSelection(picked);
            }
            if (document_->isSelected(hit->id())) {
                drag_ = Drag::Move;
                document_->selectionBounds(dragBounds_);
                dragBefore_ = document_->snapshotSelection();
                dragFrameBefore_ = document_->frameState();
                document_->selectionFrame(dragFrame_);
                refreshCursor(pt, keys);   // the move cursor for the drag
            }
            return SyncReturn::Handled;
        }

        if (!dragAdditive_) {
            document_->clearSelection();
        }
        drag_ = Drag::Marquee;
        return SyncReturn::Handled;
    }

    void CanvasView::beginResize(int handle, std::uint32_t keys) {
        dragHandle_ = handle;
        dragBefore_ = document_->snapshotSelection();
        dragFrameBefore_ = document_->frameState();
        document_->selectionFrame(dragFrame_);
        dragOrigin_ = document_->selectionOrigin(dragFrame_);
        bool altShift = (keys & kmAlt) != 0 && (keys & kmShift) != 0;
        if (handle == kOriginHandle) {
            drag_ = Drag::Origin;
        } else if (handle == kRotateHandle) {
            drag_ = Drag::Rotate;
            rotateStartAngle_ = std::atan2(dragStartDoc_.y - dragOrigin_.y, dragStartDoc_.x - dragOrigin_.x);
        } else if (dragFrame_.quad.has_value() || (altShift && handle % 2 == 0)) {
            drag_ = Drag::Warp;
            QuadFrame start = dragFrame_.asQuad();
            for (int i = 0; i < 4; ++i) {
                warpQuad_[i] = start.p[i];
            }
        } else if (altShift) {
            drag_ = Drag::Shear;
        } else {
            drag_ = Drag::Resize;
        }
    }

    void CanvasView::applyMapToSelection(const std::function<BLPoint(const BLPoint&)>& map, const QuadFrame& newQuad) {
        for (const GeometryState& entry : dragBefore_) {
            if (SketchObject* object = document_->findById(entry.id)) {
                object->setGeometry(entry.path);
                object->setTextMatrix(entry.textMatrix);
                object->warp(map);
                object->setFrameAngle(entry.angle);
            }
        }
        document_->setQuadFrame(newQuad);
        document_->touch();
    }

    void CanvasView::applyToSelection(const BLMatrix2D& matrix, double angleDelta) {
        dragMatrix_ = matrix;
        dragAngle_ = angleDelta;
        for (const GeometryState& entry : dragBefore_) {
            if (SketchObject* object = document_->findById(entry.id)) {
                object->setGeometry(entry.path);
                object->setTextMatrix(entry.textMatrix);
                object->transform(matrix);
                object->setFrameAngle(entry.angle + angleDelta);
            }
        }
        if (document_->selection().size() > 1) {
            // A rotated group keeps its own frame angle even when its members differ.
            document_->setGroupAngle(angleDelta == 0.0 ? dragFrameBefore_.groupAngle : std::optional<double>(dragFrame_.angle + angleDelta));
        }
        // A warped frame rides along with whatever is done to the objects.
        if (dragFrameBefore_.quad.has_value()) {
            document_->setQuadFrame(dragFrameBefore_.quad->transformed(matrix));
        }
        document_->touch();
    }

    void CanvasView::updateDrag(const Point& pt, std::uint32_t keys) {
        dragNowView_ = pt;
        dragNowDoc_ = toDocument(pt);
        bool shift = (keys & kmShift) != 0;
        dragShift_ = shift;
        const double kPi = 3.14159265358979323846;

        switch (drag_) {
            case Drag::Move: {
                double dx = dragNowDoc_.x - dragStartDoc_.x;
                double dy = dragNowDoc_.y - dragStartDoc_.y;
                if (!dragMoved_ && std::fabs(dx * double(zoom_)) < 3.0 && std::fabs(dy * double(zoom_)) < 3.0) {
                    return;  // a click, not yet a drag
                }
                dragMoved_ = true;
                if (shift) {  // constrain to the dominant axis
                    if (std::fabs(dx) > std::fabs(dy)) { dy = 0.0; } else { dx = 0.0; }
                }
                {   // an edge or the middle of the selection's box jumps onto a grid line, a guide or another object's edge or center
                    double lineX = std::nan(""), lineY = std::nan("");
                    auto pull = [this](int axis, double lo, double hi, double delta, double& line) {
                        double best = delta, bestGap = 1e30;
                        line = std::nan("");
                        for (double edge : {lo, (lo + hi) * 0.5, hi}) {
                            double caught = std::nan("");
                            double snapped = snapAxis(axis, edge + delta, &caught);
                            double gap = std::fabs(snapped - (edge + delta));
                            if (gap > 0.0 && gap < bestGap) {
                                bestGap = gap;
                                best = snapped - edge;
                                line = caught;
                            }
                        }
                        return best;
                    };
                    if (!shift || dx != 0.0) { dx = pull(0, dragBounds_.x0, dragBounds_.x1, dx, lineX); }
                    if (!shift || dy != 0.0) { dy = pull(1, dragBounds_.y0, dragBounds_.y1, dy, lineY); }
                    rememberSnapLines(lineX, lineY);
                }
                applyToSelection(BLMatrix2D::make_translation(dx, dy));
                break;
            }
            case Drag::Resize: {
                // Everything below happens in the frame's own (rotated) coordinates, then the scale is
                // conjugated back into document space: M = R(a) * Scale * R(-a).
                const BLBox& box = dragFrame_.local;
                BLPoint mouse = dragFrame_.toLocal(dragNowDoc_);
                const int h = dragHandle_;
                bool movesLeft = (h == 0 || h == 6 || h == 7);
                bool movesRight = (h == 2 || h == 3 || h == 4);
                bool movesTop = (h == 0 || h == 1 || h == 2);
                bool movesBottom = (h == 4 || h == 5 || h == 6);
                if (std::fabs(dragFrame_.angle) < 1e-9 && !dragFrameBefore_.quad.has_value()) {
                    // An upright frame's edges snap to the grid, guides and other objects, like a moved edge does.
                    double lineX = std::nan(""), lineY = std::nan("");
                    if (movesLeft || movesRight) { mouse.x = snapAxis(0, mouse.x, &lineX); }
                    if (movesTop || movesBottom) { mouse.y = snapAxis(1, mouse.y, &lineY); }
                    rememberSnapLines(lineX, lineY);
                }
                double ax = movesLeft ? box.x1 : box.x0;
                double ay = movesTop ? box.y1 : box.y0;
                if ((keys & kmAlt) != 0) {  // Alt: scale about the center instead of the opposite side
                    ax = (box.x0 + box.x1) * 0.5;
                    ay = (box.y0 + box.y1) * 0.5;
                }
                double grabX = movesLeft ? box.x0 : box.x1;
                double grabY = movesTop ? box.y0 : box.y1;
                double sx = 1.0, sy = 1.0;
                if ((movesLeft || movesRight) && std::fabs(grabX - ax) > 1e-6) {
                    sx = (mouse.x - ax) / (grabX - ax);
                }
                if ((movesTop || movesBottom) && std::fabs(grabY - ay) > 1e-6) {
                    sy = (mouse.y - ay) / (grabY - ay);
                }
                if (shift && (movesLeft || movesRight) && (movesTop || movesBottom)) {
                    double s = std::max(std::fabs(sx), std::fabs(sy));
                    sx = sx < 0 ? -s : s;
                    sy = sy < 0 ? -s : s;
                }
                if (std::fabs(sx) < 0.01) { sx = sx < 0 ? -0.01 : 0.01; }
                if (std::fabs(sy) < 0.01) { sy = sy < 0 ? -0.01 : 0.01; }
                BLMatrix2D m = BLMatrix2D::make_rotation(-dragFrame_.angle);
                m.post_translate(-ax, -ay);
                m.post_scale(sx, sy);
                m.post_translate(ax, ay);
                m.post_rotate(dragFrame_.angle);
                dragMoved_ = true;
                applyToSelection(m);
                break;
            }
            case Drag::Rotate: {
                BLPoint c = dragOrigin_;
                double angle = std::atan2(dragNowDoc_.y - c.y, dragNowDoc_.x - c.x) - rotateStartAngle_;
                if (shift) {
                    double step = kPi / 12.0;  // 15 degrees
                    angle = std::round(angle / step) * step;
                }
                dragMoved_ = true;
                // make_rotation(angle, x, y) does not pivot about (x, y) in this Blend2D - build it.
                BLMatrix2D m = BLMatrix2D::make_translation(-c.x, -c.y);
                m.post_rotate(angle);
                m.post_translate(c.x, c.y);
                applyToSelection(m, angle);
                break;
            }
            case Drag::Shear: {
                // Shear in the frame's own axes about the transform origin, the dragged handle following
                // the mouse: a top/bottom handle slides horizontally, a left/right handle vertically.
                const BLBox& box = dragFrame_.local;
                BLPoint mouse = dragFrame_.toLocal(dragNowDoc_);
                BLPoint origin = dragFrame_.toLocal(dragOrigin_);
                const int h = dragHandle_;
                double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
                BLMatrix2D shear = BLMatrix2D::make_identity();
                if (h == 1 || h == 5) {
                    double handleY = h == 1 ? box.y0 : box.y1;
                    double lever = handleY - origin.y;
                    if (std::fabs(lever) > 1e-6) {
                        shear = BLMatrix2D(1.0, 0.0, (mouse.x - cx) / lever, 1.0, 0.0, 0.0);
                    }
                } else {
                    double handleX = h == 7 ? box.x0 : box.x1;
                    double lever = handleX - origin.x;
                    if (std::fabs(lever) > 1e-6) {
                        shear = BLMatrix2D(1.0, (mouse.y - cy) / lever, 0.0, 1.0, 0.0, 0.0);
                    }
                }
                BLMatrix2D m = BLMatrix2D::make_rotation(-dragFrame_.angle);
                m.post_translate(-origin.x, -origin.y);
                m.post_transform(shear);
                m.post_translate(origin.x, origin.y);
                m.post_rotate(dragFrame_.angle);
                dragMoved_ = true;
                applyToSelection(m);
                break;
            }
            case Drag::Warp: {
                // Edit the frame quad - a corner follows the mouse, an edge slides along its normal - and carry
                // every point by the projective map taking the old quad onto the new one (straight lines stay straight).
                QuadFrame source = dragFrame_.asQuad();
                QuadFrame dest = source;
                const int h = dragHandle_;
                if (h % 2 == 0) {
                    dest.p[h / 2] = dragNowDoc_;
                } else {
                    int i0 = h / 2, i1 = (i0 + 1) % 4;
                    double ex = source.p[i1].x - source.p[i0].x, ey = source.p[i1].y - source.p[i0].y;
                    double len = std::hypot(ex, ey);
                    if (len < 1e-9) {
                        break;
                    }
                    double nx = ey / len, ny = -ex / len;
                    double k = (dragNowDoc_.x - dragStartDoc_.x) * nx + (dragNowDoc_.y - dragStartDoc_.y) * ny;
                    dest.p[i0] = BLPoint(dest.p[i0].x + nx * k, dest.p[i0].y + ny * k);
                    dest.p[i1] = BLPoint(dest.p[i1].x + nx * k, dest.p[i1].y + ny * k);
                }
                for (int i = 0; i < 4; ++i) {
                    warpQuad_[i] = dest.p[i];
                }
                double su = 0.0, sv = 0.0;
                if (!source.unmap(source.p[0], su, sv)) {
                    break;  // degenerate frame
                }
                auto map = [source, dest](const BLPoint& p) {
                    double u = 0.0, v = 0.0;
                    if (!source.unmap(p, u, v)) {
                        return p;
                    }
                    return dest.map(u, v);
                };
                dragMoved_ = true;
                applyMapToSelection(map, dest);
                break;
            }
            case Drag::Origin:
                document_->setSelectionOrigin(dragFrame_, dragNowDoc_);
                break;
            case Drag::Draw: {
                dragNowDoc_ = snapPoint(dragNowDoc_);  // the rubber band shows the snapped corner too
                BLPoint snapped = toView(dragNowDoc_);
                dragNowView_ = Point(float(snapped.x), float(snapped.y));
                dragMoved_ = true;
                style().markDirty();
                break;
            }
            case Drag::Marquee:
                dragMoved_ = true;
                style().markDirty();
                break;
            case Drag::GradientNew:
            case Drag::GradientEnd:
            case Drag::GradientStop:
                updateGradientDrag(keys);
                break;
            case Drag::Width:
                updateWidthDrag(keys);
                break;
            case Drag::Pencil:
                pencilMove(dragNowDoc_);
                break;
            case Drag::Knife:
            case Drag::Erase:
                cutMove(dragNowDoc_);
                break;
            case Drag::Lasso:
                cutMove(dragNowDoc_);
                break;
            case Drag::ShapeBuild:
                shapeErase_ = (keys & kmAlt) != 0;
                shapeBuildMove(dragNowDoc_);
                break;
            case Drag::ArtboardNew:
            case Drag::ArtboardMove:
            case Drag::ArtboardResize:
                artboardMove(dragNowDoc_, keys);
                break;
            case Drag::Guide:
                updateGuideDrag(pt);
                break;
            case Drag::TransformTool:
                updateTransformTool(keys);
                break;
            case Drag::PenHandle:
                penDrag(dragNowDoc_, (keys & kmAlt) != 0);
                break;
            case Drag::DirectEdit:
                directDrag(dragNowDoc_, (keys & kmAlt) != 0);
                break;
            case Drag::LiveShape:
                updateLiveDrag(dragNowDoc_, pt);
                break;
            case Drag::None:
                break;
        }
    }

    namespace {
        BLBox normalizedBox(const BLPoint& a, const BLPoint& b) {
            return BLBox(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
        }
    }

    void CanvasView::finishDrag(const Point& pt, std::uint32_t keys) {
        updateDrag(pt, keys);
        Drag finished = drag_;
        drag_ = Drag::None;
        bool shift = (keys & kmShift) != 0;

        switch (finished) {
            case Drag::Move:
            case Drag::Resize:
            case Drag::Rotate:
            case Drag::Shear:
            case Drag::Warp:
                if (dragMoved_) {
                    const char* what = "Move";
                    switch (finished) {
                        case Drag::Resize: what = "Resize"; break;
                        case Drag::Rotate: what = "Rotate"; break;
                        case Drag::Shear: what = "Shear"; break;
                        case Drag::Warp: what = "Warp"; break;
                        default: break;
                    }
                    document_->commitGeometry(dragBefore_, what, dragFrameBefore_);
                    if (finished == Drag::Move || finished == Drag::Resize || finished == Drag::Rotate) {
                        document_->noteTransform(dragMatrix_, dragAngle_);
                    }
                }
                break;
            case Drag::Origin:
                break;
            case Drag::Marquee: {
                BLBox box = normalizedBox(dragStartDoc_, dragNowDoc_);
                document_->selectInRect(box, dragAdditive_);
                style().markDirty();
                break;
            }
            case Drag::Draw: {
                if (isShapeTool()) {
                    finishShapeDraw(shift);
                    break;
                }
                BLPoint a = dragStartDoc_;
                BLPoint b = dragNowDoc_;
                double w = b.x - a.x, h = b.y - a.y;
                if (tool_ == SketchTool::Line) {
                    if (shift) {  // snap to 45 degree increments
                        double len = std::hypot(w, h);
                        double step = 3.14159265358979323846 / 4.0;
                        double angle = std::round(std::atan2(h, w) / step) * step;
                        b = BLPoint(a.x + len * std::cos(angle), a.y + len * std::sin(angle));
                    }
                    if (std::hypot(b.x - a.x, b.y - a.y) < 2.0) {
                        break;
                    }
                    SketchObject* line = SketchObject::makeLine(a.x, a.y, b.x, b.y);
                    line->setName("Line");
                    line->setStroke(document_->defaultStroke().isNone() ? SketchPaint::solid(SketchColor::gray(0.0f)) : document_->defaultStroke());
                    line->setStrokeWidth(document_->defaultStrokeWidth());
                    line->setStrokeOptions(document_->defaultStrokeOptions());
                    document_->addUndoable(line, "Draw Line");
                    break;
                }
                if (shift) {
                    double side = std::max(std::fabs(w), std::fabs(h));
                    b = BLPoint(a.x + (w < 0 ? -side : side), a.y + (h < 0 ? -side : side));
                }
                BLBox box = normalizedBox(a, b);
                double bw = box.x1 - box.x0, bh = box.y1 - box.y0;
                if (bw < 2.0 || bh < 2.0) {
                    break;
                }
                SketchObject* shape = tool_ == SketchTool::Ellipse
                    ? SketchObject::makeEllipse(box.x0 + bw * 0.5, box.y0 + bh * 0.5, bw * 0.5, bh * 0.5)
                    : SketchObject::makeRect(box.x0, box.y0, bw, bh);
                shape->setName(tool_ == SketchTool::Ellipse ? "Ellipse" : "Rectangle");
                shape->setFill(document_->defaultFill());
                shape->setStroke(document_->defaultStroke());
                shape->setStrokeWidth(document_->defaultStrokeWidth());
                shape->setStrokeOptions(document_->defaultStrokeOptions());
                document_->addUndoable(shape, tool_ == SketchTool::Ellipse ? "Draw Ellipse" : "Draw Rectangle");
                break;
            }
            case Drag::GradientNew:
            case Drag::GradientEnd:
            case Drag::GradientStop:
                finishGradientDrag();
                break;
            case Drag::Width:
                finishWidthDrag();
                break;
            case Drag::Pencil:
                finishPencil();
                break;
            case Drag::Guide:
                finishGuideDrag(pt);
                break;
            case Drag::Lasso: {
                std::vector<BLPoint> lasso = cutPoints_;
                cutPoints_.clear();
                if (lasso.size() < 3) {
                    if (!((keys & kmShift) != 0)) {
                        document_->clearSelection();   // a click in empty space
                    }
                } else {
                    document_->selectInPolygon(lasso, (keys & kmShift) != 0);
                }
                style().markDirty();
                break;
            }
            case Drag::ShapeBuild:
                finishShapeBuild();
                break;
            case Drag::ArtboardNew:
            case Drag::ArtboardMove:
            case Drag::ArtboardResize:
                finishArtboard(finished);
                break;
            case Drag::Knife:
                finishKnife();
                break;
            case Drag::Erase:
                finishErase();
                break;
            case Drag::PenHandle:
                break;   // the anchor was placed on mouse-down; the drag only shaped its handles
            case Drag::DirectEdit:
                finishDirectDrag();
                break;
            case Drag::LiveShape:
                finishLiveDrag();
                break;
            case Drag::TransformTool:
                finishTransformTool(keys);
                break;
            case Drag::None:
                break;
        }
        dragBefore_.clear();
        style().markDirty();
    }

    SyncReturn CanvasView::handleMouseMove(View& /*sender*/, const Point& pt, std::uint32_t /*buttons*/, std::uint32_t keys) {
        pointerDoc_ = toDocument(pt);
        onPointerMoved(*this);
        refreshCursor(pt, keys);
        updateHover();
        if (panning_) {
            setPan(panStartX_ + (pt.x - panAnchor_.x), panStartY_ + (pt.y - panAnchor_.y));
            return SyncReturn::Handled;
        }
        if (drag_ != Drag::None) {
            updateDrag(pt, keys);
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Pen && !penAnchors_.empty()) {
            dragNowView_ = pt;
            dragNowDoc_ = toDocument(pt);
            style().markDirty();   // the segment from the last point follows the pointer
        }
        return SyncReturn::Ignored;
    }

    namespace {
        // The two-headed arrow along the direction (dx, dy), in 45 degree steps - so a handle of a turned frame
        // shows the arrow that matches how it will actually stretch.
        const char* resizeCursorFor(double dx, double dy) {
            double steps = std::atan2(dy, dx) / (3.14159265358979323846 / 4.0);
            int k = ((int(std::lround(steps)) % 4) + 4) % 4;  // 0 = horizontal, 1 = down-right, 2 = vertical, 3 = up-right
            static const char* const kNames[4] = {"resize-we", "resize-nwse", "resize-ns", "resize-nesw"};
            return kNames[k];
        }

        // The system cursor that stands in when a cursor image cannot be loaded.
        CursorKind fallbackFor(const std::string& name) {
            if (name == "crosshair" || name == "gradient" || name == "pen" || name == "pencil" || name == "scissors" || name == "knife" || name == "eraser") return CursorKind::Cross;
            if (name == "text") return CursorKind::IBeam;
            if (name == "hand" || name == "hand-grab" || name == "rotate") return CursorKind::Hand;
            if (name == "resize-ns") return CursorKind::SizeNS;
            if (name == "resize-we") return CursorKind::SizeWE;
            if (name == "resize-nwse") return CursorKind::SizeNWSE;
            if (name == "resize-nesw") return CursorKind::SizeNESW;
            if (name == "move") return CursorKind::SizeAll;
            return CursorKind::Arrow;
        }
    }

    const char* CanvasView::liveHandleCursor(const LiveShape& shape, int handle) const {
        BLPoint at = liveHandlePoint(shape, handle);
        switch (handle) {
            case kLivePoints:
                return "resize-we";   // dragged left and right, whatever the shape turn
            case kLiveInner: {
                BLPoint center = toView(shape.regular.center);
                return resizeCursorFor(at.x - center.x, at.y - center.y);   // toward or away from the center
            }
            case kLiveRadius: {
                double c = std::cos(shape.rect.angle), s = std::sin(shape.rect.angle);
                return resizeCursorFor(c - s, s + c);   // along the corner diagonal, turned with the frame
            }
            case kLiveArcStart:
            case kLiveArcEnd: {
                BLPoint center = toView(shape.arc.center);
                return resizeCursorFor(-(at.y - center.y), at.x - center.x);   // around the center: along the tangent
            }
            default:
                return "move";
        }
    }

    const char* CanvasView::toolCursorName(SketchTool tool) {
        switch (tool) {
            case SketchTool::Select: return "select";
            case SketchTool::DirectSelect: return "direct-select";
            case SketchTool::Pen: return "pen";
            case SketchTool::Pencil: return "pencil";
            case SketchTool::Paintbrush: return "pencil";
            case SketchTool::Scissors: return "scissors";
            case SketchTool::Knife: return "knife";
            case SketchTool::Eraser: return "eraser";
            case SketchTool::ShapeBuilder: return "crosshair";
            case SketchTool::Eyedropper: return "crosshair";
            case SketchTool::MagicWand: return "crosshair";
            case SketchTool::Lasso: return "crosshair";
            case SketchTool::Text: return "text";
            case SketchTool::Gradient: return "gradient";
            case SketchTool::Hand: return "hand";
            case SketchTool::Zoom: return "zoom-in";
            default: return "crosshair";   // the shape tools, line and width
        }
    }

    // Shows Resources/cursors/<name>.svg with its hotspot from cursors.json; a system cursor if the image is missing.
    void CanvasView::setCanvasCursor(const std::string& name) {
        if (name == cursorName_) {
            return;
        }
        cursorName_ = name;
        if (!cursorHotspotsLoaded_) {
            cursorHotspotsLoaded_ = true;
            cursorHotspots_ = parseCursorHotspots(Bundle::instance().loadTextFile("cursors/cursors.json"));
        }
        CursorHotspot spot;
        auto found = cursorHotspots_.find(name);
        if (found != cursorHotspots_.end()) {
            spot = found->second;
        }
        // The SVGs are drawn for a 32 px cursor, which looks large on screen: rasterize them smaller (see kCursorScale)
        // and move the hotspot with the image so the point stays where it was drawn.
        static const double scale = [] {
            const char* text = std::getenv("SKETCH_CURSOR_SCALE");
            double value = text != nullptr ? std::atof(text) : 0.0;
            return value >= 0.2 && value <= 2.0 ? value : kCursorScale;
        }();
        int size = std::max(12, int(std::lround(double(spot.size) * scale)));
        int hotX = int(std::lround(double(spot.x) * scale));
        int hotY = int(std::lround(double(spot.y) * scale));
        BLImage image;
        if (Bundle::instance().loadCachedImage("cursors/" + name + ".svg", image, size, size) && cursor().setImage(image, hotX, hotY, size)) {
            return;
        }
        cursor().setCursorKind(fallbackFor(name));
    }

    void CanvasView::refreshCursor(const Point& pt, std::uint32_t keys) {
        lastPointer_ = pt;
        std::string name = toolCursorName(tool_);
        if (panning_) {
            name = "hand-grab";
        } else if (drag_ == Drag::Move || drag_ == Drag::Origin) {
            name = "move";
        } else if (drag_ == Drag::Rotate) {
            name = "rotate";
        } else if (drag_ == Drag::Guide) {
            name = guideDragVertical_ ? "resize-we" : "resize-ns";
        } else if (drag_ == Drag::LiveShape) {
            name = liveHandleCursor(liveStart_, liveHandle_);   // keeps its arrow for the whole drag
        } else if (drag_ != Drag::None) {
            // A resize keeps the arrow it began with; any other drag keeps the tool's own cursor.
            if (drag_ == Drag::Resize || drag_ == Drag::Shear || drag_ == Drag::Warp) {
                return;
            }
        } else if (tool_ == SketchTool::Zoom) {
            name = (keys & kmShift) != 0 ? "zoom-out" : "zoom-in";
        } else if (tool_ == SketchTool::Select && document_ != nullptr) {
            SelectionFrame frame;
            LiveShape live;
            int liveHandle = liveShape(live) ? liveHandleAt(pt, live) : kLiveNone;
            int handle = liveHandle != kLiveNone ? -1 : hitHandle(pt);
            if (liveHandle != kLiveNone) {
                name = liveHandleCursor(live, liveHandle);
            }
            if (handle >= 0 && document_->selectionFrame(frame)) {
                if (handle == kOriginHandle) {
                    name = "move";
                } else if (handle == kRotateHandle) {
                    name = "rotate";
                } else {
                    BLPoint center = toView(frame.pointAt(0.5, 0.5)), at = handlePosition(handle, frame);
                    name = resizeCursorFor(at.x - center.x, at.y - center.y);
                }
            } else if (liveHandle == kLiveNone && !guidesLocked_) {
                int guide = guideAt(pt);   // over a guide: the arrow it can be dragged along
                if (guide >= 0) {
                    name = document_->guides()[std::size_t(guide)].vertical ? "resize-we" : "resize-ns";
                }
            }
        }
        setCanvasCursor(name);
    }

    SyncReturn CanvasView::handleMouseUp(View& /*sender*/, const Point& pt, std::uint32_t /*buttons*/, std::uint32_t keys) {
        if (panning_) {
            panning_ = false;
            refreshCursor(pt, keys);
            return SyncReturn::Handled;
        }
        if (drag_ != Drag::None) {
            finishDrag(pt, keys);
            smartX_.clear();
            smartY_.clear();
            snapCacheValid_ = false;
            refreshCursor(pt, keys);
            style().markDirty();
            return SyncReturn::Handled;
        }
        return SyncReturn::Ignored;
    }

    namespace {
        std::wstring widenUtf8(const std::string& text) {
            if (text.empty()) { return std::wstring(); }
            int n = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
            std::wstring out(std::size_t(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
            return out;
        }

        std::string narrowUtf8(const std::wstring& text) {
            if (text.empty()) { return std::string(); }
            int n = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
            std::string out(std::size_t(n), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
            return out;
        }
    }

    bool CanvasView::copySelection() {
        if (document_ == nullptr) {
            return false;
        }
        std::string svg = document_->selectionToSvg();
        if (svg.empty() || !ClipboardManager::setText(widenUtf8(svg), this)) {
            return false;
        }
        pasteCount_ = 0;
        return true;
    }

    bool CanvasView::cutSelection() {
        if (!copySelection()) {
            return false;
        }
        document_->deleteSelection();
        return true;
    }

    bool CanvasView::paste(bool inPlace) {
        std::wstring text;
        if (document_ == nullptr || !ClipboardManager::getText(text)) {
            return false;
        }
        double offset = inPlace ? 0.0 : 12.0 * double(++pasteCount_);
        return document_->pasteSvg(narrowUtf8(text), offset, offset);
    }

    SyncReturn CanvasView::handleKeyDown(View& /*sender*/, std::uint32_t keyMask, int /*keyChar*/, int /*repeatCount*/, std::uint32_t vkCode) {
        if (document_ == nullptr) {
            return SyncReturn::Ignored;
        }
        if (!textEditing_ && onKeyFilter && onKeyFilter(keyMask, vkCode)) {
            return SyncReturn::Handled;
        }
        if (textEditing_ && textKeyDown(keyMask, vkCode)) {
            return SyncReturn::Handled;   // typing text: no shortcut or Delete reaches the objects
        }
        bool ctrl = (keyMask & kmCtrl) != 0;
        bool shift = (keyMask & kmShift) != 0;
        if (ctrl && onControlKey && onControlKey(vkCode, shift)) {
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterZ) {
            document_->undoStack().undo();
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterY) {
            document_->undoStack().redo();
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterA) {
            if (shift) { document_->clearSelection(); } else { document_->selectAll(); }
            return SyncReturn::Handled;
        }
        if (ctrl && shift && vkCode == vkLetterB) {
            document_->resetBoundingBox();
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterC) {
            copySelection();
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterX) {
            cutSelection();
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterV) {
            paste(shift);
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkNumber7) {
            if (shift) { document_->releaseClippingMask(); } else { document_->makeClippingMask(); }
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterG) {
            if (shift) { document_->ungroupSelection(); } else { document_->groupSelection(); }
            return SyncReturn::Handled;
        }
        if (ctrl && shift && vkCode == vkLetterD) {
            document_->transformAgain();
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterD) {
            document_->duplicateSelection(12.0, 12.0);
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Pen && !penAnchors_.empty()) {
            if (vkCode == vkReturn) { finishPen(false); return SyncReturn::Handled; }
            if (vkCode == vkEscape) { if (penAnchors_.size() >= 2) { finishPen(false); } else { cancelPen(); } return SyncReturn::Handled; }
            if (vkCode == vkBackSpace || vkCode == vkDelete) { penUndoLast(); return SyncReturn::Handled; }
        }
        if (tool_ == SketchTool::Eraser && (vkCode == vkUpArrow || vkCode == vkDownArrow)) {
            setEraserSize(eraserSize_ + (vkCode == vkUpArrow ? 2.0 : -2.0));
            return SyncReturn::Handled;
        }
        if ((vkCode == vkUpArrow || vkCode == vkDownArrow) && adjustShapeParameter(vkCode == vkUpArrow ? 1 : -1)) {
            return SyncReturn::Handled;
        }
        if (vkCode == vkDelete && tool_ == SketchTool::Artboard) {
            document_->deleteArtboard(document_->activeArtboard());
            return SyncReturn::Handled;
        }
        if (vkCode == vkDelete && tool_ == SketchTool::DirectSelect && deleteDirectAnchor()) {
            return SyncReturn::Handled;
        }
        if (vkCode == vkDelete) {
            document_->deleteSelection();
            return SyncReturn::Handled;
        }
        if (vkCode == vkEscape) {
            pencilPoints_.clear();
            cutPoints_.clear();
            if (drag_ != Drag::None) {  // cancel an in-progress drag, restoring the outlines
                if (drag_ == Drag::GradientNew || drag_ == Drag::GradientEnd || drag_ == Drag::GradientStop) {
                    cancelGradientDrag();
                } else if (drag_ == Drag::Width) {
                    cancelWidthDrag();
                } else if (!dragBefore_.empty()) {
                    applyToSelection(BLMatrix2D::make_identity());
                }
                drag_ = Drag::None;
                dragBefore_.clear();
                style().markDirty();
            } else {
                document_->clearSelection();
            }
            return SyncReturn::Handled;
        }
        double step = shift ? 10.0 : 1.0;
        double dx = 0.0, dy = 0.0;
        switch (vkCode) {
            case vkLeftArrow: dx = -step; break;
            case vkRightArrow: dx = step; break;
            case vkUpArrow: dy = -step; break;
            case vkDownArrow: dy = step; break;
            default: return SyncReturn::Ignored;
        }
        if (!document_->selection().empty()) {
            document_->transformObjects(document_->selection(), BLMatrix2D::make_translation(dx, dy), "Nudge");
        }
        return SyncReturn::Handled;
    }

    // A double click on text with the Select tool edits it.
    SyncReturn CanvasView::handleMouseDblClick(View& /*sender*/, const Point& pt, std::uint32_t buttons, std::uint32_t /*keys*/) {
        if (document_ == nullptr || (buttons & mbmLeftButton) == 0 || tool_ != SketchTool::Select) {
            return SyncReturn::Ignored;
        }
        if (textObjectAt(toDocument(pt)) == nullptr) {
            return SyncReturn::Ignored;
        }
        setTool(SketchTool::Text);
        beginText(pt);
        return SyncReturn::Handled;
    }

    void CanvasView::paintOverlay(BLContext& ctx) const {
        if (document_ == nullptr) {
            return;
        }
        paintGuides(ctx);
        if (textEditing_) {
            paintTextEditor(ctx);
        }
        // The selection is drawn in the colour of the layer it is in.
        BLRgba32 accent(0xFF1E6FE0u);
        if (const Layer* selectedLayer = document_->findLayer(document_->selectionLayer())) {
            accent = BLRgba32(layerColor(*selectedLayer) | 0xFF000000u);
        }
        ctx.save();
        ctx.set_stroke_width(1.0);
        ctx.set_stroke_style(accent);

        // A closed outline through the four corners of a frame (rectangle or warped quad), in view pixels.
        auto frameOutline = [this](const SelectionFrame& f) {
            QuadFrame quad = f.asQuad();
            BLPath path;
            for (int i = 0; i < 4; ++i) {
                BLPoint p = toView(quad.p[i]);
                if (i == 0) { path.move_to(p.x, p.y); } else { path.line_to(p.x, p.y); }
            }
            path.close();
            return path;
        };

        // With several objects selected, each gets its own (oriented) outline under the group frame.
        if (document_->selection().size() > 1) {
            for (std::size_t id : document_->selection()) {
                const SketchObject* object = document_->findById(id);
                if (object == nullptr) {
                    continue;
                }
                SelectionFrame own;
                own.angle = object->frameAngle();
                own.local = object->frameBounds();
                if (const Layer* ownLayer = document_->findLayer(object->layer())) {
                    ctx.set_stroke_style(BLRgba32(layerColor(*ownLayer) | 0xFF000000u));
                }
                ctx.stroke_path(frameOutline(own));
            }
            ctx.set_stroke_style(accent);
        }

        // The group frame with its handles.
        SelectionFrame frame;
        if (document_->selectionFrame(frame)) {
            ctx.stroke_path(frameOutline(frame));
            if (drag_ != Drag::Marquee && drag_ != Drag::Draw && tool_ != SketchTool::Gradient && tool_ != SketchTool::Width && tool_ != SketchTool::DirectSelect) {
                BLPoint knob = handlePosition(kRotateHandle, frame);
                BLPoint topCenter = handlePosition(1, frame);
                BLPath stem;
                stem.move_to(topCenter.x, topCenter.y);
                stem.line_to(knob.x, knob.y);
                ctx.stroke_path(stem);
                for (int handle = 0; handle <= kRotateHandle; ++handle) {
                    BLPoint pos = handlePosition(handle, frame);
                    if (handle == kRotateHandle) {
                        BLPath circle;
                        circle.add_circle(BLCircle(pos.x, pos.y, 4.5));
                        ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
                        ctx.fill_path(circle);
                        ctx.stroke_path(circle);
                    } else {
                        BLRect box(std::floor(pos.x) - 3.0, std::floor(pos.y) - 3.0, 7.0, 7.0);
                        ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
                        ctx.fill_rect(box);
                        ctx.stroke_rect(BLRect(box.x + 0.5, box.y + 0.5, box.w - 1.0, box.h - 1.0));
                    }
                }
            }
        }

        // The transform origin: a ringed dot (drag it to move the rotate/shear pivot).
        SelectionFrame originFrame;
        if (drag_ != Drag::Marquee && drag_ != Drag::Draw && tool_ != SketchTool::Gradient && tool_ != SketchTool::Width && tool_ != SketchTool::DirectSelect && document_->selectionFrame(originFrame)) {
            BLPoint o = handlePosition(kOriginHandle, originFrame);
            BLPath ring;
            ring.add_circle(BLCircle(o.x, o.y, 4.5));
            ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
            ctx.fill_path(ring);
            ctx.stroke_path(ring);
            BLPath cross;
            cross.move_to(o.x - 7.0, o.y);
            cross.line_to(o.x + 7.0, o.y);
            cross.move_to(o.x, o.y - 7.0);
            cross.line_to(o.x, o.y + 7.0);
            ctx.stroke_path(cross);
            ctx.set_fill_style(accent);
            ctx.fill_circle(BLCircle(o.x, o.y, 1.6));
        }

        paintHover(ctx);
        if (tool_ == SketchTool::Gradient) {
            paintGradientOverlay(ctx);
        }
        if (tool_ == SketchTool::Width) {
            paintWidthOverlay(ctx);
        }
        if (drag_ == Drag::Pencil) {
            paintPencilPreview(ctx);
        }
        if (drag_ == Drag::Knife || drag_ == Drag::Erase || tool_ == SketchTool::Eraser) {
            paintCutOverlay(ctx);
        }
        if (drag_ == Drag::Lasso) {
            paintLasso(ctx);
        }
        if (drag_ == Drag::ShapeBuild) {
            paintShapeBuilder(ctx);
        }
        if (tool_ == SketchTool::Artboard) {
            paintArtboardTool(ctx);
        }
        if (tool_ == SketchTool::Pen) {
            paintPenOverlay(ctx);
        }
        if (tool_ == SketchTool::DirectSelect) {
            paintDirectOverlay(ctx);
        }
        if (tool_ == SketchTool::Select) {
            paintLiveOverlay(ctx);
        }
        if (isTransformTool()) {
            paintTransformTool(ctx);
        }

        // Rubber band for marquee / shape being drawn.
        if (drag_ == Drag::Draw && isShapeTool()) {
            paintShapePreview(ctx);
        } else if (drag_ == Drag::Marquee || drag_ == Drag::Draw) {
            BLPoint a = toView(dragStartDoc_);
            BLPoint b(double(dragNowView_.x), double(dragNowView_.y));
            if (drag_ == Drag::Draw && tool_ == SketchTool::Line) {
                BLPath line;
                line.move_to(a.x, a.y);
                line.line_to(b.x, b.y);
                ctx.stroke_path(line);
            } else {
                BLRect band(std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y));
                if (drag_ == Drag::Marquee) {
                    ctx.set_fill_style(BLRgba32(0x261E6FE0u));
                    ctx.fill_rect(band);
                }
                if (drag_ == Drag::Draw && tool_ == SketchTool::Ellipse) {
                    BLPath oval;
                    oval.add_ellipse(BLEllipse(band.x + band.w * 0.5, band.y + band.h * 0.5, band.w * 0.5, band.h * 0.5));
                    ctx.stroke_path(oval);
                } else {
                    ctx.stroke_rect(band);
                }
            }
        }
        ctx.restore();
    }

    // ---- Width tool ----

    SketchObject* CanvasView::widthTarget() const {
        if (document_ == nullptr || document_->selection().size() != 1) {
            return nullptr;
        }
        SketchObject* object = document_->findById(document_->selection().front());
        return object != nullptr && !object->stroke().isNone() && object->strokeWidth() > 0.0 ? object : nullptr;
    }

    bool CanvasView::widthHandles(const SketchObject& object, const WidthPoint& point, BLPoint& center, BLPoint& left, BLPoint& right) const {
        BLPoint p, n;
        if (!object.pathSample(point.t, p, n)) {
            return false;
        }
        double half = object.strokeWidth() * 0.5;
        center = toView(p);
        left = toView(BLPoint(p.x + n.x * half * point.left, p.y + n.y * half * point.left));
        right = toView(BLPoint(p.x - n.x * half * point.right, p.y - n.y * half * point.right));
        return true;
    }

    void CanvasView::setWidths(const std::vector<WidthPoint>& widths) {
        document_->changeSelectionStrokeOptions([&widths](StrokeOptions& o) { o.widths = widths; });
    }

    void CanvasView::beginWidthDrag(const Point& pt, std::uint32_t keys) {
        dragMoved_ = false;
        drag_ = Drag::None;
        SketchObject* target = widthTarget();
        double px = double(pt.x), py = double(pt.y);
        if (target != nullptr) {
            widthBefore_ = document_->snapshotStyle();
            widthObject_ = target->id();
            std::vector<WidthPoint> widths = target->strokeOptions().widths;
            auto closeTo = [&](const BLPoint& p) {
                return std::fabs(px - p.x) <= kHandleHit + 2.0 && std::fabs(py - p.y) <= kHandleHit + 2.0;
            };
            bool alt = (keys & kmAlt) != 0;
            for (std::size_t i = 0; i < widths.size(); ++i) {
                BLPoint c, l, r;
                if (!widthHandles(*target, widths[i], c, l, r)) {
                    break;
                }
                if (closeTo(c)) {
                    if (alt) {  // Alt-click deletes the point
                        widths.erase(widths.begin() + std::ptrdiff_t(i));
                        if (widths.size() < 2) { widths.clear(); }
                        setWidths(widths);
                        document_->commitStyle(widthBefore_, "Delete Width Point");
                        widthBefore_.clear();
                        return;
                    }
                    drag_ = Drag::Width;
                    widthMode_ = WidthMode::Slide;
                    widthIndex_ = i;
                    widthWhat_ = "Move Width Point";
                    return;
                }
                bool onLeft = closeTo(l), onRight = closeTo(r);
                if (onLeft || onRight) {
                    drag_ = Drag::Width;
                    widthMode_ = !alt ? WidthMode::Both : (onLeft ? WidthMode::Left : WidthMode::Right);
                    widthIndex_ = i;
                    widthWhat_ = "Change Stroke Width";
                    return;
                }
            }
            // Dragging the path itself adds a point where it was grabbed (and the ends, so the rest stays as it was).
            double t = 0.0, distance = 0.0;
            if (target->nearestOnPath(dragStartDoc_, t, distance) && distance * double(zoom_) <= kHandleHit) {
                if (widths.empty()) {
                    widths.push_back({0.0, 1.0, 1.0});
                    widths.push_back({1.0, 1.0, 1.0});
                }
                WidthPoint added{t, widthScaleAt(widths, t, true), widthScaleAt(widths, t, false)};
                auto at = std::upper_bound(widths.begin(), widths.end(), added, [](const WidthPoint& a, const WidthPoint& b) { return a.t < b.t; });
                widthIndex_ = std::size_t(at - widths.begin());
                widths.insert(at, added);
                setWidths(widths);
                drag_ = Drag::Width;
                widthMode_ = WidthMode::Both;
                widthWhat_ = "Add Width Point";
                return;
            }
            widthBefore_.clear();
        }
        // Anything else just selects the object under the pointer.
        SketchObject* hit = document_->hitTest(dragStartDoc_, 4.0 / double(zoom_));
        if (hit != nullptr && !document_->isSelected(hit->id())) {
            document_->setSelection(document_->withGroupMembers({hit->id()}));
        }
    }

    void CanvasView::updateWidthDrag(std::uint32_t keys) {
        SketchObject* target = document_->findById(widthObject_);
        if (target == nullptr || widthIndex_ >= target->strokeOptions().widths.size()) {
            return;
        }
        std::vector<WidthPoint> widths = target->strokeOptions().widths;
        WidthPoint& point = widths[widthIndex_];
        if (widthMode_ == WidthMode::Slide) {
            double t = 0.0, distance = 0.0;
            if (!target->nearestOnPath(dragNowDoc_, t, distance)) {
                return;
            }
            double lo = widthIndex_ > 0 ? widths[widthIndex_ - 1].t + 0.001 : 0.0;
            double hi = widthIndex_ + 1 < widths.size() ? widths[widthIndex_ + 1].t - 0.001 : 1.0;
            point.t = std::min(hi, std::max(lo, t));
        } else {
            BLPoint p, n;
            if (!target->pathSample(point.t, p, n)) {
                return;
            }
            // How far the pointer is out from the path along the normal, in units of half the stroke weight.
            double half = std::max(target->strokeWidth() * 0.5, 1e-6);
            double out = ((dragNowDoc_.x - p.x) * n.x + (dragNowDoc_.y - p.y) * n.y) / half;
            if (widthMode_ == WidthMode::Both) {
                point.left = point.right = std::fabs(out);
            } else if (widthMode_ == WidthMode::Left) {
                point.left = std::max(0.0, out);
            } else {
                point.right = std::max(0.0, -out);
            }
            (void)keys;
        }
        dragMoved_ = true;
        setWidths(widths);
    }

    void CanvasView::finishWidthDrag() {
        if (!widthBefore_.empty()) {
            document_->commitStyle(widthBefore_, widthWhat_);
        }
        widthBefore_.clear();
    }

    void CanvasView::cancelWidthDrag() {
        for (const StyleState& before : widthBefore_) {
            if (SketchObject* object = document_->findById(before.id)) {
                before.applyTo(*object);
            }
        }
        widthBefore_.clear();
        document_->touch();
    }

    void CanvasView::paintWidthOverlay(BLContext& ctx) const {
        const SketchObject* target = widthTarget();
        if (target == nullptr) {
            return;
        }
        const BLRgba32 accent(0xFF1E6FE0u);
        ctx.set_stroke_width(1.0);
        ctx.set_stroke_style(accent);
        for (const WidthPoint& point : target->strokeOptions().widths) {
            BLPoint c, l, r;
            if (!widthHandles(*target, point, c, l, r)) {
                continue;
            }
            BLPath span;
            span.move_to(l.x, l.y);
            span.line_to(r.x, r.y);
            ctx.stroke_path(span);
            ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
            for (const BLPoint& h : {l, r}) {
                ctx.fill_rect(BLRect(h.x - 3.0, h.y - 3.0, 6.0, 6.0));
                ctx.stroke_rect(BLRect(h.x - 3.0, h.y - 3.0, 6.0, 6.0));
            }
            BLPath diamond;
            diamond.move_to(c.x, c.y - 5.0);
            diamond.line_to(c.x + 5.0, c.y);
            diamond.line_to(c.x, c.y + 5.0);
            diamond.line_to(c.x - 5.0, c.y);
            diamond.close();
            ctx.fill_path(diamond);
            ctx.stroke_path(diamond);
        }
    }

    // ---- Gradient tool ----

    SketchObject* CanvasView::gradientTarget() const {
        if (document_ == nullptr || document_->selection().size() != 1) {
            return nullptr;
        }
        SketchObject* object = document_->findById(document_->selection().front());
        return object != nullptr && gradientPaint(*object).isGradient() && !gradientPaint(*object).gradient().stops.empty() ? object : nullptr;
    }

    bool CanvasView::gradientAxisView(const SketchObject& object, BLPoint& start, BLPoint& end) const {
        const SketchPaint& fill = gradientPaint(object);
        if (!fill.isGradient()) {
            return false;
        }
        BLPoint a, b;
        fill.gradient().axisPoints(object.geometryBounds(), fill.kind() == SketchPaint::Kind::Radial, a, b);
        start = toView(a);
        end = toView(b);
        return true;
    }

    BLPoint CanvasView::aspectHandle(const BLPoint& center, const BLPoint& end, double aspect, double skew) {
        double dx = end.x - center.x, dy = end.y - center.y;
        return BLPoint(center.x + (-dy + skew * dx) * aspect, center.y + (dx + skew * dy) * aspect);
    }

    BLPoint CanvasView::tiltHandle(const BLPoint& start, const BLPoint& end, double skew) {
        double dx = end.x - start.x, dy = end.y - start.y, len = std::hypot(dx, dy);
        if (len < 1e-9) {
            return BLPoint((start.x + end.x) * 0.5, (start.y + end.y) * 0.5);
        }
        double ux = dx / len, uy = dy / len;
        double lx = skew * ux - uy, ly = skew * uy + ux;  // skew * along + across
        double ll = std::hypot(lx, ly);
        return BLPoint((start.x + end.x) * 0.5 + lx / ll * 28.0, (start.y + end.y) * 0.5 + ly / ll * 28.0);
    }

    void CanvasView::setGradientOnStroke(bool stroke) {
        if (stroke != gradientOnStroke_) {
            gradientOnStroke_ = stroke;
            style().markDirty();
        }
    }

    void CanvasView::setSelectedGradientStop(std::size_t index) {
        gradientSelectedStop_ = index;
        style().markDirty();
    }

    namespace {
        // `p` moved onto the nearest 45 degree direction from `anchor`, keeping its distance.
        BLPoint snapTo45(const BLPoint& anchor, const BLPoint& p) {
            double step = 3.14159265358979323846 / 4.0;
            double len = std::hypot(p.x - anchor.x, p.y - anchor.y);
            double angle = std::round(std::atan2(p.y - anchor.y, p.x - anchor.x) / step) * step;
            return BLPoint(anchor.x + len * std::cos(angle), anchor.y + len * std::sin(angle));
        }
    }

    void CanvasView::beginGradientDrag(const Point& pt, std::uint32_t keys) {
        dragMoved_ = false;
        gradientBefore_ = document_->snapshotStyle();
        if (SketchObject* target = gradientTarget()) {
            BLPoint a, b;
            gradientAxisView(*target, a, b);
            SketchGradient gradient = gradientPaint(*target).gradient();
            bool radial = gradientPaint(*target).kind() == SketchPaint::Kind::Radial;
            double px = double(pt.x), py = double(pt.y);
            auto closeTo = [&](const BLPoint& p) {
                return std::fabs(px - p.x) <= kHandleHit + 2.0 && std::fabs(py - p.y) <= kHandleHit + 2.0;
            };
            auto at = [&](double t) { return BLPoint(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); };
            // The ends of the axis are handles of their own; every stop strictly between them is draggable.
            for (std::size_t i = 0; i < gradient.stops.size(); ++i) {
                double t = gradient.stops[i].offset;
                if (t <= 0.001 || t >= 0.999 || !closeTo(at(t))) {
                    continue;
                }
                if ((keys & kmAlt) != 0) {
                    if (gradient.stops.size() > 2) {
                        gradient.stops.erase(gradient.stops.begin() + std::ptrdiff_t(i));
                        setGradientPaint(*target, radial ? SketchPaint::radial(gradient) : SketchPaint::linear(gradient));
                        document_->touch();
                        document_->commitStyle(gradientBefore_, "Delete Gradient Stop");
                        setSelectedGradientStop(std::min(gradientSelectedStop_, gradient.stops.size() - 1));
                        onGradientStopSelected(*this);
                    }
                    drag_ = Drag::None;
                    return;
                }
                drag_ = Drag::GradientStop;
                gradientStop_ = i;
                gradientWhat_ = "Move Gradient Stop";
                setSelectedGradientStop(i);
                onGradientStopSelected(*this);
                return;
            }
            if (radial && closeTo(aspectHandle(a, b, gradient.hasAxis ? gradient.axisAspect : 1.0, gradient.axisSkew))) {
                drag_ = Drag::GradientEnd;
                gradientEnd_ = 2;
                gradientWhat_ = "Reshape Gradient";
                return;
            }
            if (!radial && closeTo(tiltHandle(a, b, gradient.hasAxis ? gradient.axisSkew : 0.0))) {
                drag_ = Drag::GradientEnd;
                gradientEnd_ = 3;
                gradientWhat_ = "Tilt Gradient";
                return;
            }
            if (closeTo(a) || closeTo(b)) {
                drag_ = Drag::GradientEnd;
                gradientEnd_ = closeTo(a) ? 0 : 1;
                gradientWhat_ = radial ? (gradientEnd_ == 0 ? "Move Gradient Center" : "Resize Gradient") : "Move Gradient Axis";
                setSelectedGradientStop(gradientEnd_ == 0 ? 0 : gradient.stops.size() - 1);
                onGradientStopSelected(*this);
                return;
            }
            // A click on the axis adds a stop there (and lets it be dragged on).
            double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
            if (len2 > 1.0) {
                double t = ((px - a.x) * dx + (py - a.y) * dy) / len2;
                BLPoint on = at(t);
                if (t > 0.0 && t < 1.0 && std::hypot(px - on.x, py - on.y) <= 4.0) {
                    gradient.stops.push_back({t, gradient.colorAt(t)});
                    gradient.sortStops();
                    for (std::size_t i = 0; i < gradient.stops.size(); ++i) {
                        if (gradient.stops[i].offset == t) {
                            gradientStop_ = i;
                        }
                    }
                    setGradientPaint(*target, radial ? SketchPaint::radial(gradient) : SketchPaint::linear(gradient));
                    document_->touch();
                    drag_ = Drag::GradientStop;
                    gradientWhat_ = "Add Gradient Stop";
                    setSelectedGradientStop(gradientStop_);
                    onGradientStopSelected(*this);
                    return;
                }
            }
        }

        // Otherwise a drag lays a new axis across the selection; clicking an unselected object selects it first.
        SketchObject* hit = document_->hitTest(dragStartDoc_, 4.0 / double(zoom_));
        if (hit != nullptr && !document_->isSelected(hit->id())) {
            document_->setSelection(document_->withGroupMembers({hit->id()}));
        }
        gradientBefore_ = document_->snapshotStyle();
        if (gradientBefore_.empty()) {
            drag_ = Drag::None;
            return;
        }
        drag_ = Drag::GradientNew;
        gradientWhat_ = "Gradient";
    }

    void CanvasView::updateGradientDrag(std::uint32_t keys) {
        bool shift = (keys & kmShift) != 0;
        if (gradientBefore_.empty()) {
            return;
        }
        if (drag_ == Drag::GradientNew) {
            BLPoint a = dragStartDoc_, b = dragNowDoc_;
            if (!dragMoved_ && std::hypot(b.x - a.x, b.y - a.y) * double(zoom_) < 3.0) {
                return;  // a click, not yet a drag
            }
            dragMoved_ = true;
            if (shift) {
                b = snapTo45(a, b);
            }
            for (const StyleState& before : gradientBefore_) {
                SketchObject* object = document_->findById(before.id);
                if (object == nullptr) {
                    continue;
                }
                SketchGradient gradient;
                bool radial = false;
                if (gradientPaint(before).isGradient()) {
                    gradient = gradientPaint(before).gradient();
                    radial = gradientPaint(before).kind() == SketchPaint::Kind::Radial;
                } else {
                    const SketchPaint& fallback = (gradientOnStroke_ ? document_->defaultStroke() : document_->defaultFill());
                    gradient = SketchGradient::fromColor(!gradientPaint(before).isNone() ? gradientPaint(before).color()
                        : (fallback.isNone() ? SketchColor::gray(0.0f) : fallback.color()));
                }
                gradient.setAxis(object->geometryBounds(), a, b);
                setGradientPaint(*object, radial ? SketchPaint::radial(gradient) : SketchPaint::linear(gradient));
            }
            document_->touch();
            return;
        }

        SketchObject* object = document_->findById(gradientBefore_.front().id);
        if (object == nullptr || !gradientPaint(*object).isGradient()) {
            return;
        }
        bool radial = gradientPaint(*object).kind() == SketchPaint::Kind::Radial;
        SketchGradient gradient = gradientPaint(*object).gradient();
        BLBox box = object->geometryBounds();
        BLPoint start, end;
        gradient.axisPoints(box, radial, start, end);
        dragMoved_ = true;
        if (drag_ == Drag::GradientEnd) {
            BLPoint now = dragNowDoc_;
            if (gradientEnd_ == 2 || gradientEnd_ == 3) {
                // The reshaping handles. Work in the axis frame: u along the axis, v across it.
                double length = std::hypot(end.x - start.x, end.y - start.y);
                if (length > 1e-9) {
                    if (!gradient.hasAxis) {
                        gradient.setAxis(box, start, end);  // make the derived axis explicit before reshaping it
                    }
                    double ux = (end.x - start.x) / length, uy = (end.y - start.y) / length;
                    // Radial: the pointer is the across radius' end, measured from the center; linear: from the middle.
                    BLPoint origin = gradientEnd_ == 2 ? start : BLPoint((start.x + end.x) * 0.5, (start.y + end.y) * 0.5);
                    double qu = (now.x - origin.x) * ux + (now.y - origin.y) * uy;
                    double qv = -(now.x - origin.x) * uy + (now.y - origin.y) * ux;
                    if (shift) {
                        gradient.axisSkew = 0.0;
                        if (gradientEnd_ == 2) { gradient.axisAspect = 1.0; }
                    } else if (std::fabs(qv) > 1e-6) {
                        gradient.axisSkew = std::min(10.0, std::max(-10.0, qu / qv));
                        if (gradientEnd_ == 2) {
                            gradient.axisAspect = std::min(8.0, std::max(0.05, std::fabs(qv) / length));
                        }
                    }
                }
            } else if (gradientEnd_ == 0) {
                if (radial) {  // the center carries the radius point along
                    end = BLPoint(end.x + now.x - start.x, end.y + now.y - start.y);
                    start = now;
                } else {
                    start = shift ? snapTo45(end, now) : now;
                }
            } else {
                end = shift && !radial ? snapTo45(start, now) : now;
            }
            if (gradientEnd_ < 2) {
                gradient.setAxis(box, start, end);
            }
        } else if (drag_ == Drag::GradientStop && gradientStop_ < gradient.stops.size()) {
            double dx = end.x - start.x, dy = end.y - start.y, len2 = dx * dx + dy * dy;
            if (len2 < 1e-9) {
                return;
            }
            double t = ((dragNowDoc_.x - start.x) * dx + (dragNowDoc_.y - start.y) * dy) / len2;
            // A stop cannot pass its neighbors (the list stays sorted while it moves).
            double lo = gradientStop_ > 0 ? gradient.stops[gradientStop_ - 1].offset : 0.0;
            double hi = gradientStop_ + 1 < gradient.stops.size() ? gradient.stops[gradientStop_ + 1].offset : 1.0;
            gradient.stops[gradientStop_].offset = std::min(hi, std::max(lo, t));
        }
        setGradientPaint(*object, radial ? SketchPaint::radial(gradient) : SketchPaint::linear(gradient));
        document_->touch();
    }

    void CanvasView::finishGradientDrag() {
        if (!gradientBefore_.empty()) {
            document_->commitStyle(gradientBefore_, gradientWhat_);
        }
        gradientBefore_.clear();
    }

    void CanvasView::cancelGradientDrag() {
        for (const StyleState& before : gradientBefore_) {
            if (SketchObject* object = document_->findById(before.id)) {
                before.applyTo(*object);
            }
        }
        gradientBefore_.clear();
        document_->touch();
    }

    // The target's axis with its stops: the axis end handles are a circle (start / center) and a square (end /
    // radius point); interior stops are diamonds in their own color.
    void CanvasView::paintGradientOverlay(BLContext& ctx) const {
        SketchObject* target = gradientTarget();
        BLPoint a, b;
        if (target == nullptr || !gradientAxisView(*target, a, b)) {
            return;
        }
        const SketchGradient& gradient = gradientPaint(*target).gradient();
        bool radial = gradientPaint(*target).kind() == SketchPaint::Kind::Radial;
        const BLRgba32 accent(0xFF1E6FE0u), white(0xFFFFFFFFu);
        ctx.save();
        if (radial) {
            // The gradient's circle - an ellipse once it has an aspect, turned to follow the axis.
            double radius = std::hypot(b.x - a.x, b.y - a.y);
            BLPath ring;
            ring.add_circle(BLCircle(a.x, a.y, radius));
            ring.transform(gradient.axisMatrix(a, b, true));
            ctx.set_stroke_width(1.0);
            ctx.set_stroke_style(BLRgba32(0x801E6FE0u));
            ctx.stroke_path(ring);
        }
        BLPath line;
        line.move_to(a.x, a.y);
        line.line_to(b.x, b.y);
        ctx.set_stroke_width(3.0);
        ctx.set_stroke_style(white);
        ctx.stroke_path(line);
        ctx.set_stroke_width(1.0);
        ctx.set_stroke_style(accent);
        ctx.stroke_path(line);

        for (std::size_t index = 0; index < gradient.stops.size(); ++index) {
            const GradientStop& stop = gradient.stops[index];
            double t = stop.offset;
            if (t <= 0.001 || t >= 0.999) {
                continue;
            }
            bool selected = index == gradientSelectedStop_;
            double x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t;
            BLPath diamond;
            diamond.move_to(x, y - 5.5);
            diamond.line_to(x + 5.5, y);
            diamond.line_to(x, y + 5.5);
            diamond.line_to(x - 5.5, y);
            diamond.close();
            ctx.set_fill_style(stop.color.toRgba32());
            ctx.fill_path(diamond);
            ctx.set_stroke_width(3.0);
            ctx.set_stroke_style(white);
            ctx.stroke_path(diamond);
            ctx.set_stroke_width(selected ? 2.0 : 1.0);
            ctx.set_stroke_style(selected ? BLRgba32(0xFF000000u) : accent);
            ctx.stroke_path(diamond);
        }
        // The end handles show the color of the stop sitting on them.
        auto endColor = [&](bool first) {
            for (const GradientStop& stop : gradient.stops) {
                if (first ? stop.offset <= 0.001 : stop.offset >= 0.999) {
                    return stop.color.toRgba32();
                }
            }
            return white;
        };
        BLPath circle;
        circle.add_circle(BLCircle(a.x, a.y, 5.5));
        ctx.set_fill_style(white);
        ctx.fill_path(circle);
        ctx.stroke_path(circle);
        ctx.set_fill_style(endColor(true));
        ctx.fill_circle(BLCircle(a.x, a.y, 3.0));
        // A selected end stop is marked by a heavier outline on its handle.
        if (!gradient.stops.empty() && gradientSelectedStop_ == 0) {
            ctx.set_stroke_width(2.0);
            ctx.set_stroke_style(BLRgba32(0xFF000000u));
            ctx.stroke_path(circle);
            ctx.set_stroke_width(1.0);
            ctx.set_stroke_style(accent);
        }
        {   // the reshaping handle: radial = where the across radius ends, linear = a tilt for the equal-color lines
            BLPoint k = radial ? aspectHandle(a, b, gradient.hasAxis ? gradient.axisAspect : 1.0, gradient.axisSkew)
                               : tiltHandle(a, b, gradient.hasAxis ? gradient.axisSkew : 0.0);
            if (!radial) {  // a thin line from the middle of the axis shows the direction the equal-color lines take
                BLPath arm;
                arm.move_to((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
                arm.line_to(k.x, k.y);
                ctx.set_stroke_width(1.0);
                ctx.set_stroke_style(accent);
                ctx.stroke_path(arm);
            }
            BLPath mark;
            mark.move_to(k.x, k.y - 4.5);
            mark.line_to(k.x + 4.5, k.y);
            mark.line_to(k.x, k.y + 4.5);
            mark.line_to(k.x - 4.5, k.y);
            mark.close();
            ctx.set_fill_style(white);
            ctx.fill_path(mark);
            ctx.stroke_path(mark);
        }
        BLRect square(std::floor(b.x) - 5.0, std::floor(b.y) - 5.0, 11.0, 11.0);
        ctx.set_fill_style(white);
        ctx.fill_rect(square);
        ctx.stroke_rect(BLRect(square.x + 0.5, square.y + 0.5, square.w - 1.0, square.h - 1.0));
        ctx.set_fill_style(endColor(false));
        ctx.fill_rect(BLRect(square.x + 3.0, square.y + 3.0, square.w - 6.0, square.h - 6.0));
        if (!gradient.stops.empty() && gradientSelectedStop_ == gradient.stops.size() - 1 && gradientSelectedStop_ != 0) {
            ctx.set_stroke_width(2.0);
            ctx.set_stroke_style(BLRgba32(0xFF000000u));
            ctx.stroke_rect(BLRect(square.x + 0.5, square.y + 0.5, square.w - 1.0, square.h - 1.0));
        }
        ctx.restore();
    }

    // delta follows WM_MOUSEWHEEL (120 per notch, positive = away from the user).
    SyncReturn CanvasView::handleMouseWheel(View& /*sender*/, const Point& pt, float delta) {
        float notches = delta / 120.0f;
        if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            zoomAt(pt, zoom_ * std::pow(1.15f, notches));
        } else if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            setPan(panX_ + notches * 40.0f, panY_);
        } else {
            setPan(panX_, panY_ + notches * 40.0f);
        }
        return SyncReturn::Handled;
    }

    void CanvasView::setPageWidth(float width) {
        if (width <= 0.0f) {
            throw std::runtime_error("CanvasView::setPageWidth: width must be positive");
        }
        pageWidth_ = width;
        style().markDirty();
    }

    void CanvasView::setPageHeight(float height) {
        if (height <= 0.0f) {
            throw std::runtime_error("CanvasView::setPageHeight: height must be positive");
        }
        pageHeight_ = height;
        style().markDirty();
    }

    void CanvasView::setZoom(float zoom) {
        if (zoom <= 0.0f) {
            throw std::runtime_error("CanvasView::setZoom: zoom must be positive");
        }
        zoom_ = zoom;
        style().markDirty();
        onZoomChanged(*this);
    }

    void CanvasView::zoomToFit() {
        if (document_ != nullptr) {  // the page may have been resized by New/Open since the last paint
            pageWidth_ = document_->pageWidth();
            pageHeight_ = document_->pageHeight();
        }
        Rect client = getClientBounds();
        float availW = client.width() - leftInset_ - rulerSize() - 40.0f;
        float availH = client.height() - rulerSize() - 40.0f;
        if (availW <= 0.0f || availH <= 0.0f) {
            return;
        }
        BLBox all = document_ != nullptr ? document_->artboardsBounds() : BLBox(0, 0, pageWidth_, pageHeight_);
        float fitW = availW / float(all.x1 - all.x0);
        float fitH = availH / float(all.y1 - all.y0);
        float fit = fitW < fitH ? fitW : fitH;
        centered_ = true;
        setZoom(fit < kMinZoom ? kMinZoom : (fit > kMaxZoom ? kMaxZoom : fit));
    }

    void CanvasView::fitActiveArtboard() {
        if (document_ == nullptr) {
            return;
        }
        const Artboard& board = document_->activeArtboardRect();
        Rect client = getClientBounds();
        float availW = client.width() - leftInset_ - rulerSize() - 40.0f;
        float availH = client.height() - rulerSize() - 40.0f;
        if (availW <= 0.0f || availH <= 0.0f) {
            return;
        }
        float fit = std::min(availW / float(board.width), availH / float(board.height));
        centered_ = false;
        setZoom(fit < kMinZoom ? kMinZoom : (fit > kMaxZoom ? kMaxZoom : fit));
        float w = client.width() - leftInset_ - rulerSize(), h = client.height() - rulerSize();
        setPan((w - float(board.width) * zoom_) * 0.5f - float(board.x) * zoom_, (h - float(board.height) * zoom_) * 0.5f - float(board.y) * zoom_);
    }

    void CanvasView::setSnapToGrid(bool value) {
        snapToGrid_ = value;
    }

    void CanvasView::setShowGrid(bool value) {
        showGrid_ = value;
        style().markDirty();
    }

    void CanvasView::setShowRulers(bool value) {
        showRulers_ = value;
        style().markDirty();
    }

    void CanvasView::setGridSpacing(float spacing) {
        if (spacing <= 0.0f) {
            throw std::runtime_error("CanvasView::setGridSpacing: spacing must be positive");
        }
        gridSpacing_ = spacing;
        style().markDirty();
    }

    void CanvasView::paint(BLContext& ctx) {
        Rect client = getClientBounds();
        if (client.width() <= 0.0f || client.height() <= 0.0f) {
            return;
        }

        if (document_ != nullptr) {
            pageWidth_ = document_->pageWidth();
            pageHeight_ = document_->pageHeight();
        }
        float rulerSize = this->rulerSize();

        ctx.save();
        ctx.clip_to_rect(BLRect(client.left() + leftInset_ + rulerSize, client.top() + rulerSize,
            client.width() - leftInset_ - rulerSize, client.height() - rulerSize));

        // Pasteboard.
        ctx.set_fill_style(BLRgba32(pasteboardColor_));
        ctx.fill_rect(BLRect(client.left(), client.top(), client.width(), client.height()));

        // Until the user pans or wheel-zooms, the artboards stay centered in the drawing area
        // (vertically only when they fit, else a fixed top margin).
        BLBox all = document_ != nullptr ? document_->artboardsBounds() : BLBox(0, 0, pageWidth_, pageHeight_);
        if (centered_) {
            float availW = client.width() - leftInset_ - rulerSize;
            float availH = client.height() - rulerSize;
            float w = float(all.x1 - all.x0) * zoom_;
            float h = float(all.y1 - all.y0) * zoom_;
            panX_ = (availW - w) * 0.5f - float(all.x0) * zoom_;
            panY_ = (h < availH - 40.0f ? (availH - h) * 0.5f : 20.0f) - float(all.y0) * zoom_;
        }

        // Document (0, 0) sits at the pan offset from the drawing area's corner; each artboard is a page at its own place.
        double pageLeft = client.left() + leftInset_ + rulerSize + double(panX_);
        double pageTop = client.top() + rulerSize + double(panY_);
        Font labelFont = FontManager::getSystemFont(SystemUIFont::Status);
        labelFont.setSize(10.0f);
        const std::vector<Artboard> single{Artboard{"", 0.0, 0.0, double(pageWidth_), double(pageHeight_)}};
        const std::vector<Artboard> none;
        const std::vector<Artboard>& boards = !showPages_ ? none : document_ != nullptr ? document_->artboards() : single;
        for (std::size_t index = 0; index < boards.size(); ++index) {
            const Artboard& board = boards[index];
            double left = pageLeft + board.x * double(zoom_), top = pageTop + board.y * double(zoom_);
            double pageW = board.width * double(zoom_), pageH = board.height * double(zoom_);
            if (document_ != nullptr && !document_->pageBackground()) {
                // Transparent: a light checkerboard, clipped to the page.
                ctx.save();
                ctx.clip_to_rect(BLRect(left, top, pageW, pageH));
                ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
                ctx.fill_rect(BLRect(left, top, pageW, pageH));
                ctx.set_fill_style(BLRgba32(0xFFD9D9D9u));
                const double square = 10.0;
                for (double y = 0.0, row = 0.0; y < pageH; y += square, row += 1.0) {
                    for (double x = (std::fmod(row, 2.0) == 0.0 ? 0.0 : square), col = 0.0; x < pageW; x += 2.0 * square, col += 1.0) {
                        ctx.fill_rect(BLRect(left + x, top + y, square, square));
                    }
                }
                ctx.restore();
            } else {
                ctx.set_fill_style(document_ != nullptr ? document_->pageBackground()->toRgba32() : BLRgba32(0xFFFFFFFFu));
                ctx.fill_rect(BLRect(left, top, pageW, pageH));
            }

            if (showGrid_) {
                double step = double(gridSpacing_) * double(zoom_);
                // Below ~4px a grid is just noise. The lines sit on multiples of the spacing in document units.
                if (step >= 4.0) {
                    BLPath grid;
                    for (double gx = (std::floor(board.x / double(gridSpacing_)) + 1.0) * double(gridSpacing_); gx < board.x + board.width; gx += double(gridSpacing_)) {
                        double px = std::floor(pageLeft + gx * double(zoom_)) + 0.5;
                        grid.move_to(px, top);
                        grid.line_to(px, top + pageH);
                    }
                    for (double gy = (std::floor(board.y / double(gridSpacing_)) + 1.0) * double(gridSpacing_); gy < board.y + board.height; gy += double(gridSpacing_)) {
                        double py = std::floor(pageTop + gy * double(zoom_)) + 0.5;
                        grid.move_to(left, py);
                        grid.line_to(left + pageW, py);
                    }
                    ctx.set_stroke_style(BLRgba32(0xFFE7E7E7u));
                    ctx.set_stroke_width(1.0);
                    ctx.stroke_path(grid);
                }
            }
        }

        // Objects, clipped to the page-and-beyond drawing area; document units are points.
        if (document_ != nullptr) {
            ctx.save();
            ctx.translate(pageLeft, pageTop);
            ctx.scale(double(zoom_));
            document_->paint(ctx);
            ctx.restore();
            paintOverlay(ctx);
        }

        // Outlines (the active artboard's is stronger when there are several) and the names above them.
        BLFont* nameFont = labelFont.blFont();
        for (std::size_t index = 0; index < boards.size(); ++index) {
            const Artboard& board = boards[index];
            double left = pageLeft + board.x * double(zoom_), top = pageTop + board.y * double(zoom_);
            bool active = document_ != nullptr && boards.size() > 1 && index == document_->activeArtboard();
            ctx.set_stroke_style(active ? BLRgba32(0xFF1E6FE0u) : BLRgba32(0x59000000u));
            ctx.set_stroke_width(1.0);
            ctx.stroke_rect(BLRect(left - 0.5, top - 0.5, board.width * double(zoom_) + 1.0, board.height * double(zoom_) + 1.0));
            if (boards.size() > 1 && nameFont != nullptr && nameFont->is_valid() && top - 14.0 > client.top() + rulerSize) {
                ctx.set_fill_style(active ? BLRgba32(0xFFFFFFFFu) : BLRgba32(0xFFD8D8D8u));
                ctx.fill_utf8_text(BLPoint(left, top - 5.0), *nameFont, board.name.c_str(), board.name.size());
            }
        }
        ctx.restore();

        if (showRulers_) {
            paintRulers(ctx, client);
        }
        if (onPaintTop) {
            onPaintTop(ctx);
        }
    }

    // Tick labels every 100 document units at the current zoom - enough for the scaffold; the
    // real ruler (adaptive tick spacing, cursor marker, guide dragging) comes with S7.
    void CanvasView::paintRulers(BLContext& ctx, const Rect& client) const {
        BLRgba32 face = UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32();
        BLRgba32 text = UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32();
        BLRgba32 edge = UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32();

        ctx.save();
        ctx.set_fill_style(face);
        const double left = client.left() + leftInset_;
        ctx.fill_rect(BLRect(left, client.top(), client.width() - leftInset_, kRulerThickness));
        ctx.fill_rect(BLRect(left, client.top(), kRulerThickness, client.height()));
        ctx.set_stroke_style(edge);
        ctx.set_stroke_width(1.0);
        BLPath edges;
        edges.move_to(left, client.top() + kRulerThickness - 0.5);
        edges.line_to(client.left() + client.width(), client.top() + kRulerThickness - 0.5);
        edges.move_to(left + kRulerThickness - 0.5, client.top());
        edges.line_to(left + kRulerThickness - 0.5, client.top() + client.height());
        ctx.stroke_path(edges);

        Font font = FontManager::getSystemFont(SystemUIFont::Status);
        font.setSize(9.0f);  // rulers are chrome, not content - keep the numerals small
        BLFont* blFont = font.blFont();
        double originX = left + kRulerThickness + double(panX_);
        double originY = client.top() + kRulerThickness + double(panY_);
        double step = 100.0 * double(zoom_);
        if (blFont != nullptr && blFont->is_valid() && step >= 20.0) {
            ctx.set_fill_style(text);
            BLPath ticks;
            int firstX = int(std::ceil((left + kRulerThickness - originX) / step));
            for (int i = firstX; ; ++i) {
                double x = originX + double(i) * step;
                if (x > client.left() + client.width()) {
                    break;
                }
                std::string label = std::to_string(i * 100);
                ticks.move_to(std::floor(x) + 0.5, client.top() + 12.0);
                ticks.line_to(std::floor(x) + 0.5, client.top() + kRulerThickness);
                ctx.fill_utf8_text(BLPoint(x + 3.0, client.top() + 11.0), *blFont, label.c_str(), label.size());
            }
            int firstY = int(std::ceil((client.top() + kRulerThickness - originY) / step));
            for (int i = firstY; ; ++i) {
                double y = originY + double(i) * step;
                if (y > client.top() + client.height()) {
                    break;
                }
                std::string label = std::to_string(i * 100);
                ticks.move_to(left + 12.0, std::floor(y) + 0.5);
                ticks.line_to(left + kRulerThickness, std::floor(y) + 0.5);
                ctx.fill_utf8_text(BLPoint(left + 2.0, y + 11.0), *blFont, label.c_str(), label.size());
            }
            ctx.set_stroke_style(edge);
            ctx.stroke_path(ticks);
        }
        ctx.restore();
    }

}
