#include "curvy/sketchdocument.h"
#include "curvy/gzip.h"
#include "curvy/sketchbin.h"
#include "curvy/sketchsvg.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace curvy {

    namespace {
        // True when `path` ends with `extension` (a dot and letters), ignoring case.
        bool hasExtension(const std::string& path, const std::string& extension) {
            if (path.size() < extension.size()) {
                return false;
            }
            for (std::size_t i = 0; i < extension.size(); ++i) {
                char c = path[path.size() - extension.size() + i];
                if (std::tolower(static_cast<unsigned char>(c)) != extension[i]) {
                    return false;
                }
            }
            return true;
        }
    }

    void SketchDocument::setPageSize(float width, float height) {
        if (width <= 0.0f || height <= 0.0f) {
            throw std::runtime_error("SketchDocument::setPageSize: width and height must be positive");
        }
        artboards_[activeArtboard_].width = width;
        artboards_[activeArtboard_].height = height;
        changed();
    }

    void SketchDocument::setPpi(float ppi) {
        if (ppi <= 0.0f) {
            throw std::runtime_error("SketchDocument::setPpi: ppi must be positive");
        }
        ppi_ = ppi;
        changed();
    }

    void SketchDocument::setPageBackground(const std::optional<SketchColor>& color) {
        pageBackground_ = color;
        changed();
    }

    SketchObject& SketchDocument::object(std::size_t index) const {
        if (index >= objects_.size()) {
            throw std::runtime_error("SketchDocument::object: index out of range");
        }
        return *objects_[index];
    }

    SketchObject& SketchDocument::add(SketchObject* object) {
        if (object == nullptr) {
            throw std::runtime_error("SketchDocument::add: object is null");
        }
        std::unique_ptr<SketchObject> owned(object);
        owned->setId(nextId_++);
        SketchObject* raw = owned.get();   // changed() may move it down the stack (into its layer), so keep hold of it here
        objects_.push_back(std::move(owned));
        changed();
        return *raw;
    }

    bool SketchDocument::remove(std::size_t id) {
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            if (objects_[i]->id() == id) {
                objects_.erase(objects_.begin() + std::ptrdiff_t(i));
                pruneSelection();
                changed();
                return true;
            }
        }
        return false;
    }

    SketchObject* SketchDocument::findById(std::size_t id) const {
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (object->id() == id) {
                return object.get();
            }
        }
        return nullptr;
    }

    SketchObject* SketchDocument::hitTest(const BLPoint& point, double tolerance) const {
        for (std::size_t i = objects_.size(); i > 0; --i) {
            SketchObject* object = objects_[i - 1].get();
            if (object->isVisible() && !object->isLocked() && object->hitTest(point, tolerance)) {
                // Inside a clipping group only what shows through the mask can be hit.
                bool cutAway = false;
                for (std::size_t group : object->groups()) {
                    const SketchObject* mask = clipMaskOf(group);
                    if (mask != nullptr && mask != object && mask->geometry().hit_test(point, BL_FILL_RULE_NON_ZERO) != BL_HIT_TEST_IN) {
                        cutAway = true;
                    }
                }
                if (!cutAway) {
                    return object;
                }
            }
        }
        return nullptr;
    }

    void SketchDocument::clearObjects() {
        objects_.clear();
        clearGraph();
        nextId_ = 1;
        selection_.clear();
        guides_.clear();
        {
            // One artboard again, of the same size (a new or opened document sets the size next).
            Artboard first = artboards_[activeArtboard_];
            first.name = "Artboard 1";
            first.x = first.y = 0.0;
            artboards_.assign(1, first);
            activeArtboard_ = 0;
        }
        resetLayers();
        swatches_ = defaultSwatches();
        toolDefaults_ = ToolDefaults();
        symbols_.clear();
        patterns_.clear();
        brushes_.clear();
        activeBrush_ = 0;
        nextBrushId_ = 1;
        nextSymbolId_ = 1;
        undoStack_.clear();
        updateAllViews();
        // Panels that show the selection or the document follow (this is not an edit: the document is not marked modified).
        onSelectionChanged(*this);
        onChanged(*this);
    }

    void SketchDocument::paint(BLContext& ctx) const {
        paintRange(ctx, 0, objects_.size(), 0);
    }

    void SketchDocument::paintRange(BLContext& ctx, std::size_t begin, std::size_t end, std::size_t depth) const {
        std::size_t i = begin;
        while (i < end) {
            const SketchObject& object = *objects_[i];
            if (object.groups().size() <= depth) {
                object.paint(ctx);
                ++i;
                continue;
            }
            std::size_t group = object.groups()[depth];
            std::size_t j = i + 1;
            while (j < end && objects_[j]->groups().size() > depth && objects_[j]->groups()[depth] == group) {
                ++j;
            }
            GroupProps props = groupProps(group);
            const bool clipped = props.clip && j - i >= 2;
            if (props.isDefault() || (!clipped && props.opacity >= 1.0 && props.blendMode == BlendMode::Normal)) {
                paintRange(ctx, i, j, depth + 1);
            } else {
                bool any = false;
                BLBox box(0, 0, 0, 0);
                for (std::size_t k = i; k < j; ++k) {
                    if (!objects_[k]->isVisible()) {
                        continue;
                    }
                    BLBox b = objects_[k]->bounds();
                    box = any ? BLBox(std::min(box.x0, b.x0), std::min(box.y0, b.y0), std::max(box.x1, b.x1), std::max(box.y1, b.y1)) : b;
                    any = true;
                }
                if (any) {
                    auto paintBody = [this, i, j, depth, clipped](BLContext& target) {
                        if (!clipped) {
                            paintRange(target, i, j, depth + 1);
                            return;
                        }
                        // The mask is the topmost member: the others draw into a layer that is cut to its outline.
                        const SketchObject& mask = *objects_[j - 1];
                        BLBox m = mask.geometryBounds(), area(0, 0, 0, 0);
                        bool first = true;
                        for (std::size_t k = i; k + 1 < j; ++k) {
                            if (objects_[k]->isVisible()) {
                                BLBox b = objects_[k]->bounds();
                                area = first ? b : BLBox(std::min(area.x0, b.x0), std::min(area.y0, b.y0), std::max(area.x1, b.x1), std::max(area.y1, b.y1));
                                first = false;
                            }
                        }
                        if (first) {
                            return;
                        }
                        BLBox cut(std::max(area.x0, m.x0), std::max(area.y0, m.y0), std::min(area.x1, m.x1), std::min(area.y1, m.y1));
                        if (cut.x1 <= cut.x0 || cut.y1 <= cut.y0) {
                            return;
                        }
                        paintClipped(target, cut, mask.geometry(), [this, i, j, depth](BLContext& inner) { paintRange(inner, i, j - 1, depth + 1); });
                    };
                    if (props.opacity < 1.0 || props.blendMode != BlendMode::Normal) {
                        paintIsolated(ctx, box, props.opacity, props.blendMode, paintBody);
                    } else {
                        paintBody(ctx);
                    }
                }
            }
            i = j;
        }
    }

    GroupProps SketchDocument::groupProps(std::size_t groupId) const {
        auto it = groupProps_.find(groupId);
        return it != groupProps_.end() ? it->second : GroupProps();
    }

    void SketchDocument::setGroupProps(std::size_t groupId, const GroupProps& props) {
        if (props.isDefault()) {
            groupProps_.erase(groupId);
        } else {
            groupProps_[groupId] = props;
        }
        changed();
    }

    std::size_t SketchDocument::selectedGroup() const {
        if (selection_.empty()) {
            return 0;
        }
        const SketchObject* first = findById(selection_.front());
        if (first == nullptr || first->groups().empty()) {
            return 0;
        }
        std::size_t group = first->groups().front();
        std::size_t members = 0;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            members += !object->groups().empty() && object->groups().front() == group ? 1 : 0;
        }
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr || object->groups().empty() || object->groups().front() != group) {
                return 0;
            }
        }
        return members == selection_.size() ? group : 0;
    }

    void SketchDocument::setSelectionGroupOpacity(double opacity) {
        if (std::size_t group = selectedGroup()) {
            GroupProps props = groupProps(group);
            props.opacity = std::min(1.0, std::max(0.0, opacity));
            setGroupProps(group, props);
        }
    }

    void SketchDocument::setSelectionGroupBlendMode(BlendMode mode) {
        if (std::size_t group = selectedGroup()) {
            GroupProps props = groupProps(group);
            props.blendMode = mode;
            setGroupProps(group, props);
        }
    }

    // ---- Selection ----

    bool SketchDocument::isSelected(std::size_t id) const {
        return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
    }

    void SketchDocument::setSelection(const std::vector<std::size_t>& ids) {
        std::vector<std::size_t> valid;
        for (std::size_t id : ids) {
            if (findById(id) != nullptr && std::find(valid.begin(), valid.end(), id) == valid.end()) {
                valid.push_back(id);
            }
        }
        if (valid == selection_) {
            return;
        }
        selection_ = valid;
        groupAngle_.reset();
        quadFrame_.reset();
        originU_ = originV_ = 0.5;
        onSelectionChanged(*this);
        updateAllViews();
    }

    void SketchDocument::selectAll() {
        std::vector<std::size_t> ids;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (object->isVisible() && !object->isLocked()) {
                ids.push_back(object->id());
            }
        }
        setSelection(ids);
    }

    bool SketchDocument::selectionBounds(BLBox& out) const {
        bool any = false;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr) {
                continue;
            }
            BLBox box = object->geometryBounds();
            for (std::size_t group : object->groups()) {
                const SketchObject* mask = clipMaskOf(group);
                if (mask != nullptr && mask != object) {
                    BLBox m = mask->geometryBounds();
                    BLBox cut(std::max(box.x0, m.x0), std::max(box.y0, m.y0), std::min(box.x1, m.x1), std::min(box.y1, m.y1));
                    box = cut.x1 >= cut.x0 && cut.y1 >= cut.y0 ? cut : m;
                }
            }
            if (!any) {
                out = box;
                any = true;
            } else {
                out.x0 = std::min(out.x0, box.x0);
                out.y0 = std::min(out.y0, box.y0);
                out.x1 = std::max(out.x1, box.x1);
                out.y1 = std::max(out.y1, box.y1);
            }
        }
        return any;
    }

    namespace {
        // Heckbert's unit-square -> quad coefficients: X = (a u + b v + c) / (g u + h v + 1), Y likewise with d, e, f.
        struct Projective {
            double a, b, c, d, e, f, g, h;
        };

        Projective projectiveFor(const BLPoint* q) {
            double dx1 = q[1].x - q[2].x, dx2 = q[3].x - q[2].x, dx3 = q[0].x - q[1].x + q[2].x - q[3].x;
            double dy1 = q[1].y - q[2].y, dy2 = q[3].y - q[2].y, dy3 = q[0].y - q[1].y + q[2].y - q[3].y;
            double g = 0.0, h = 0.0;
            double den = dx1 * dy2 - dx2 * dy1;
            if ((std::fabs(dx3) > 1e-12 || std::fabs(dy3) > 1e-12) && std::fabs(den) > 1e-12) {
                g = (dx3 * dy2 - dx2 * dy3) / den;
                h = (dx1 * dy3 - dx3 * dy1) / den;
            }
            return Projective{q[1].x - q[0].x + g * q[1].x, q[3].x - q[0].x + h * q[3].x, q[0].x,
                q[1].y - q[0].y + g * q[1].y, q[3].y - q[0].y + h * q[3].y, q[0].y, g, h};
        }
    }

    BLPoint QuadFrame::map(double u, double v) const {
        Projective m = projectiveFor(p);
        double denom = m.g * u + m.h * v + 1.0;
        if (std::fabs(denom) < 1e-9) {
            denom = denom < 0 ? -1e-9 : 1e-9;
        }
        return BLPoint((m.a * u + m.b * v + m.c) / denom, (m.d * u + m.e * v + m.f) / denom);
    }

    bool QuadFrame::unmap(const BLPoint& world, double& u, double& v) const {
        Projective m = projectiveFor(p);
        // Invert the 3x3 [[a b c] [d e f] [g h 1]] with the adjugate.
        double A = m.e - m.f * m.h, B = m.c * m.h - m.b, C = m.b * m.f - m.c * m.e;
        double D = m.f * m.g - m.d, E = m.a - m.c * m.g, F = m.c * m.d - m.a * m.f;
        double G = m.d * m.h - m.e * m.g, H = m.b * m.g - m.a * m.h, I = m.a * m.e - m.b * m.d;
        double det = m.a * A + m.b * D + m.c * G;
        if (std::fabs(det) < 1e-12) {
            return false;
        }
        double uu = A * world.x + B * world.y + C;
        double vv = D * world.x + E * world.y + F;
        double ww = G * world.x + H * world.y + I;
        if (std::fabs(ww) < 1e-12) {
            return false;
        }
        u = uu / ww;
        v = vv / ww;
        return true;
    }

    QuadFrame QuadFrame::transformed(const BLMatrix2D& matrix) const {
        QuadFrame result;
        for (int i = 0; i < 4; ++i) {
            result.p[i] = matrix.map_point(p[i]);
        }
        return result;
    }

    QuadFrame SelectionFrame::asQuad() const {
        if (quad.has_value()) {
            return *quad;
        }
        QuadFrame result;
        result.p[0] = toWorld(local.x0, local.y0);
        result.p[1] = toWorld(local.x1, local.y0);
        result.p[2] = toWorld(local.x1, local.y1);
        result.p[3] = toWorld(local.x0, local.y1);
        return result;
    }

    BLPoint SelectionFrame::toWorld(double lx, double ly) const {
        double c = std::cos(angle), s = std::sin(angle);
        return BLPoint(lx * c - ly * s, lx * s + ly * c);
    }

    BLPoint SelectionFrame::toLocal(const BLPoint& world) const {
        double c = std::cos(angle), s = std::sin(angle);
        return BLPoint(world.x * c + world.y * s, -world.x * s + world.y * c);
    }

    bool SketchDocument::selectionFrame(SelectionFrame& out) const {
        double angle = 0.0;
        bool any = false;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr) {
                continue;
            }
            if (!any) {
                angle = object->frameAngle();
                any = true;
            } else if (std::fabs(object->frameAngle() - angle) > 1e-9) {
                angle = groupAngle_.value_or(0.0);  // mixed orientations: the group's own angle
                break;
            }
        }
        if (!any) {
            return false;
        }
        out.angle = angle;
        out.quad = quadFrame_;
        bool first = true;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr) {
                continue;
            }
            BLPath path = object->geometry();
            if (angle != 0.0) {
                path.transform(BLMatrix2D::make_rotation(-angle));
            }
            BLBox box(0, 0, 0, 0);
            path.get_bounding_box(&box);
            if (first) {
                out.local = box;
                first = false;
            } else {
                out.local.x0 = std::min(out.local.x0, box.x0);
                out.local.y0 = std::min(out.local.y0, box.y0);
                out.local.x1 = std::max(out.local.x1, box.x1);
                out.local.y1 = std::max(out.local.y1, box.y1);
            }
        }
        return true;
    }

    BLPoint SketchDocument::selectionOrigin(const SelectionFrame& frame) const {
        return frame.pointAt(originU_, originV_);
    }

    void SketchDocument::setSelectionOrigin(const SelectionFrame& frame, const BLPoint& world) {
        double u = 0.5, v = 0.5;
        if (frame.unmapPoint(world, u, v)) {
            originU_ = u;
            originV_ = v;
        }
        updateAllViews();
    }

    void SketchDocument::pruneSelection() {
        std::size_t before = selection_.size();
        selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
            [this](std::size_t id) { return findById(id) == nullptr; }), selection_.end());
        if (selection_.size() != before) {
            groupAngle_.reset();
            quadFrame_.reset();
            originU_ = originV_ = 0.5;
            onSelectionChanged(*this);
        }
    }

    // ---- Ownership moves used by undo ----

    std::unique_ptr<SketchObject> SketchDocument::detach(std::size_t id, std::size_t* index) {
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            if (objects_[i]->id() == id) {
                std::unique_ptr<SketchObject> taken = std::move(objects_[i]);
                objects_.erase(objects_.begin() + std::ptrdiff_t(i));
                if (index != nullptr) {
                    *index = i;
                }
                pruneSelection();
                changed();
                return taken;
            }
        }
        return nullptr;
    }

    void SketchDocument::attach(std::unique_ptr<SketchObject> object, std::size_t index) {
        if (index > objects_.size()) {
            index = objects_.size();
        }
        objects_.insert(objects_.begin() + std::ptrdiff_t(index), std::move(object));
        changed();
    }

    // ---- Undoable edits ----

    namespace {
        // std::function needs a copyable callable, so an object that undo/redo hands back and forth
        // lives in a shared slot.
        struct Slot {
            std::unique_ptr<SketchObject> object;
            std::size_t id = 0;
            std::size_t index = 0;
        };
    }

    SketchObject& SketchDocument::addUndoable(SketchObject* object, const std::string& description) {
        if (object == nullptr) {
            throw std::runtime_error("SketchDocument::addUndoable: object is null");
        }
        auto slot = std::make_shared<Slot>();
        slot->object.reset(object);
        slot->index = objects_.size();
        slot->object->setId(nextId_++);
        slot->id = slot->object->id();
        SketchObject* raw = object;
        undoStack_.push({description,
            [this, slot]() {
                attach(std::move(slot->object), slot->index);
                setSelection({slot->id});
            },
            [this, slot]() {
                slot->object = detach(slot->id, &slot->index);
            }});
        return *raw;
    }

    void SketchDocument::deleteSelection() {
        if (selection_.empty()) {
            return;
        }
        std::vector<std::shared_ptr<Slot>> slots;
        for (std::size_t id : selection_) {
            auto slot = std::make_shared<Slot>();
            slot->id = id;
            slots.push_back(slot);
        }
        std::vector<std::size_t> ids = selection_;
        undoStack_.push({"Delete",
            [this, slots]() {
                // Highest stack position first, so every recorded index is its original one.
                for (std::size_t i = objects_.size(); i > 0; --i) {
                    std::size_t id = objects_[i - 1]->id();
                    for (const std::shared_ptr<Slot>& slot : slots) {
                        if (slot->id == id) {
                            slot->object = detach(id, &slot->index);
                            break;
                        }
                    }
                }
            },
            [this, slots, ids]() {
                // Reinsert lowest index first so each stored index is valid again.
                std::vector<std::shared_ptr<Slot>> ordered = slots;
                std::sort(ordered.begin(), ordered.end(),
                    [](const std::shared_ptr<Slot>& a, const std::shared_ptr<Slot>& b) { return a->index < b->index; });
                for (const std::shared_ptr<Slot>& slot : ordered) {
                    if (slot->object) {
                        attach(std::move(slot->object), slot->index);
                    }
                }
                setSelection(ids);
            }});
    }

    void SketchDocument::duplicateSelection(double dx, double dy) {
        std::vector<std::unique_ptr<SketchObject>> copies;
        for (std::size_t id : selection_) {
            if (const SketchObject* source = findById(id)) {
                copies.emplace_back(source->clone());
                copies.back()->transform(BLMatrix2D::make_translation(dx, dy));
            }
        }
        addObjectsUndoable(std::move(copies), "Duplicate");
    }

    std::vector<std::size_t> SketchDocument::addObjectsUndoable(std::vector<std::unique_ptr<SketchObject>> objects, const std::string& description,
        const SketchDocument* groupsFrom) {
        std::vector<std::shared_ptr<Slot>> slots;
        std::vector<std::size_t> ids;
        std::size_t index = objects_.size();
        // Groups come along, but as new groups of their own (copies must not share a group with the originals).
        std::map<std::size_t, std::size_t> freshGroups;
        for (std::unique_ptr<SketchObject>& object : objects) {
            std::vector<std::size_t> groups = object->groups();
            for (std::size_t& g : groups) {
                auto it = freshGroups.find(g);
                if (it == freshGroups.end()) {
                    std::size_t fresh = nextGroupId_++;
                    GroupProps props = (groupsFrom != nullptr ? groupsFrom : this)->groupProps(g);
                    if (!props.isDefault()) {
                        groupProps_[fresh] = props;
                    }
                    it = freshGroups.emplace(g, fresh).first;
                }
                g = it->second;
            }
            object->setGroups(groups);
            auto slot = std::make_shared<Slot>();
            slot->object = std::move(object);
            slot->object->setId(nextId_++);
            slot->id = slot->object->id();
            slot->index = index++;
            ids.push_back(slot->id);
            slots.push_back(slot);
        }
        if (slots.empty()) {
            return ids;
        }
        undoStack_.push({description,
            [this, slots, ids]() {
                for (const std::shared_ptr<Slot>& slot : slots) {
                    attach(std::move(slot->object), slot->index);
                }
                setSelection(ids);
            },
            [this, slots]() {
                for (std::size_t i = slots.size(); i > 0; --i) {
                    slots[i - 1]->object = detach(slots[i - 1]->id, &slots[i - 1]->index);
                }
            }});
        return ids;
    }

    std::string SketchDocument::selectionToSvg() const {
        SketchDocument scratch;
        scratch.setPageSize(pageWidth(), pageHeight());
        scratch.setColorMode(colorMode_);
        scratch.setSymbols(symbols_);
        scratch.setPatterns(patterns_);
        scratch.setBrushes(brushes_);
        for (const std::unique_ptr<SketchObject>& object : objects_) {  // stacking order, so groups stay adjacent
            if (isSelected(object->id())) {
                scratch.add(object->clone());
            }
        }
        return scratch.objects_.empty() ? std::string() : svgFromDocument(scratch);
    }

    bool SketchDocument::pasteSvg(const std::string& svg, double dx, double dy) {
        SketchDocument scratch;
        if (!documentFromSvg(svg, scratch) || scratch.objectCount() == 0) {
            return false;
        }
        std::vector<std::unique_ptr<SketchObject>> objects;
        for (std::size_t i = 0; i < scratch.objectCount(); ++i) {
            objects.emplace_back(scratch.object(i).clone());
            objects.back()->transform(BLMatrix2D::make_translation(dx, dy));
            if (objects.back()->isSymbol()) {
                objects.back()->setSymbol(importSymbol(objects.back()->symbol()));
            }
            std::vector<AppearanceItem> items = objects.back()->appearance();
            bool rebound = false;
            for (AppearanceItem& item : items) {
                if (item.strokeOptions.brush) {
                    item.strokeOptions.brush = importBrush(item.strokeOptions.brush);
                    rebound = true;
                }
                if (item.paint.isPattern() && item.paint.patternDef()) {
                    item.paint = SketchPaint::pattern(importPattern(item.paint.patternDef()), item.paint.patternScale(), item.paint.patternAngle());
                    rebound = true;
                }
            }
            if (rebound) {
                objects.back()->setAppearance(items);
            }
        }
        addObjectsUndoable(std::move(objects), "Paste", &scratch);
        return true;
    }

    bool SketchDocument::resetBoundingBox() {
        std::vector<GeometryState> before = snapshotSelection();
        if (before.empty()) {
            return false;
        }
        FrameState frameBefore = frameState();
        bool needed = groupAngle_.has_value() || quadFrame_.has_value();
        for (const GeometryState& entry : before) {
            needed = needed || std::fabs(entry.angle) > 1e-12;
        }
        if (!needed) {
            return false;
        }
        for (const GeometryState& entry : before) {
            if (SketchObject* object = findById(entry.id)) {
                object->setFrameAngle(0.0);
            }
        }
        groupAngle_.reset();
        quadFrame_.reset();
        originU_ = originV_ = 0.5;
        commitGeometry(before, "Reset Bounding Box", frameBefore);
        return true;
    }

    std::vector<GeometryState> SketchDocument::snapshotSelection() const {
        std::vector<GeometryState> result;
        for (std::size_t id : selection_) {
            if (const SketchObject* object = findById(id)) {
                result.push_back({id, object->geometry(), object->frameAngle(), object->textMatrix()});
            }
        }
        return result;
    }

    void SketchDocument::commitGeometry(const std::vector<GeometryState>& before, const std::string& description,
        const FrameState& frameBefore) {
        FrameState frameAfter = frameState();
        std::vector<GeometryState> after;
        for (const GeometryState& entry : before) {
            if (const SketchObject* object = findById(entry.id)) {
                after.push_back({entry.id, object->geometry(), object->frameAngle(), object->textMatrix()});
            }
        }
        auto apply = [this](const std::vector<GeometryState>& state, const FrameState& frame) {
            for (const GeometryState& entry : state) {
                if (SketchObject* object = findById(entry.id)) {
                    object->setGeometry(entry.path);
                    object->setTextMatrix(entry.textMatrix);
                    object->setFrameAngle(entry.angle);
                }
            }
            groupAngle_ = frame.groupAngle;
            quadFrame_ = frame.quad;
            changed();
        };
        undoStack_.push({description,
            [apply, after, frameAfter]() { apply(after, frameAfter); },
            [apply, before, frameBefore]() { apply(before, frameBefore); }});
    }

    void SketchDocument::transformObjects(const std::vector<std::size_t>& ids, const BLMatrix2D& matrix, const std::string& description) {
        std::vector<GeometryState> before;
        FrameState frameBefore = frameState();
        for (std::size_t id : ids) {
            if (SketchObject* object = findById(id)) {
                before.push_back({id, object->geometry(), object->frameAngle(), object->textMatrix()});
                object->transform(matrix);
            }
        }
        if (!before.empty()) {
            if (quadFrame_.has_value()) {
                quadFrame_ = quadFrame_->transformed(matrix);
            }
            noteTransform(matrix, 0.0);
            commitGeometry(before, description, frameBefore);
        }
    }

    // ---- Arrange / align / transform / lock / hide ----

    void SketchDocument::reorder(const std::vector<std::size_t>& ids) {
        std::vector<std::unique_ptr<SketchObject>> sorted;
        for (std::size_t id : ids) {
            for (std::unique_ptr<SketchObject>& object : objects_) {
                if (object != nullptr && object->id() == id) {
                    sorted.push_back(std::move(object));
                    break;
                }
            }
        }
        // Anything the list did not name (cannot happen for our own orders) keeps its place at the front.
        std::vector<std::unique_ptr<SketchObject>> rest;
        for (std::unique_ptr<SketchObject>& object : objects_) {
            if (object != nullptr) {
                rest.push_back(std::move(object));
            }
        }
        objects_ = std::move(rest);
        for (std::unique_ptr<SketchObject>& object : sorted) {
            objects_.push_back(std::move(object));
        }
        changed();
    }

    std::vector<std::size_t> SketchDocument::withGroupMembers(const std::vector<std::size_t>& ids) const {
        std::vector<std::size_t> out = ids;
        std::vector<std::size_t> outer;
        for (std::size_t id : ids) {
            const SketchObject* object = findById(id);
            if (object != nullptr && !object->groups().empty()) {
                outer.push_back(object->groups().front());
            }
        }
        if (outer.empty()) {
            return out;
        }
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (object->groups().empty() || !object->isVisible() || object->isLocked() ||
                std::find(outer.begin(), outer.end(), object->groups().front()) == outer.end() ||
                std::find(out.begin(), out.end(), object->id()) != out.end()) {
                continue;
            }
            out.push_back(object->id());
        }
        return out;
    }

    bool SketchDocument::canUngroup() const {
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object != nullptr && !object->groups().empty()) {
                return true;
            }
        }
        return false;
    }

    void SketchDocument::applyGroups(const std::vector<GroupState>& states) {
        for (const GroupState& state : states) {
            if (SketchObject* object = findById(state.id)) {
                object->setGroups(state.groups);
            }
        }
    }

    bool SketchDocument::groupSelection(bool clip) {
        if (selection_.size() < 2) {
            return false;
        }
        std::vector<std::size_t> orderBefore;
        std::vector<GroupState> groupsBefore;
        std::size_t top = 0;
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            orderBefore.push_back(objects_[i]->id());
            groupsBefore.push_back({objects_[i]->id(), objects_[i]->groups()});
            if (isSelected(objects_[i]->id())) {
                top = i;
            }
        }
        // The members keep their relative order and take the place of the topmost one.
        std::vector<std::size_t> lower, picked, upper;
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            std::size_t id = objects_[i]->id();
            (isSelected(id) ? picked : (i < top ? lower : upper)).push_back(id);
        }
        std::vector<std::size_t> orderAfter = lower;
        orderAfter.insert(orderAfter.end(), picked.begin(), picked.end());
        orderAfter.insert(orderAfter.end(), upper.begin(), upper.end());

        // Members from different layers all move to the layer of the topmost one.
        std::vector<std::pair<std::size_t, std::size_t>> layersBefore, layersAfter;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            layersBefore.push_back({object->id(), object->layer()});
            layersAfter.push_back({object->id(), isSelected(object->id()) ? objects_[top]->layer() : object->layer()});
        }
        auto applyObjectLayers = [this](const std::vector<std::pair<std::size_t, std::size_t>>& states) {
            for (const auto& [id, layer] : states) {
                if (SketchObject* object = findById(id)) {
                    object->setLayer(layer);
                }
            }
        };
        applyObjectLayers(layersAfter);
        orderAfter = layerSorted(orderAfter);
        std::size_t group = nextGroupId_++;
        std::vector<GroupState> groupsAfter = groupsBefore;
        for (GroupState& state : groupsAfter) {
            if (isSelected(state.id)) {
                state.groups.insert(state.groups.begin(), group);
            }
        }
        GroupProps clipProps;
        clipProps.clip = clip;
        auto applyProps = [this, group, clipProps](bool on) {
            if (on && clipProps.clip) { groupProps_[group] = clipProps; } else { groupProps_.erase(group); }
        };
        applyProps(true);
        applyGroups(groupsAfter);
        reorder(orderAfter);
        undoStack_.push({clip ? "Make Clipping Mask" : "Group",
            [this, orderAfter, groupsAfter, layersAfter, applyObjectLayers, applyProps]() { applyProps(true); applyObjectLayers(layersAfter); applyGroups(groupsAfter); reorder(orderAfter); },
            [this, orderBefore, groupsBefore, layersBefore, applyObjectLayers, applyProps]() { applyProps(false); applyObjectLayers(layersBefore); applyGroups(groupsBefore); reorder(orderBefore); }});
        return true;
    }

    bool SketchDocument::ungroupSelection() {
        std::vector<GroupState> before, after;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr || object->groups().empty()) {
                continue;
            }
            before.push_back({id, object->groups()});
            std::vector<std::size_t> rest(object->groups().begin() + 1, object->groups().end());
            after.push_back({id, rest});
        }
        if (before.empty()) {
            return false;
        }
        applyGroups(after);
        changed();
        undoStack_.push({"Ungroup",
            [this, after]() { applyGroups(after); changed(); },
            [this, before]() { applyGroups(before); changed(); }});
        return true;
    }

    bool SketchDocument::arrangeSelection(ArrangeOp op) {
        if (selection_.empty()) {
            return false;
        }
        std::vector<std::size_t> before;
        std::vector<bool> chosen;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            before.push_back(object->id());
            chosen.push_back(isSelected(object->id()));
        }
        std::vector<std::size_t> after = before;
        std::vector<bool> flags = chosen;
        std::size_t n = after.size();
        switch (op) {
            case ArrangeOp::BringToFront:
            case ArrangeOp::SendToBack: {
                std::vector<std::size_t> picked, others;
                for (std::size_t i = 0; i < n; ++i) {
                    (flags[i] ? picked : others).push_back(after[i]);
                }
                after = op == ArrangeOp::BringToFront ? others : picked;
                after.insert(after.end(), op == ArrangeOp::BringToFront ? picked.begin() : others.begin(),
                    op == ArrangeOp::BringToFront ? picked.end() : others.end());
                break;
            }
            case ArrangeOp::BringForward:
            case ArrangeOp::SendBackward: {
                // Step by whole units: a group moves past a neighbor (or a neighboring group) as one.
                std::vector<std::vector<std::size_t>> units;
                std::vector<bool> unitFlags;
                for (std::size_t i = 0; i < n; ++i) {
                    const std::vector<std::size_t>& groups = objects_[i]->groups();
                    bool joins = i > 0 && !groups.empty() && !objects_[i - 1]->groups().empty() &&
                        groups.front() == objects_[i - 1]->groups().front();
                    if (joins) {
                        units.back().push_back(after[i]);
                        unitFlags.back() = unitFlags.back() || flags[i];
                    } else {
                        units.push_back({after[i]});
                        unitFlags.push_back(flags[i]);
                    }
                }
                std::size_t m = units.size();
                if (op == ArrangeOp::BringForward) {
                    for (std::size_t i = m; i-- > 1;) {
                        if (unitFlags[i - 1] && !unitFlags[i]) {
                            std::swap(units[i - 1], units[i]);
                            std::swap(unitFlags[i - 1], unitFlags[i]);
                        }
                    }
                } else {
                    for (std::size_t i = 1; i < m; ++i) {
                        if (unitFlags[i] && !unitFlags[i - 1]) {
                            std::swap(units[i - 1], units[i]);
                            std::swap(unitFlags[i - 1], unitFlags[i]);
                        }
                    }
                }
                after.clear();
                for (const std::vector<std::size_t>& unit : units) {
                    after.insert(after.end(), unit.begin(), unit.end());
                }
                break;
            }
        }
        after = layerSorted(after);   // an object stays inside its layer
        if (after == before) {
            return false;
        }
        const char* what = op == ArrangeOp::BringToFront ? "Bring to Front" : (op == ArrangeOp::BringForward ? "Bring Forward"
            : (op == ArrangeOp::SendBackward ? "Send Backward" : "Send to Back"));
        reorder(after);
        undoStack_.push({what, [this, after]() { reorder(after); }, [this, before]() { reorder(before); }});
        return true;
    }

    bool SketchDocument::alignSelection(AlignMode mode) {
        std::vector<GeometryState> before = snapshotSelection();
        if (before.empty()) {
            return false;
        }
        FrameState frameBefore = frameState();
        BLBox target = artboards_[activeArtboard_].box();  // one object (or one group) lines up with the active artboard
        // A group aligns as one piece: its members share the group's bounds.
        auto unitKey = [this](const SketchObject& o) { return o.groups().empty() ? o.id() * 2 : o.groups().front() * 2 + 1; };
        std::map<std::size_t, BLBox> unitBounds;
        for (const GeometryState& entry : before) {
            if (const SketchObject* object = findById(entry.id)) {
                BLBox b = object->geometryBounds();
                auto [it, fresh] = unitBounds.emplace(unitKey(*object), b);
                if (!fresh) {
                    it->second = BLBox(std::min(it->second.x0, b.x0), std::min(it->second.y0, b.y0),
                        std::max(it->second.x1, b.x1), std::max(it->second.y1, b.y1));
                }
            }
        }
        if (unitBounds.size() > 1) {
            selectionBounds(target);
        }
        bool moved = false;
        for (const GeometryState& entry : before) {
            SketchObject* object = findById(entry.id);
            if (object == nullptr) {
                continue;
            }
            BLBox b = unitBounds[unitKey(*object)];
            double dx = 0.0, dy = 0.0;
            switch (mode) {
                case AlignMode::Left: dx = target.x0 - b.x0; break;
                case AlignMode::Center: dx = (target.x0 + target.x1) * 0.5 - (b.x0 + b.x1) * 0.5; break;
                case AlignMode::Right: dx = target.x1 - b.x1; break;
                case AlignMode::Top: dy = target.y0 - b.y0; break;
                case AlignMode::Middle: dy = (target.y0 + target.y1) * 0.5 - (b.y0 + b.y1) * 0.5; break;
                case AlignMode::Bottom: dy = target.y1 - b.y1; break;
            }
            if (std::fabs(dx) > 1e-9 || std::fabs(dy) > 1e-9) {
                object->transform(BLMatrix2D::make_translation(dx, dy));
                moved = true;
            }
        }
        if (!moved) {
            return false;
        }
        quadFrame_.reset();
        static const char* kNames[] = {"Align Left", "Align Center", "Align Right", "Align Top", "Align Middle", "Align Bottom"};
        commitGeometry(before, kNames[int(mode)], frameBefore);
        return true;
    }

    bool SketchDocument::rotateSelection90(bool clockwise) {
        BLBox box;
        std::vector<GeometryState> before = snapshotSelection();
        if (before.empty() || !selectionBounds(box)) {
            return false;
        }
        FrameState frameBefore = frameState();
        double angle = clockwise ? 3.14159265358979323846 * 0.5 : -3.14159265358979323846 * 0.5;
        double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
        BLMatrix2D m = BLMatrix2D::make_translation(-cx, -cy);
        m.post_rotate(angle);
        m.post_translate(cx, cy);
        for (const GeometryState& entry : before) {
            if (SketchObject* object = findById(entry.id)) {
                object->transform(m);
                object->setFrameAngle(entry.angle + angle);
            }
        }
        quadFrame_.reset();
        groupAngle_.reset();
        commitGeometry(before, clockwise ? "Rotate 90 CW" : "Rotate 90 CCW", frameBefore);
        return true;
    }

    bool SketchDocument::flipSelection(bool horizontal) {
        BLBox box;
        std::vector<GeometryState> before = snapshotSelection();
        if (before.empty() || !selectionBounds(box)) {
            return false;
        }
        FrameState frameBefore = frameState();
        double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
        BLMatrix2D m = BLMatrix2D::make_translation(-cx, -cy);
        m.post_scale(horizontal ? -1.0 : 1.0, horizontal ? 1.0 : -1.0);
        m.post_translate(cx, cy);
        for (const GeometryState& entry : before) {
            if (SketchObject* object = findById(entry.id)) {
                object->transform(m);
                object->setFrameAngle(-entry.angle);  // a mirrored turned box is the box turned the other way
            }
        }
        quadFrame_.reset();
        groupAngle_.reset();
        commitGeometry(before, horizontal ? "Flip Horizontal" : "Flip Vertical", frameBefore);
        return true;
    }

    bool SketchDocument::applyFlags(const std::vector<FlagState>& before, const std::vector<FlagState>& after, const std::string& description) {
        if (before.empty()) {
            return false;
        }
        auto apply = [this](const std::vector<FlagState>& states) {
            for (const FlagState& state : states) {
                if (SketchObject* object = findById(state.id)) {
                    object->setVisible(state.visible);
                    object->setLocked(state.locked);
                }
            }
            // A hidden or locked object cannot stay selected.
            std::vector<std::size_t> keep;
            for (std::size_t id : selection_) {
                const SketchObject* object = findById(id);
                if (object != nullptr && object->isVisible() && !object->isLocked()) {
                    keep.push_back(id);
                }
            }
            if (keep.size() != selection_.size()) {
                setSelection(keep);
            }
            changed();
        };
        apply(after);
        undoStack_.push({description, [apply, after]() { apply(after); }, [apply, before]() { apply(before); }});
        return true;
    }

    bool SketchDocument::changeSelectionText(const std::function<void(SketchObject&)>& change, const std::string& description) {
        struct Snap {
            std::size_t id;
            std::string text, font;
            double size;
            TextStyle style;
            bool operator==(const Snap& o) const { return text == o.text && font == o.font && size == o.size && style == o.style; }
        };
        std::vector<Snap> before, after;
        for (std::size_t id : selection_) {
            SketchObject* object = findById(id);
            if (object == nullptr || !object->isText()) {
                continue;
            }
            before.push_back({id, object->text(), object->fontName(), object->fontSize(), object->textStyle()});
            change(*object);
            after.push_back({id, object->text(), object->fontName(), object->fontSize(), object->textStyle()});
        }
        if (before.empty() || before == after) {
            return false;
        }
        auto apply = [this](const std::vector<Snap>& snaps) {
            for (const Snap& snap : snaps) {
                if (SketchObject* object = findById(snap.id)) {
                    object->setTextAll(snap.text, snap.font, snap.size, snap.style);
                }
            }
            changed();
        };
        undoStack_.push({description, [apply, after]() { apply(after); }, [apply, before]() { apply(before); }});
        return true;
    }

    bool SketchDocument::lockSelection() {
        std::vector<FlagState> before, after;
        for (std::size_t id : selection_) {
            if (const SketchObject* object = findById(id)) {
                before.push_back({id, object->ownVisible(), object->ownLocked()});
                after.push_back({id, object->ownVisible(), true});
            }
        }
        return applyFlags(before, after, "Lock");
    }

    bool SketchDocument::hideSelection() {
        std::vector<FlagState> before, after;
        for (std::size_t id : selection_) {
            if (const SketchObject* object = findById(id)) {
                before.push_back({id, object->ownVisible(), object->ownLocked()});
                after.push_back({id, false, object->ownLocked()});
            }
        }
        return applyFlags(before, after, "Hide");
    }

    bool SketchDocument::unlockAll() {
        std::vector<FlagState> before, after;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (object->ownLocked()) {
                before.push_back({object->id(), object->ownVisible(), true});
                after.push_back({object->id(), object->ownVisible(), false});
            }
        }
        return applyFlags(before, after, "Unlock All");
    }

    bool SketchDocument::showAll() {
        std::vector<FlagState> before, after;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (!object->ownVisible()) {
                before.push_back({object->id(), false, object->ownLocked()});
                after.push_back({object->id(), true, object->ownLocked()});
            }
        }
        return applyFlags(before, after, "Show All");
    }

    bool SketchDocument::readFromFile(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        std::string text = buffer.str();
        if (isBinaryDocument(text)) {   // .skb: told by its first bytes, not its name
            return documentFromBinary(text, *this);
        }
        if (isGzip(text)) {   // .svgz: told by its first bytes, not its name
            std::string inflated;
            if (!gzipDecompress(text, inflated)) {
                return false;
            }
            return documentFromSvg(inflated, *this);
        }
        return documentFromSvg(text, *this);
    }

    bool SketchDocument::writeToFile(const std::string& path) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        std::string text = hasExtension(path, ".skb") ? binaryFromDocument(*this) : svgFromDocument(*this);
        if (hasExtension(path, ".svgz")) {
            text = gzipCompress(text);
        }
        out << text;
        return static_cast<bool>(out);
    }

    void SketchDocument::changed() {
        if (batchDepth_ > 0) {
            batchPending_ = true;
            return;
        }
        syncLayers();   // new objects find their layer, layer state reaches the objects, the stack follows the layers
        markModified();
        updateAllViews();
        onChanged(*this);
    }

    // ---- Style ----

    StyleState SketchDocument::withGroup(StyleState state) const {
        const SketchObject* object = findById(state.id);
        if (object != nullptr && !object->groups().empty()) {
            state.group = object->groups().front();
            state.groupProps = groupProps(state.group);
        }
        return state;
    }

    std::vector<StyleState> SketchDocument::snapshotStyle() const {
        std::vector<StyleState> result;
        for (std::size_t id : selection_) {
            if (const SketchObject* object = findById(id)) {
                result.push_back(withGroup(StyleState::of(*object)));
            }
        }
        return result;
    }

    void SketchDocument::setSelectionFill(const SketchPaint& paint) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setFill(paint);
            }
        }
        changed();
    }

    void SketchDocument::setSelectionStroke(const SketchPaint& paint) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setStroke(paint);
            }
        }
        changed();
    }

    void SketchDocument::setSelectionStrokeWidth(double width) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setStrokeWidth(width);
            }
        }
        changed();
    }

    void SketchDocument::setSelectionOpacity(double opacity) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setOpacity(opacity);
            }
        }
        changed();
    }

    void SketchDocument::setSelectionBlendMode(BlendMode mode) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setBlendMode(mode);
            }
        }
        changed();
    }

    void SketchDocument::setActiveItem(AppearanceKind kind, std::size_t index) {
        (void)kind;
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setActiveIndex(index);
            }
        }
        onChanged(*this);
    }

    void SketchDocument::setSelectionItemOpacity(AppearanceKind kind, double opacity) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                if (AppearanceItem* item = object->activeItemPtr(kind)) { item->opacity = std::min(1.0, std::max(0.0, opacity)); }
            }
        }
        changed();
    }

    void SketchDocument::setSelectionItemBlendMode(AppearanceKind kind, BlendMode mode) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                if (AppearanceItem* item = object->activeItemPtr(kind)) { item->blendMode = mode; }
            }
        }
        changed();
    }

    void SketchDocument::setSelectionItemVisible(AppearanceKind kind, bool visible) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                if (AppearanceItem* item = object->activeItemPtr(kind)) { item->visible = visible; }
            }
        }
        changed();
    }

    bool SketchDocument::addAppearanceItem(AppearanceKind kind) {
        std::vector<StyleState> before = snapshotStyle();
        if (before.empty()) {
            return false;
        }
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                std::vector<AppearanceItem> items = object->appearance();
                if (kind == AppearanceKind::Stroke) {
                    SketchPaint paint = defaultStroke_.isNone() ? SketchPaint::solid(SketchColor::gray(0.0f)) : defaultStroke_;
                    items.push_back(AppearanceItem::makeStroke(paint, defaultStrokeWidth_ > 0.0 ? defaultStrokeWidth_ : 1.0, defaultStrokeOptions_));
                } else {
                    items.push_back(AppearanceItem::makeFill(defaultFill_.isNone() ? SketchPaint::solid(SketchColor::gray(0.85f)) : defaultFill_));
                }
                object->setAppearance(items);
                object->setActiveIndex(items.size() - 1);
            }
        }
        changed();
        return commitStyle(before, kind == AppearanceKind::Stroke ? "Add Stroke" : "Add Fill");
    }

    std::vector<GraphicStyle> defaultGraphicStyles() {
        auto style = [](const char* name, std::vector<AppearanceItem> items, double opacity = 1.0) {
            GraphicStyle s;
            s.name = name;
            s.items = std::move(items);
            s.opacity = opacity;
            return s;
        };
        auto solid = [](const char* hex) { return SketchPaint::solid(SketchColor::fromHex(hex)); };
        auto gradient = [](const char* from, const char* to, double angle) {
            SketchGradient g;
            g.stops = {GradientStop{0.0, SketchColor::fromHex(from)}, GradientStop{1.0, SketchColor::fromHex(to)}};
            g.angleDegrees = angle;
            return g;
        };
        std::vector<GraphicStyle> styles;
        styles.push_back(style("Default", {AppearanceItem::makeFill(solid("#ffffff")), AppearanceItem::makeStroke(solid("#000000"), 1.0)}));
        AppearanceItem shadow = AppearanceItem::makeEffect(EffectKind::DropShadow);
        styles.push_back(style("Sunset + shadow", {AppearanceItem::makeFill(SketchPaint::linear(gradient("#f2b824", "#e8452c", 45.0))),
            AppearanceItem::makeStroke(SketchPaint::none(), 1.0), shadow}));
        styles.push_back(style("Blue card", {AppearanceItem::makeFill(solid("#0078d4")), AppearanceItem::makeStroke(solid("#1b3a6b"), 3.0)}));
        SketchGradient glowFill = gradient("#ffffff", "#00aeef", 0.0);
        styles.push_back(style("Glow", {AppearanceItem::makeFill(SketchPaint::radial(glowFill)), AppearanceItem::makeStroke(SketchPaint::none(), 1.0),
            AppearanceItem::makeEffect(EffectKind::OuterGlow)}));
        StrokeOptions dashed;
        dashed.dashes = {8.0, 4.0};
        styles.push_back(style("Dashed red", {AppearanceItem::makeFill(solid("#ffffff")), AppearanceItem::makeStroke(solid("#c0392b"), 3.0, dashed)}));
        styles.push_back(style("Ghost", {AppearanceItem::makeFill(solid("#6a4fc7")), AppearanceItem::makeStroke(SketchPaint::none(), 1.0)}, 0.55));
        return styles;
    }

    bool SketchDocument::addGraphicStyleFromSelection() {
        const SketchObject* source = selection_.empty() ? nullptr : findById(selection_.front());
        if (source == nullptr) {
            return false;
        }
        std::vector<GraphicStyle> before = graphicStyles_;
        GraphicStyle style;
        style.items = source->appearance();
        style.opacity = source->opacity();
        style.blendMode = source->blendMode();
        for (std::size_t number = before.size() + 1;; ++number) {
            style.name = "Style " + std::to_string(number);
            bool taken = false;
            for (const GraphicStyle& existing : before) {
                taken = taken || existing.name == style.name;
            }
            if (!taken) {
                break;
            }
        }
        std::vector<GraphicStyle> after = before;
        after.push_back(style);
        graphicStyles_ = after;
        changed();
        undoStack_.push({"New Graphic Style", [this, after]() { graphicStyles_ = after; changed(); }, [this, before]() { graphicStyles_ = before; changed(); }});
        return true;
    }

    bool SketchDocument::deleteGraphicStyle(std::size_t index) {
        if (index >= graphicStyles_.size()) {
            return false;
        }
        std::vector<GraphicStyle> before = graphicStyles_;
        std::vector<GraphicStyle> after = before;
        after.erase(after.begin() + std::ptrdiff_t(index));
        graphicStyles_ = after;
        changed();
        undoStack_.push({"Delete Graphic Style", [this, after]() { graphicStyles_ = after; changed(); }, [this, before]() { graphicStyles_ = before; changed(); }});
        return true;
    }

    bool SketchDocument::applyGraphicStyle(std::size_t index) {
        std::vector<StyleState> before = snapshotStyle();
        if (before.empty() || index >= graphicStyles_.size()) {
            return false;
        }
        const GraphicStyle& style = graphicStyles_[index];
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setAppearance(style.items);
                // The active rows are the top-most fill and stroke (what the panels then edit).
                std::size_t fill = 0, stroke = 0, effect = 0;
                const std::vector<AppearanceItem>& items = object->appearance();
                for (std::size_t i = 0; i < items.size(); ++i) {
                    (items[i].kind == AppearanceKind::Fill ? fill : (items[i].kind == AppearanceKind::Stroke ? stroke : effect)) = i;
                }
                object->setActiveIndices(fill, stroke, effect);
                object->setOpacity(style.opacity);
                object->setBlendMode(style.blendMode);
            }
        }
        changed();
        return commitStyle(before, "Apply " + style.name);
    }

    bool SketchDocument::addEffect(EffectKind kind) {
        std::vector<StyleState> before = snapshotStyle();
        if (before.empty()) {
            return false;
        }
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                std::vector<AppearanceItem> items = object->appearance();
                items.push_back(AppearanceItem::makeEffect(kind));
                object->setAppearance(items);
                object->setActiveIndex(items.size() - 1);
            }
        }
        changed();
        return commitStyle(before, std::string("Add ") + effectName(kind));
    }

    bool SketchDocument::changeActiveEffect(const std::function<void(AppearanceItem&)>& change, const std::string& description) {
        std::vector<StyleState> before = snapshotStyle();
        if (before.empty()) {
            return false;
        }
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                if (AppearanceItem* item = object->activeItemPtr(AppearanceKind::Effect)) {
                    change(*item);
                }
            }
        }
        changed();
        return commitStyle(before, description);
    }

    bool SketchDocument::duplicateActiveItem(AppearanceKind kind) {
        std::vector<StyleState> before = snapshotStyle();
        if (before.empty()) {
            return false;
        }
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                std::vector<AppearanceItem> items = object->appearance();
                std::size_t index = object->activeIndex(kind);
                if (index >= items.size()) {
                    continue;
                }
                items.insert(items.begin() + std::ptrdiff_t(index) + 1, items[index]);
                object->setAppearance(items);
                object->setActiveIndex(index + 1);
            }
        }
        changed();
        return commitStyle(before, "Duplicate Item");
    }

    bool SketchDocument::deleteActiveItem(AppearanceKind kind) {
        std::vector<StyleState> before = snapshotStyle();
        if (before.empty()) {
            return false;
        }
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                std::vector<AppearanceItem> items = object->appearance();
                std::size_t index = object->activeIndex(kind);
                if (index >= items.size()) {
                    continue;
                }
                std::size_t sameKind = 0;
                for (const AppearanceItem& item : items) {
                    sameKind += item.kind == kind ? 1 : 0;
                }
                if (sameKind <= 1 && kind != AppearanceKind::Effect) {
                    items[index].paint = SketchPaint::none();  // there is always one fill and one stroke
                    object->setAppearance(items);
                    continue;
                }
                items.erase(items.begin() + std::ptrdiff_t(index));
                object->setAppearance(items);
                // The nearest item of the same kind below (else above) takes over as the active one.
                std::size_t next = items.size();
                for (std::size_t i = std::min(index, items.size()); i-- > 0;) {
                    if (items[i].kind == kind) { next = i; break; }
                }
                for (std::size_t i = index; next == items.size() && i < items.size(); ++i) {
                    if (items[i].kind == kind) { next = i; }
                }
                object->setActiveIndex(next);
            }
        }
        changed();
        return commitStyle(before, "Delete Item");
    }

    bool SketchDocument::moveActiveItem(AppearanceKind kind, int delta) {
        std::vector<StyleState> before = snapshotStyle();
        if (before.empty() || delta == 0) {
            return false;
        }
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                std::vector<AppearanceItem> items = object->appearance();
                std::size_t index = object->activeIndex(kind);
                long target = long(index) + (delta > 0 ? 1 : -1);
                if (index >= items.size() || target < 0 || target >= long(items.size())) {
                    continue;
                }
                std::swap(items[index], items[std::size_t(target)]);
                object->setAppearance(items);
                object->setActiveIndex(std::size_t(target));
            }
        }
        changed();
        return commitStyle(before, delta > 0 ? "Move Item Up" : "Move Item Down");
    }

    bool SketchDocument::clearAppearance() {
        std::vector<StyleState> before = snapshotStyle();
        if (before.empty()) {
            return false;
        }
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                AppearanceItem fill = AppearanceItem::makeFill(SketchPaint::none());
                AppearanceItem stroke = AppearanceItem::makeStroke(SketchPaint::none(), 1.0);
                object->setAppearance({fill, stroke});
                object->setActiveIndices(0, 1);
                object->setOpacity(1.0);
                object->setBlendMode(BlendMode::Normal);
            }
        }
        changed();
        return commitStyle(before, "Clear Appearance");
    }

    void SketchDocument::changeSelectionStrokeOptions(const std::function<void(StrokeOptions&)>& change) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                StrokeOptions options = object->strokeOptions();
                change(options);
                object->setStrokeOptions(options);
            }
        }
        changed();
    }

    void SketchDocument::setDefaultStrokeOptions(const StrokeOptions& options) {
        defaultStrokeOptions_ = options;
        onChanged(*this);
    }

    bool SketchDocument::commitStyle(const std::vector<StyleState>& before, const std::string& description) {
        std::vector<StyleState> after;
        bool differs = false;
        for (const StyleState& old : before) {
            const SketchObject* object = findById(old.id);
            if (object == nullptr) {
                continue;
            }
            StyleState now = withGroup(StyleState::of(*object));
            differs = differs || now != old;
            after.push_back(now);
        }
        if (!differs) {
            return false;
        }
        auto apply = [this](const std::vector<StyleState>& states) {
            for (const StyleState& state : states) {
                if (SketchObject* object = findById(state.id)) {
                    state.applyTo(*object);
                }
                if (state.group != 0) {
                    if (state.groupProps.isDefault()) { groupProps_.erase(state.group); } else { groupProps_[state.group] = state.groupProps; }
                }
            }
            changed();
        };
        undoStack_.push({description,
            [apply, after]() { apply(after); },
            [apply, before]() { apply(before); }});
        return true;
    }

    namespace {
        SketchPaint paintInSpace(const SketchPaint& paint, ColorSpace space) {
            if (paint.isNone()) {
                return paint;
            }
            if (!paint.isGradient()) {
                return SketchPaint::solid(paint.color().convertedTo(space));
            }
            SketchGradient gradient = paint.gradient();
            for (GradientStop& stop : gradient.stops) {
                stop.color = stop.color.convertedTo(space);
            }
            return paint.kind() == SketchPaint::Kind::Radial ? SketchPaint::radial(gradient) : SketchPaint::linear(gradient);
        }
    }

    void SketchDocument::setColorMode(ColorSpace mode) {
        if (mode != ColorSpace::RGB && mode != ColorSpace::CMYK) {
            throw std::runtime_error("SketchDocument::setColorMode: a document is RGB or CMYK");
        }
        colorMode_ = mode;
        defaultFill_ = paintInSpace(defaultFill_, mode);
        defaultStroke_ = paintInSpace(defaultStroke_, mode);
        for (GraphicStyle& style : graphicStyles_) {
            for (AppearanceItem& item : style.items) {
                item.paint = paintInSpace(item.paint, mode);
            }
        }
        changed();
    }

    bool SketchDocument::convertToColorMode(ColorSpace mode) {
        if (mode != ColorSpace::RGB && mode != ColorSpace::CMYK) {
            throw std::runtime_error("SketchDocument::convertToColorMode: a document is RGB or CMYK");
        }
        if (mode == colorMode_) {
            return false;
        }
        // Everything the conversion touches, before and after.
        struct State {
            ColorSpace mode;
            SketchPaint defaultFill, defaultStroke;
            std::vector<StyleState> styles;
            std::vector<GraphicStyle> graphicStyles;
        };
        auto capture = [this]() {
            State state{colorMode_, defaultFill_, defaultStroke_, {}, graphicStyles_};
            for (const std::unique_ptr<SketchObject>& object : objects_) {
                state.styles.push_back(StyleState::of(*object));
            }
            return state;
        };
        State before = capture();
        colorMode_ = mode;
        defaultFill_ = paintInSpace(defaultFill_, mode);
        defaultStroke_ = paintInSpace(defaultStroke_, mode);
        for (GraphicStyle& style : graphicStyles_) {
            for (AppearanceItem& item : style.items) {
                item.paint = paintInSpace(item.paint, mode);
            }
        }
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            std::vector<AppearanceItem> items = object->appearance();
            for (AppearanceItem& item : items) {
                item.paint = paintInSpace(item.paint, mode);
            }
            object->setAppearance(items);
        }
        State after = capture();
        auto apply = [this](const State& state) {
            colorMode_ = state.mode;
            defaultFill_ = state.defaultFill;
            defaultStroke_ = state.defaultStroke;
            graphicStyles_ = state.graphicStyles;
            for (const StyleState& style : state.styles) {
                if (SketchObject* object = findById(style.id)) {
                    style.applyTo(*object);
                }
            }
            changed();
        };
        undoStack_.push({mode == ColorSpace::CMYK ? "Convert to CMYK" : "Convert to RGB",
            [apply, after]() { apply(after); },
            [apply, before]() { apply(before); }});
        changed();
        return true;
    }

    void SketchDocument::setDefaultFill(const SketchPaint& paint) {
        defaultFill_ = paint;
        onChanged(*this);
    }

    void SketchDocument::setDefaultStroke(const SketchPaint& paint) {
        defaultStroke_ = paint;
        onChanged(*this);
    }

    void SketchDocument::setDefaultStrokeWidth(double width) {
        defaultStrokeWidth_ = width < 0.0 ? 0.0 : width;
        onChanged(*this);
    }

}
