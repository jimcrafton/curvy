#include "curvy/sketchdocument.h"

#include <cmath>

// Ruler guides: straight lines across the page that objects snap to (and that never print). Each change is one undo step.

namespace curvy {

    namespace {
        bool sane(double position) {
            return std::isfinite(position);
        }
    }

    void SketchDocument::setGuides(std::vector<Guide> guides) {
        guides_ = std::move(guides);
        updateAllViews();
    }

    std::size_t SketchDocument::addGuide(bool vertical, double position) {
        if (!sane(position)) {
            return guides_.size();
        }
        std::size_t index = guides_.size();
        Guide guide{vertical, position};
        undoStack_.push({"Add Guide",
            [this, guide]() { guides_.push_back(guide); changed(); },
            [this]() { guides_.pop_back(); changed(); }});
        return index;
    }

    bool SketchDocument::moveGuide(std::size_t index, double position) {
        if (index >= guides_.size() || !sane(position) || guides_[index].position == position) {
            return false;
        }
        double before = guides_[index].position;
        undoStack_.push({"Move Guide",
            [this, index, position]() { if (index < guides_.size()) { guides_[index].position = position; } changed(); },
            [this, index, before]() { if (index < guides_.size()) { guides_[index].position = before; } changed(); }});
        return true;
    }

    bool SketchDocument::removeGuide(std::size_t index) {
        if (index >= guides_.size()) {
            return false;
        }
        Guide removed = guides_[index];
        undoStack_.push({"Delete Guide",
            [this, index]() { if (index < guides_.size()) { guides_.erase(guides_.begin() + std::ptrdiff_t(index)); } changed(); },
            [this, index, removed]() { guides_.insert(guides_.begin() + std::ptrdiff_t(std::min(index, guides_.size())), removed); changed(); }});
        return true;
    }

    bool SketchDocument::clearGuides() {
        if (guides_.empty()) {
            return false;
        }
        std::vector<Guide> before = guides_;
        undoStack_.push({"Clear Guides",
            [this]() { guides_.clear(); changed(); },
            [this, before]() { guides_ = before; changed(); }});
        return true;
    }

}
