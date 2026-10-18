#include "curvy/sketchdocument.h"

#include "curvy/pathops.h"
#include "curvy/shapetools.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

// The document's cutting and combining operations: Pathfinder, the Knife, the Eraser and the Scissors. Each one turns some objects
// into others as a single undo step, through replaceUndoable().

namespace curvy {

    namespace {

        // std::function needs a copyable callable, so objects that undo / redo hand back and forth live in shared slots.
        struct Slot {
            std::unique_ptr<SketchObject> object;
            std::size_t id = 0;
            std::size_t index = 0;   // the stacking position the object had when it was last taken out
        };

        struct Edit {
            std::vector<std::shared_ptr<Slot>> removed;
            std::vector<std::shared_ptr<Slot>> added;
            bool atTop = false;
        };

        // What the cutting tools may touch: shown, not locked, and not text (which stays editable text).
        bool workable(const SketchObject& object) {
            return object.isVisible() && !object.isLocked() && !object.isText();
        }

        // True when some visible fill of the object paints something (a shape with only a stroke is treated as a line).
        bool paintsFill(const SketchObject& object) {
            if (!object.isClosed()) {
                return false;
            }
            for (const AppearanceItem& item : object.appearance()) {
                if (item.kind == AppearanceKind::Fill && item.visible && !item.paint.isNone()) {
                    return true;
                }
            }
            return false;
        }

        bool boxesOverlap(const BLBox& a, const BLBox& b) {
            return a.x0 <= b.x1 && a.x1 >= b.x0 && a.y0 <= b.y1 && a.y1 >= b.y0;
        }

        BLBox boxOfLoops(const Loops& loops) {
            BLBox box(1e300, 1e300, -1e300, -1e300);
            for (const Loop& loop : loops) {
                for (const BLPoint& p : loop) {
                    box.x0 = std::min(box.x0, p.x);
                    box.y0 = std::min(box.y0, p.y);
                    box.x1 = std::max(box.x1, p.x);
                    box.y1 = std::max(box.y1, p.y);
                }
            }
            return box;
        }

        BLPath linePath(const std::vector<BLPoint>& points) {
            BLPath path;
            if (points.empty()) {
                return path;
            }
            path.move_to(points[0]);
            for (std::size_t i = 1; i < points.size(); ++i) {
                path.line_to(points[i]);
            }
            return path;
        }
    }

    std::vector<std::size_t> SketchDocument::replaceUndoable(std::vector<Replacement> replacements, const std::string& description) {
        auto edits = std::make_shared<std::vector<Edit>>();
        std::vector<std::size_t> addedIds, removedIds;
        for (Replacement& replacement : replacements) {
            Edit edit;
            edit.atTop = replacement.atTop;
            for (std::size_t id : replacement.remove) {
                auto slot = std::make_shared<Slot>();
                slot->id = id;
                edit.removed.push_back(slot);
                removedIds.push_back(id);
            }
            for (std::unique_ptr<SketchObject>& object : replacement.add) {
                auto slot = std::make_shared<Slot>();
                slot->object = std::move(object);
                slot->object->setId(nextId_++);
                slot->id = slot->object->id();
                edit.added.push_back(slot);
                addedIds.push_back(slot->id);
            }
            edits->push_back(std::move(edit));
        }
        if (edits->empty()) {
            return addedIds;
        }
        undoStack_.push({description,
            [this, edits, addedIds]() {
                for (Edit& edit : *edits) {
                    // Take the removed ones out, highest stacking position first so every recorded index is the original one.
                    std::size_t lowest = std::numeric_limits<std::size_t>::max(), highest = 0, taken = 0;
                    for (std::size_t i = objects_.size(); i > 0; --i) {
                        std::size_t id = objects_[i - 1]->id();
                        for (const std::shared_ptr<Slot>& slot : edit.removed) {
                            if (slot->id == id) {
                                slot->object = detach(id, &slot->index);
                                lowest = std::min(lowest, slot->index);
                                highest = std::max(highest, slot->index);
                                ++taken;
                                break;
                            }
                        }
                    }
                    // The new objects go where they were: at the lowest removed position, or the place the highest one left.
                    std::size_t at = taken == 0 ? objects_.size() : (edit.atTop ? highest + 1 - taken : lowest);
                    for (const std::shared_ptr<Slot>& slot : edit.added) {
                        attach(std::move(slot->object), at++);
                    }
                }
                setSelection(addedIds);
            },
            [this, edits, removedIds]() {
                for (std::size_t e = edits->size(); e > 0; --e) {
                    Edit& edit = (*edits)[e - 1];
                    for (std::size_t i = edit.added.size(); i > 0; --i) {
                        edit.added[i - 1]->object = detach(edit.added[i - 1]->id, &edit.added[i - 1]->index);
                    }
                    std::vector<std::shared_ptr<Slot>> ordered = edit.removed;
                    std::sort(ordered.begin(), ordered.end(), [](const std::shared_ptr<Slot>& a, const std::shared_ptr<Slot>& b) { return a->index < b->index; });
                    for (const std::shared_ptr<Slot>& slot : ordered) {
                        if (slot->object) {
                            attach(std::move(slot->object), slot->index);
                        }
                    }
                }
                setSelection(removedIds);
            }});
        return addedIds;
    }

    // ---- Pathfinder ----

    bool SketchDocument::combineSelection(BoolOp op) {
        std::vector<SketchObject*> chosen;   // bottom to top
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (isSelected(object->id()) && object->isVisible() && !object->isLocked()) {
                chosen.push_back(object.get());
            }
        }
        if (chosen.size() < 2) {
            return false;
        }
        Loops shape = flattenToLoops(chosen[0]->geometry());
        for (std::size_t i = 1; i < chosen.size(); ++i) {
            shape = combineLoops(shape, flattenToLoops(chosen[i]->geometry()), op);
        }
        if (shape.empty()) {
            return false;
        }
        const SketchObject* look = op == BoolOp::Subtract ? chosen.front() : chosen.back();
        Replacement replacement;
        for (SketchObject* object : chosen) {
            replacement.remove.push_back(object->id());
        }
        replacement.add.emplace_back(look->cloneWithGeometry(SketchObjectType::Path, loopsToPath(shape)));
        replacement.atTop = true;
        static const char* kNames[] = {"Unite", "Intersect", "Minus Front", "Exclude"};
        std::vector<Replacement> all;
        all.push_back(std::move(replacement));
        replaceUndoable(std::move(all), kNames[int(op)]);
        return true;
    }

    // ---- Shape Builder ----

    SketchDocument::ShapeBuilderModel SketchDocument::shapeBuilderModel() const {
        ShapeBuilderModel model;
        std::vector<Loops> shapes;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (isSelected(object->id()) && workable(*object) && paintsFill(*object)) {
                model.objects.push_back(object->id());
                shapes.push_back(flattenToLoops(object->geometry()));
            }
        }
        model.cells = arrangeCells(shapes);
        return model;
    }

    bool SketchDocument::shapeBuild(const std::vector<std::size_t>& cellIndices, bool erase) {
        ShapeBuilderModel model = shapeBuilderModel();
        std::vector<std::size_t> chosen;
        for (std::size_t index : cellIndices) {
            if (index < model.cells.size() && std::find(chosen.begin(), chosen.end(), index) == chosen.end()) {
                chosen.push_back(index);
            }
        }
        if (chosen.empty()) {
            return false;
        }
        Loops taken;
        std::vector<bool> touched(model.objects.size(), false);
        std::size_t lookOf = 0;   // the topmost shape under the first chosen cell wears the joined shape's look
        for (std::size_t n = 0; n < chosen.size(); ++n) {
            const ShapeCell& cell = model.cells[chosen[n]];
            taken = taken.empty() ? cell.region : combineLoops(taken, cell.region, BoolOp::Union);
            for (std::size_t member : cell.members) {
                touched[member] = true;
                if (n == 0) {
                    lookOf = std::max(lookOf, member);
                }
            }
        }
        if (taken.empty()) {
            return false;
        }
        Replacement replacement;
        for (std::size_t k = 0; k < model.objects.size(); ++k) {
            if (!touched[k]) {
                continue;
            }
            const SketchObject* source = findById(model.objects[k]);
            replacement.remove.push_back(source->id());
            Loops left = combineLoops(flattenToLoops(source->geometry()), taken, BoolOp::Subtract);
            if (!left.empty()) {
                replacement.add.emplace_back(source->cloneWithGeometry(SketchObjectType::Path, loopsToPath(left)));
            }
        }
        if (!erase) {
            replacement.add.emplace_back(findById(model.objects[lookOf])->cloneWithGeometry(SketchObjectType::Path, loopsToPath(taken)));
        }
        std::vector<Replacement> all;
        all.push_back(std::move(replacement));
        replaceUndoable(std::move(all), erase ? "Shape Builder Erase" : "Shape Builder");
        return true;
    }

    // ---- Knife ----

    namespace {
        // When some of the candidates are selected, only those are used; otherwise all of them (an unrelated selection never makes a
        // tool do nothing at all).
        template <class T>
        void preferSelected(std::vector<T>& candidates, const SketchDocument& document) {
            bool anySelected = false;
            for (const T& candidate : candidates) {
                anySelected = anySelected || document.isSelected(candidate.object->id());
            }
            if (anySelected) {
                candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                    [&document](const T& candidate) { return !document.isSelected(candidate.object->id()); }), candidates.end());
            }
        }

        struct Candidate {
            SketchObject* object = nullptr;
            Loops region;
        };
    }

    bool SketchDocument::knifeCut(const std::vector<BLPoint>& cut) {
        if (cut.size() < 2) {
            return false;
        }
        // The shapes the line as drawn passes through (the cut itself is carried on past its ends, but only what was touched is cut).
        std::vector<Candidate> candidates;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (!workable(*object) || !paintsFill(*object)) {
                continue;
            }
            Loops region = flattenToLoops(object->geometry());
            bool touched = false;
            if (!region.empty()) {
                clipLineOutside(cut, false, region, touched);
            }
            if (touched) {
                candidates.push_back(Candidate{object.get(), std::move(region)});
            }
        }
        preferSelected(candidates, *this);
        std::vector<Replacement> replacements;
        for (const Candidate& candidate : candidates) {
            std::vector<Loops> pieces = cutAlongLine(candidate.region, cut);
            if (pieces.size() < 2) {
                continue;
            }
            Replacement replacement;
            replacement.remove.push_back(candidate.object->id());
            for (const Loops& piece : pieces) {
                replacement.add.emplace_back(candidate.object->cloneWithGeometry(SketchObjectType::Path, loopsToPath(piece)));
            }
            replacements.push_back(std::move(replacement));
        }
        if (replacements.empty()) {
            return false;
        }
        replaceUndoable(std::move(replacements), "Knife Cut");
        return true;
    }

    // ---- Eraser ----

    bool SketchDocument::erase(const std::vector<BLPoint>& stroke, double width) {
        Loops pen = strokeToLoops(stroke, width);
        if (pen.empty()) {
            return false;
        }
        const BLBox penBox = boxOfLoops(pen);
        // Everything the pen touches; filled shapes carry their outline, lines are looked at below.
        std::vector<Candidate> candidates;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (!workable(*object) || !boxesOverlap(object->geometryBounds(), penBox)) {
                continue;
            }
            if (paintsFill(*object)) {
                Loops region = flattenToLoops(object->geometry());
                if (!region.empty() && !combineLoops(region, pen, BoolOp::Intersect).empty()) {
                    candidates.push_back(Candidate{object.get(), std::move(region)});
                }
            } else {
                bool touches = false;
                std::vector<bool> closed;
                for (const std::vector<BLPoint>& line : flattenToLines(object->geometry(), closed)) {
                    bool touched = false;
                    clipLineOutside(line, false, pen, touched);
                    touches = touches || touched;
                }
                if (touches) {
                    candidates.push_back(Candidate{object.get(), Loops()});
                }
            }
        }
        preferSelected(candidates, *this);
        std::vector<Replacement> replacements;
        for (const Candidate& candidate : candidates) {
            const SketchObject& object = *candidate.object;
            Replacement replacement;
            replacement.remove.push_back(object.id());
            if (paintsFill(object)) {
                // A filled shape: the pen's area is cut out of it; what is left may fall into several pieces (or nothing).
                for (const Loops& piece : splitIntoRegions(combineLoops(candidate.region, pen, BoolOp::Subtract))) {
                    replacement.add.emplace_back(object.cloneWithGeometry(SketchObjectType::Path, loopsToPath(piece)));
                }
            } else {
                // A line (or an outline with no fill): the parts the pen touched are gone, each stretch that is left is its own line.
                std::vector<bool> closed;
                std::vector<std::vector<BLPoint>> lines = flattenToLines(object.geometry(), closed);
                BLPath untouched;
                std::vector<std::vector<BLPoint>> leftOvers;
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    bool touched = false;
                    std::vector<std::vector<BLPoint>> runs = clipLineOutside(lines[i], closed[i], pen, touched);
                    if (!touched) {
                        untouched.add_path(linePath(lines[i]));
                        continue;
                    }
                    for (std::vector<BLPoint>& run : runs) {
                        leftOvers.push_back(std::move(run));
                    }
                }
                if (untouched.size() > 0) {
                    replacement.add.emplace_back(object.cloneWithGeometry(SketchObjectType::Path, untouched));
                }
                for (const std::vector<BLPoint>& run : leftOvers) {
                    replacement.add.emplace_back(object.cloneWithGeometry(SketchObjectType::Path, linePath(run)));
                }
            }
            replacements.push_back(std::move(replacement));
        }
        if (replacements.empty()) {
            return false;
        }
        replaceUndoable(std::move(replacements), "Erase");
        return true;
    }

    // ---- Scissors ----

    bool SketchDocument::scissorsCut(const BLPoint& at, double tolerance) {
        SketchObject* hitObject = nullptr;
        std::vector<SubPath> hitSubs;
        std::size_t hitSub = 0, hitSegment = 0;
        double hitT = 0.0, best = tolerance;
        for (std::size_t i = objects_.size(); i > 0; --i) {   // top first: on a tie the one in front is cut
            SketchObject& object = *objects_[i - 1];
            if (!workable(object)) {
                continue;
            }
            std::vector<SubPath> subs = pathToSubPaths(object.geometry());
            for (std::size_t s = 0; s < subs.size(); ++s) {
                std::size_t segment = 0;
                double t = 0.0, distance = 0.0;
                if (nearestOnSubPath(subs[s], at, segment, t, distance) && distance < best) {
                    best = distance;
                    hitObject = &object;
                    hitSubs = subs;
                    hitSub = s;
                    hitSegment = segment;
                    hitT = t;
                }
            }
        }
        if (hitObject == nullptr) {
            return false;
        }
        std::vector<SubPath> pieces;
        if (!splitSubPath(hitSubs[hitSub], hitSegment, hitT, pieces)) {
            return false;
        }
        std::vector<SubPath> rest;
        for (std::size_t s = 0; s < hitSubs.size(); ++s) {
            if (s != hitSub) {
                rest.push_back(hitSubs[s]);
            }
        }
        Replacement replacement;
        replacement.remove.push_back(hitObject->id());
        rest.push_back(pieces[0]);   // the closed path now open, or the first half of an open one (with whatever else the object had)
        replacement.add.emplace_back(hitObject->cloneWithGeometry(SketchObjectType::Path, subPathsToPath(rest)));
        if (pieces.size() > 1) {
            replacement.add.emplace_back(hitObject->cloneWithGeometry(SketchObjectType::Path, subPathsToPath({pieces[1]})));
        }
        std::vector<Replacement> all;
        all.push_back(std::move(replacement));
        replaceUndoable(std::move(all), "Cut Path");
        return true;
    }

}
