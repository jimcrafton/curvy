#include "curvy/sketchdocument.h"

#include "curvy/pathflatten.h"

#include <algorithm>
#include <cmath>

// Text on a path and text between two paths (see TextWarp in sketchobject.h).

namespace curvy {

    namespace {
        // The path as a polyline path, running the other way (curves become fine straight pieces).
        BLPath reversed(const BLPath& path) {
            std::vector<MeasuredPath> all = measurePath(path);
            BLPath out;
            if (all.empty()) {
                return path;
            }
            MeasuredPath r = reversedPath(all.front());
            out.move_to(r.points.front());
            for (std::size_t i = 1; i < r.points.size(); ++i) {
                out.line_to(r.points[i]);
            }
            return out;
        }

        BLPoint endOf(const BLPath& path, bool last) {
            std::vector<MeasuredPath> all = measurePath(path);
            if (all.empty()) {
                return BLPoint(0, 0);
            }
            return last ? all.front().points.back() : all.front().points.front();
        }
    }

    // One text and `paths` other objects with an outline; fills them in selection order.
    static bool pickTextAndPaths(const SketchDocument& document, std::size_t paths, const SketchObject*& text, std::vector<const SketchObject*>& out) {
        text = nullptr;
        out.clear();
        for (std::size_t id : document.selection()) {
            const SketchObject* object = document.findById(id);
            if (object == nullptr) {
                return false;
            }
            if (object->isText()) {
                if (text != nullptr) {
                    return false;
                }
                text = object;
            } else if (!object->isSymbol() && object->geometry().size() > 1) {
                out.push_back(object);
            } else {
                return false;
            }
        }
        return text != nullptr && out.size() == paths;
    }

    bool SketchDocument::canMakeTextOnPath() const {
        const SketchObject* text = nullptr;
        std::vector<const SketchObject*> paths;
        return pickTextAndPaths(*this, 1, text, paths) && !measurePath(paths[0]->geometry()).empty();
    }

    bool SketchDocument::canMakeTextBetweenPaths() const {
        const SketchObject* text = nullptr;
        std::vector<const SketchObject*> paths;
        return pickTextAndPaths(*this, 2, text, paths) && !measurePath(paths[0]->geometry()).empty() && !measurePath(paths[1]->geometry()).empty();
    }

    const SketchObject* SketchDocument::selectedWarpedText() const {
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object != nullptr && object->isText() && object->textWarp().active()) {
                return object;
            }
        }
        return nullptr;
    }

    bool SketchDocument::canReleaseTextWarp() const {
        return selectedWarpedText() != nullptr;
    }

    // Puts `after` on the texts as one undo step; the paths of a new warp are in the page's space because the text is then placed by the identity.
    void SketchDocument::applyTextWarp(const std::vector<TextWarpState>& before, const std::vector<TextWarpState>& after, const std::string& description) {
        auto apply = [this](const std::vector<TextWarpState>& states) {
            for (const TextWarpState& state : states) {
                if (SketchObject* object = findById(state.id)) {
                    object->setTextMatrix(state.matrix);
                    object->setTextWarp(state.warp);
                }
            }
            changed();
        };
        undoStack_.push({description, [apply, after]() { apply(after); }, [apply, before]() { apply(before); }});
    }

    bool SketchDocument::makeTextOnPath() {
        const SketchObject* text = nullptr;
        std::vector<const SketchObject*> paths;
        if (!pickTextAndPaths(*this, 1, text, paths) || measurePath(paths[0]->geometry()).empty()) {
            return false;
        }
        TextWarpState before{text->id(), text->textWarp(), text->textMatrix()}, after = before;
        after.warp = TextWarp();
        after.warp.kind = TextWarp::Kind::OnPath;
        after.warp.first = paths[0]->geometry();
        after.matrix = BLMatrix2D::make_identity();   // the path is in page coordinates: the flat space of the text is the page
        applyTextWarp({before}, {after}, "Text on a Path");
        setSelection({text->id()});
        return true;
    }

    bool SketchDocument::makeTextBetweenPaths() {
        const SketchObject* text = nullptr;
        std::vector<const SketchObject*> paths;
        if (!pickTextAndPaths(*this, 2, text, paths) || measurePath(paths[0]->geometry()).empty() || measurePath(paths[1]->geometry()).empty()) {
            return false;
        }
        BLPath top = paths[0]->geometry(), bottom = paths[1]->geometry();
        const BLBox a = paths[0]->geometryBounds(), b = paths[1]->geometryBounds();
        if ((b.y0 + b.y1) < (a.y0 + a.y1)) {
            std::swap(top, bottom);   // the one higher up the page is the top
        }
        if (endOf(top, true).x < endOf(top, false).x) {
            top = reversed(top);      // the text reads from left to right
        }
        const BLPoint t0 = endOf(top, false), t1 = endOf(top, true), b0 = endOf(bottom, false), b1 = endOf(bottom, true);
        if ((t1.x - t0.x) * (b1.x - b0.x) + (t1.y - t0.y) * (b1.y - b0.y) < 0.0) {
            bottom = reversed(bottom);   // both run the same way
        }
        TextWarpState before{text->id(), text->textWarp(), text->textMatrix()}, after = before;
        after.warp = TextWarp();
        after.warp.kind = TextWarp::Kind::Between;
        after.warp.first = top;
        after.warp.second = bottom;
        after.matrix = BLMatrix2D::make_identity();
        applyTextWarp({before}, {after}, "Text Between Paths");
        setSelection({text->id()});
        return true;
    }

    bool SketchDocument::releaseTextWarp() {
        std::vector<TextWarpState> before, after;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr || !object->isText() || !object->textWarp().active()) {
                continue;
            }
            TextWarpState was{id, object->textWarp(), object->textMatrix()}, now = was;
            // Plain text starts where the warped text starts (the text's own origin, for text between paths the left edge of its top).
            const double x0 = was.warp.kind == TextWarp::Kind::Between ? object->warpFrameLeft() : 0.0;
            const BLPoint at = object->mapTextPoint(x0, 0.0);
            now.warp = TextWarp();
            now.matrix = BLMatrix2D::make_translation(at.x - x0, at.y);
            before.push_back(was);
            after.push_back(now);
        }
        if (before.empty()) {
            return false;
        }
        applyTextWarp(before, after, "Release Text from Path");
        return true;
    }

    bool SketchDocument::setTextPathOptions(double startPercent, bool flip) {
        if (!std::isfinite(startPercent)) {
            return false;
        }
        std::vector<TextWarpState> before, after;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr || !object->isText() || !object->textWarp().active()) {
                continue;
            }
            TextWarpState was{id, object->textWarp(), object->textMatrix()}, now = was;
            now.warp.flip = flip;
            if (was.warp.kind == TextWarp::Kind::OnPath) {
                now.warp.start = std::max(-100.0, std::min(200.0, startPercent));
            }
            if (!(now.warp == was.warp)) {
                before.push_back(was);
                after.push_back(now);
            }
        }
        if (before.empty()) {
            return false;
        }
        applyTextWarp(before, after, "Text Path Options");
        return true;
    }

}
