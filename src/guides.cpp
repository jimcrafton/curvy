#include "curvy/canvasview.h"

#include <algorithm>
#include <cmath>
#include <limits>

// Ruler guides and snapping on the canvas. Guides are made by dragging out of a ruler, moved by dragging them (Select tool) and removed by
// dragging them back onto a ruler. Drawing, moving, resizing and pen points snap to the grid (when on), to the guides, and - Smart
// Guides - to the edges and centers of the other objects, which shows a magenta line where it caught.

namespace curvy {

    using namespace newui;

    namespace {
        constexpr double kGuideHit = 4.0;      // view pixels either side of a guide
        constexpr double kSnapPixels = 6.0;    // how close, in view pixels, a line has to be to catch
        constexpr std::size_t kMaxObjectsForSmartGuides = 3000;   // beyond this only the guides are used (collecting is a pass over every object)
        const double kNone = std::numeric_limits<double>::quiet_NaN();
    }

    void CanvasView::setShowGuides(bool value) {
        showGuides_ = value;
        style().markDirty();
    }

    void CanvasView::setGuidesLocked(bool value) {
        guidesLocked_ = value;
    }

    void CanvasView::setSmartGuides(bool value) {
        smartGuides_ = value;
        snapCacheValid_ = false;
    }

    // ---- the lines things can snap to ----

    // Every guide, and (with Smart Guides) the left / center / right and top / middle / bottom of every other object. While objects are
    // being moved or resized the selected ones are left out - they are what is moving.
    void CanvasView::collectSnapLines() const {
        snapX_.clear();
        snapY_.clear();
        if (document_ != nullptr) {
            if (showGuides_) {
                for (const Guide& guide : document_->guides()) {
                    (guide.vertical ? snapX_ : snapY_).push_back(SnapLine{guide.position, true});
                }
            }
            if (smartGuides_ && document_->objectCount() <= kMaxObjectsForSmartGuides) {
                const bool leaveOutSelected = drag_ == Drag::Move || drag_ == Drag::Resize || drag_ == Drag::Rotate;
                for (std::size_t i = 0; i < document_->objectCount(); ++i) {
                    const SketchObject& object = document_->object(i);
                    if (!object.isVisible() || (leaveOutSelected && document_->isSelected(object.id()))) {
                        continue;
                    }
                    BLBox box = object.geometryBounds();
                    if (!(box.x1 >= box.x0) || !std::isfinite(box.x0) || !std::isfinite(box.x1)) {
                        continue;
                    }
                    for (double x : {box.x0, (box.x0 + box.x1) * 0.5, box.x1}) { snapX_.push_back(SnapLine{x, false}); }
                    for (double y : {box.y0, (box.y0 + box.y1) * 0.5, box.y1}) { snapY_.push_back(SnapLine{y, false}); }
                }
            }
        }
        snapCacheValid_ = true;
    }

    // `value` (a document x for axis 0, a y for axis 1) moved onto the nearest grid line, guide or object line within a few pixels. `line`,
    // when given, receives the object line it caught (what the magenta line is drawn at), or NaN when it caught none.
    double CanvasView::snapAxis(int axis, double value, double* line) const {
        const double reach = kSnapPixels / double(zoom_);
        double best = value, bestGap = reach;
        double caught = kNone;
        if (snapToGrid_ && gridSpacing_ > 0.0f) {
            double spacing = double(gridSpacing_);
            double nearest = std::round(value / spacing) * spacing;
            if (std::fabs(nearest - value) <= bestGap) {
                best = nearest;
                bestGap = std::fabs(nearest - value);
            }
        }
        if (!snapCacheValid_) {
            collectSnapLines();
        }
        for (const SnapLine& candidate : axis == 0 ? snapX_ : snapY_) {
            double gap = std::fabs(candidate.position - value);
            if (gap < bestGap) {
                best = candidate.position;
                bestGap = gap;
                caught = candidate.guide ? kNone : candidate.position;
            }
        }
        if (line != nullptr) {
            *line = caught;
        }
        return best;
    }

    double CanvasView::snapValue(double value, int axis) const {
        return snapAxis(axis, value, nullptr);
    }

    // A point snapped on both axes; the object lines it caught are remembered for the overlay.
    BLPoint CanvasView::snapPoint(const BLPoint& point) const {
        double lx = kNone, ly = kNone;
        BLPoint snapped(snapAxis(0, point.x, &lx), snapAxis(1, point.y, &ly));
        rememberSnapLines(lx, ly);
        return snapped;
    }

    void CanvasView::rememberSnapLines(double x, double y) const {
        smartX_.clear();
        smartY_.clear();
        if (!std::isnan(x)) { smartX_.push_back(x); }
        if (!std::isnan(y)) { smartY_.push_back(y); }
    }

    // ---- making, moving and deleting guides ----

    int CanvasView::guideAt(const Point& pt) const {
        if (document_ == nullptr || !showGuides_) {
            return -1;
        }
        int found = -1;
        double bestGap = kGuideHit;
        BLPoint doc = toDocument(pt);
        for (std::size_t i = 0; i < document_->guides().size(); ++i) {
            const Guide& guide = document_->guides()[i];
            double gap = std::fabs((guide.vertical ? doc.x : doc.y) - guide.position) * double(zoom_);
            if (gap <= bestGap) {
                bestGap = gap;
                found = int(i);
            }
        }
        return found;
    }

    // A press that starts a guide: in a ruler (a new guide - the top ruler gives a horizontal one) or on an existing guide.
    bool CanvasView::beginGuideDrag(const Point& pt) {
        if (document_ == nullptr) {
            return false;
        }
        const Rect client = getClientBounds();
        const double rs = rulerSize();
        const double left = double(pt.x) - double(client.left()), top = double(pt.y) - double(client.top());
        if (showRulers_ && (left < rs || top < rs) && !(left < rs && top < rs)) {
            guideDragIndex_ = -1;
            guideDragVertical_ = left < rs;   // the left ruler gives a vertical guide
        } else {
            int hit = (tool_ == SketchTool::Select && !guidesLocked_ && hitHandle(pt) < 0) ? guideAt(pt) : -1;
            if (hit < 0) {
                return false;
            }
            guideDragIndex_ = hit;
            guideDragVertical_ = document_->guides()[std::size_t(hit)].vertical;
        }
        drag_ = Drag::Guide;
        updateGuideDrag(pt);
        return true;
    }

    void CanvasView::updateGuideDrag(const Point& pt) {
        BLPoint doc = toDocument(pt);
        double position = guideDragVertical_ ? doc.x : doc.y;
        guideDragPosition_ = snapAxis(guideDragVertical_ ? 0 : 1, position, nullptr);
        style().markDirty();
    }

    // Let go: inside the drawing area the guide is made / moved; back on a ruler a new guide is dropped and an old one deleted.
    void CanvasView::finishGuideDrag(const Point& pt) {
        const Rect client = getClientBounds();
        const double rs = rulerSize();
        const bool inside = double(pt.x) - double(client.left()) >= rs && double(pt.y) - double(client.top()) >= rs;
        if (guideDragIndex_ < 0) {
            if (inside) {
                document_->addGuide(guideDragVertical_, guideDragPosition_);
            }
        } else if (inside) {
            document_->moveGuide(std::size_t(guideDragIndex_), guideDragPosition_);
        } else {
            document_->removeGuide(std::size_t(guideDragIndex_));
        }
        guideDragIndex_ = -1;
        style().markDirty();
    }

    // ---- painting ----

    void CanvasView::paintGuides(BLContext& ctx) const {
        if (document_ == nullptr) {
            return;
        }
        const Rect client = getClientBounds();
        const double rs = rulerSize();
        const double x0 = double(client.left()) + rs, y0 = double(client.top()) + rs, x1 = double(client.left() + client.width()), y1 = double(client.top() + client.height());
        ctx.save();
        ctx.clip_to_rect(BLRect(x0, y0, x1 - x0, y1 - y0));
        ctx.set_stroke_width(1.0);
        auto line = [&](bool vertical, double position, BLRgba32 color) {
            BLPoint at = toView(vertical ? BLPoint(position, 0.0) : BLPoint(0.0, position));
            BLPath path;
            if (vertical) {
                double px = std::floor(at.x) + 0.5;
                path.move_to(px, y0);
                path.line_to(px, y1);
            } else {
                double py = std::floor(at.y) + 0.5;
                path.move_to(x0, py);
                path.line_to(x1, py);
            }
            ctx.set_stroke_style(color);
            ctx.stroke_path(path);
        };
        if (showGuides_) {
            for (std::size_t i = 0; i < document_->guides().size(); ++i) {
                const Guide& guide = document_->guides()[i];
                if (drag_ == Drag::Guide && int(i) == guideDragIndex_) {
                    continue;   // being dragged: drawn at its new place below
                }
                line(guide.vertical, guide.position, BLRgba32(0xFF1FB5E6u));
            }
        }
        if (drag_ == Drag::Guide) {
            line(guideDragVertical_, guideDragPosition_, BLRgba32(0xFF1FB5E6u));
        }
        // Where Smart Guides caught: magenta lines (only while a drag is on).
        if (drag_ != Drag::None) {
            for (double x : smartX_) { line(true, x, BLRgba32(0xFFFF2D95u)); }
            for (double y : smartY_) { line(false, y, BLRgba32(0xFFFF2D95u)); }
        }
        ctx.restore();
    }

}
