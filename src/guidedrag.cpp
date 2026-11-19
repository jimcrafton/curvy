#include "curvy/guidedrag.h"

#include <cmath>
#include <stdexcept>

namespace {
    constexpr float kClickSlop = 4.0f;   // pixels (x + y) a press may move and still be a click
}

namespace curvy {

    GuideDrag::GuideDrag(SketchDocument& document, CanvasView& canvas) : document_(document), canvas_(canvas) {
    }

    bool GuideDrag::begin(const newui::Point& viewPoint) {
        active_ = false;
        const Rulers::Region region = canvas_.rulerAt(viewPoint);
        origin_ = false;
        pressedAt_ = viewPoint;
        if (region == Rulers::Region::Corner) {
            origin_ = true;
            active_ = true;
            follow(viewPoint);
            return true;
        }
        if (region != Rulers::Region::None) {
            // The top ruler makes a horizontal guide (a y), the left one a vertical guide (an x).
            guide_ = Guide{region == Rulers::Region::Left, 0.0};
            index_.reset();
        } else {
            const std::optional<std::size_t> hit = canvas_.guideAt(viewPoint);
            if (!hit || document_.guidesLocked()) {
                return false;
            }
            guide_ = document_.guides()[*hit];
            index_ = hit;
        }
        active_ = true;
        follow(viewPoint);
        return true;
    }

    void GuideDrag::follow(const newui::Point& viewPoint) {
        const BLPoint at = canvas_.toDocument(viewPoint);
        if (origin_) {
            if (std::abs(viewPoint.x - pressedAt_.x) + std::abs(viewPoint.y - pressedAt_.y) >= kClickSlop) {
                canvas_.setOriginPreview(at);
            }
            return;
        }
        guide_.position = guide_.vertical ? at.x : at.y;
        CanvasView::GuidePreview preview;
        preview.replaces = index_;
        preview.guide = guide_;
        canvas_.setGuidePreview(preview);
    }

    bool GuideDrag::move(const newui::Point& viewPoint) {
        if (!active_) {
            return false;
        }
        follow(viewPoint);
        return true;
    }

    bool GuideDrag::overOwnRuler(const newui::Point& viewPoint) const {
        const Rulers::Region region = canvas_.rulerAt(viewPoint);
        return region == (guide_.vertical ? Rulers::Region::Left : Rulers::Region::Top);
    }

    bool GuideDrag::end(const newui::Point& viewPoint) {
        if (!active_) {
            return false;
        }
        follow(viewPoint);
        active_ = false;
        canvas_.setGuidePreview(std::nullopt);
        if (origin_) {
            origin_ = false;
            canvas_.setOriginPreview(std::nullopt);
            if (std::abs(viewPoint.x - pressedAt_.x) + std::abs(viewPoint.y - pressedAt_.y) < kClickSlop) {
                if (onCornerClick) {
                    onCornerClick();
                }
            } else if (canvas_.rulerAt(viewPoint) == Rulers::Region::None) {
                const BLPoint at = canvas_.toDocument(viewPoint);
                try {
                    document_.setRulerOrigin(RulerOrigin{at.x, at.y});
                } catch (const std::runtime_error&) {
                    // not a finite point: the origin stays
                }
            }
            return true;
        }
        try {
            if (index_) {
                if (overOwnRuler(viewPoint)) {
                    document_.removeGuide(*index_);
                } else {
                    document_.moveGuide(*index_, guide_.position);
                }
            } else if (!overOwnRuler(viewPoint)) {
                document_.setGuidesVisible(true);   // pulling one out of a ruler shows the guides, as in Illustrator
                document_.addGuide(guide_.vertical, guide_.position);
            }
        } catch (const std::runtime_error&) {
            // refused (a locked set or a position that is not a number): the drag simply ends
        }
        index_.reset();
        return true;
    }

    void GuideDrag::cancel() {
        if (active_) {
            active_ = false;
            origin_ = false;
            index_.reset();
            canvas_.setGuidePreview(std::nullopt);
            canvas_.setOriginPreview(std::nullopt);
        }
    }

}
