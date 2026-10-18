#include "curvy/sketchdocument.h"

#include <algorithm>
#include <cmath>

// Artboards: named rectangles on the canvas. The active one is the page for Align, Image Trace and export.

namespace curvy {

    void SketchDocument::setActiveArtboard(std::size_t index) {
        if (index >= artboards_.size() || index == activeArtboard_) {
            return;
        }
        activeArtboard_ = index;
        updateAllViews();   // not an edit (the document stays unmodified); the page size the panels show follows
        onChanged(*this);
    }

    BLBox SketchDocument::artboardsBounds() const {
        BLBox out = artboards_.front().box();
        for (const Artboard& board : artboards_) {
            BLBox b = board.box();
            out = BLBox(std::min(out.x0, b.x0), std::min(out.y0, b.y0), std::max(out.x1, b.x1), std::max(out.y1, b.y1));
        }
        return out;
    }

    int SketchDocument::artboardAt(const BLPoint& point) const {
        for (std::size_t i = artboards_.size(); i > 0; --i) {
            if (artboards_[i - 1].contains(point)) {
                return int(i - 1);
            }
        }
        return -1;
    }

    std::string SketchDocument::nextArtboardName() const {
        for (std::size_t n = artboards_.size() + 1;; ++n) {
            std::string name = "Artboard " + std::to_string(n);
            bool taken = false;
            for (const Artboard& board : artboards_) {
                taken = taken || board.name == name;
            }
            if (!taken) {
                return name;
            }
        }
    }

    namespace {
        struct ArtboardState {
            std::vector<Artboard> boards;
            std::size_t active = 0;
        };
    }

    std::size_t SketchDocument::addArtboard(Artboard artboard) {
        artboard.width = std::max(1.0, artboard.width);
        artboard.height = std::max(1.0, artboard.height);
        if (artboard.name.empty()) {
            artboard.name = nextArtboardName();
        }
        ArtboardState before{artboards_, activeArtboard_}, after = before;
        after.boards.push_back(artboard);
        after.active = after.boards.size() - 1;
        auto apply = [this](const ArtboardState& state) { setArtboards(state.boards, state.active); };
        undoStack_.push({"Add Artboard", [apply, after]() { apply(after); }, [apply, before]() { apply(before); }});
        return after.active;
    }

    std::size_t SketchDocument::addArtboardBesideLast() {
        Artboard board = artboards_[activeArtboard_];
        board.name.clear();
        board.x = artboardsBounds().x1 + 40.0;
        board.y = artboards_[activeArtboard_].y;
        return addArtboard(board);
    }

    bool SketchDocument::deleteArtboard(std::size_t index) {
        if (artboards_.size() < 2 || index >= artboards_.size()) {
            return false;
        }
        ArtboardState before{artboards_, activeArtboard_}, after = before;
        after.boards.erase(after.boards.begin() + std::ptrdiff_t(index));
        after.active = std::min(index, after.boards.size() - 1);
        auto apply = [this](const ArtboardState& state) { setArtboards(state.boards, state.active); };
        undoStack_.push({"Delete Artboard", [apply, after]() { apply(after); }, [apply, before]() { apply(before); }});
        return true;
    }

    bool SketchDocument::setArtboard(std::size_t index, const Artboard& artboard) {
        if (index >= artboards_.size()) {
            return false;
        }
        Artboard fixed = artboard;
        fixed.width = std::max(1.0, fixed.width);
        fixed.height = std::max(1.0, fixed.height);
        if (fixed.name.empty()) {
            fixed.name = artboards_[index].name;
        }
        if (fixed == artboards_[index]) {
            return false;
        }
        ArtboardState before{artboards_, activeArtboard_}, after = before;
        after.boards[index] = fixed;
        auto apply = [this](const ArtboardState& state) { setArtboards(state.boards, state.active); };
        undoStack_.push({"Edit Artboard", [apply, after]() { apply(after); }, [apply, before]() { apply(before); }});
        return true;
    }

    void SketchDocument::setArtboards(std::vector<Artboard> artboards, std::size_t active) {
        if (artboards.empty()) {
            return;
        }
        artboards_ = std::move(artboards);
        activeArtboard_ = std::min(active, artboards_.size() - 1);
        changed();
    }

}
