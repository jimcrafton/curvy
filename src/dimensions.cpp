#include "curvy/dimensions.h"

#include "curvy/objectnode.h"
#include "curvy/shapegeometry.h"

#include <algorithm>
#include <cmath>

namespace curvy {

    namespace {

        constexpr double kEpsilon = 1e-9;
        constexpr double kDegrees = 3.14159265358979323846 / 180.0;

        double pathLength(const BLPath& path) {
            const BLPathView view = path.view();
            double total = 0.0;
            BLPoint current(0.0, 0.0), start(0.0, 0.0);
            const auto line = [&](const BLPoint& to) {
                total += std::hypot(to.x - current.x, to.y - current.y);
                current = to;
            };
            const auto cubic = [&](const BLPoint& p1, const BLPoint& p2, const BLPoint& p3) {
                constexpr int steps = 24;
                const BLPoint p0 = current;
                for (int i = 1; i <= steps; ++i) {
                    const double t = double(i) / steps, u = 1.0 - t;
                    line(BLPoint(u * u * u * p0.x + 3.0 * u * u * t * p1.x + 3.0 * u * t * t * p2.x + t * t * t * p3.x,
                                 u * u * u * p0.y + 3.0 * u * u * t * p1.y + 3.0 * u * t * t * p2.y + t * t * t * p3.y));
                }
            };
            for (std::size_t i = 0; i < view.size; ++i) {
                const BLPoint p = view.vertex_data[i];
                switch (view.command_data[i]) {
                    case BL_PATH_CMD_MOVE:
                        current = start = p;
                        break;
                    case BL_PATH_CMD_ON:
                        line(p);
                        break;
                    case BL_PATH_CMD_QUAD:
                        if (i + 1 < view.size) {
                            const BLPoint e = view.vertex_data[i + 1];
                            cubic(BLPoint(current.x + 2.0 / 3.0 * (p.x - current.x), current.y + 2.0 / 3.0 * (p.y - current.y)),
                                  BLPoint(e.x + 2.0 / 3.0 * (p.x - e.x), e.y + 2.0 / 3.0 * (p.y - e.y)), e);
                            ++i;
                        }
                        break;
                    case BL_PATH_CMD_CUBIC:
                        if (i + 2 < view.size) {
                            cubic(p, view.vertex_data[i + 1], view.vertex_data[i + 2]);
                            i += 2;
                        }
                        break;
                    case BL_PATH_CMD_CLOSE:
                        line(start);
                        break;
                    default:
                        break;
                }
            }
            return total;
        }

    }

    const char* DimensionModel::name(DimensionItem::Kind kind) {
        switch (kind) {
            case DimensionItem::Kind::Width:
                return "width";
            case DimensionItem::Kind::Height:
                return "height";
            case DimensionItem::Kind::Radius:
                return "radius";
            case DimensionItem::Kind::CurveLength:
                return "length";
        }
        return "";
    }

    std::optional<DimensionItem::Kind> DimensionModel::kindFromName(const std::string& text) {
        for (DimensionItem::Kind kind : {DimensionItem::Kind::Width, DimensionItem::Kind::Height, DimensionItem::Kind::Radius, DimensionItem::Kind::CurveLength}) {
            if (text == name(kind)) {
                return kind;
            }
        }
        return std::nullopt;
    }

    DimensionModel::Affine DimensionModel::multiply(const Affine& l, const Affine& r) {
        Affine m;
        m.a = l.a * r.a + l.c * r.b;
        m.b = l.b * r.a + l.d * r.b;
        m.c = l.a * r.c + l.c * r.d;
        m.d = l.b * r.c + l.d * r.d;
        m.e = l.a * r.e + l.c * r.f + l.e;
        m.f = l.b * r.e + l.d * r.f + l.f;
        return m;
    }

    bool DimensionModel::invert(const Affine& m, Affine& out) {
        const double det = m.a * m.d - m.b * m.c;
        if (std::fabs(det) < kEpsilon) {
            return false;
        }
        out.a = m.d / det;
        out.b = -m.b / det;
        out.c = -m.c / det;
        out.d = m.a / det;
        out.e = -(out.a * m.e + out.c * m.f);
        out.f = -(out.b * m.e + out.d * m.f);
        return true;
    }

    // The matrix the transform node applies (see TransformNode in jitnodes: p + t + R Sk S (v - p)).
    DimensionModel::Affine DimensionModel::fromNode(const jitnodes::Node& node) {
        const double sx = node.getFloat("sx"), sy = node.getFloat("sy");
        const double px = node.getFloat("px"), py = node.getFloat("py");
        const double r = double(node.getFloat("rot")) * kDegrees;
        const double k = std::tan(double(node.getFloat("skew")) * kDegrees);
        const double cs = std::cos(r), sn = std::sin(r);
        Affine m;
        m.a = cs * sx;
        m.b = sn * sx;
        m.c = cs * k * sy - sn * sy;
        m.d = sn * k * sy + cs * sy;
        m.e = px + node.getFloat("tx") - m.a * px - m.c * py;
        m.f = py + node.getFloat("ty") - m.b * px - m.d * py;
        return m;
    }

    bool DimensionModel::refuse(const std::string& text) {
        message_ = text;
        return false;
    }

    bool DimensionModel::bounds(BLBox& box) {
        const jitnodes::GeometryHandle artwork = selection_.empty() ? nullptr : document_.artwork();
        if (artwork == nullptr) {
            return false;
        }
        bool any = false;
        const ShapeGeometry shapeGeometry(*artwork);
        for (const jitnodes::ShapeRange& shape : artwork->shapes()) {
            if (shape.key.empty() || !selection_.isShapeKeySelected(std::string(shape.key))) {
                continue;
            }
            const BLPath path = shapeGeometry.path(shape, 0.0, 0.0, 1.0);
            BLBox one;
            if (path.is_empty() || path.get_bounding_box(&one) != BL_SUCCESS) {
                continue;
            }
            if (!any) {
                box = one;
                any = true;
            } else {
                box.x0 = (std::min)(box.x0, one.x0);
                box.y0 = (std::min)(box.y0, one.y0);
                box.x1 = (std::max)(box.x1, one.x1);
                box.y1 = (std::max)(box.y1, one.y1);
            }
        }
        return any;
    }

    NodeRef DimensionModel::singleShape() {
        const jitnodes::GeometryHandle artwork = selection_.empty() ? nullptr : document_.artwork();
        if (artwork == nullptr) {
            return NodeRef();
        }
        std::string key;
        int count = 0;
        for (const jitnodes::ShapeRange& shape : artwork->shapes()) {
            if (!shape.key.empty() && selection_.isShapeKeySelected(std::string(shape.key))) {
                key = std::string(shape.key);
                ++count;
            }
        }
        return count == 1 ? document_.nodeForShapeKey(key) : NodeRef();
    }

    std::vector<DimensionItem> DimensionModel::compute() {
        std::vector<DimensionItem> items;
        BLBox box;
        if (!bounds(box)) {
            return items;
        }
        const double w = box.x1 - box.x0, h = box.y1 - box.y0;
        items.push_back({DimensionItem::Kind::Width, w, BLPoint((box.x0 + box.x1) * 0.5, box.y1), w > kEpsilon});
        items.push_back({DimensionItem::Kind::Height, h, BLPoint(box.x1, (box.y0 + box.y1) * 0.5), h > kEpsilon});
        const NodeRef single = singleShape();
        if (!single.valid() || !document_.hasNode(single)) {
            return items;
        }
        if (document_.node(single).typeName() == "circle") {
            if (std::fabs(w - h) <= 1e-3 * (std::max)(w, h) && w > kEpsilon) {
                items.push_back({DimensionItem::Kind::Radius, w * 0.5, BLPoint((box.x0 + box.x1) * 0.5, (box.y0 + box.y1) * 0.5), true});
            }
            return items;
        }
        const jitnodes::GeometryHandle artwork = document_.artwork();
        double length = 0.0;
        const ShapeGeometry shapeGeometry(*artwork);
        for (const jitnodes::ShapeRange& shape : artwork->shapes()) {
            if (!shape.key.empty() && selection_.isShapeKeySelected(std::string(shape.key))) {
                length += pathLength(shapeGeometry.path(shape, 0.0, 0.0, 1.0));
            }
        }
        items.push_back({DimensionItem::Kind::CurveLength, length, BLPoint(box.x0, box.y0), false});
        return items;
    }

    std::vector<NodeRef> DimensionModel::editableNodes() const {
        std::vector<NodeRef> nodes;
        for (const NodeRef& item : selection_.items()) {
            if (!document_.hasNode(item)) {
                continue;
            }
            bool covered = false;   // a group and one of its members both selected: the group's transform moves both
            for (NodeRef up = selection_.enclosing(item); up.valid(); up = selection_.enclosing(up)) {
                if (selection_.contains(up)) {
                    covered = true;
                    break;
                }
            }
            if (!covered && std::find(nodes.begin(), nodes.end(), item) == nodes.end()) {
                nodes.push_back(item);
            }
        }
        return nodes;
    }

    DimensionModel::Affine DimensionModel::aboveOf(const NodeRef& object) const {
        Affine total;
        for (NodeRef up = object; up.valid(); up = selection_.enclosing(up)) {
            const NodeRef xform = selection_.find(SelectionModel::Role::ObjectXform, up);
            if (xform.valid() && document_.hasNode(xform)) {
                total = multiply(fromNode(document_.node(xform)), total);
            }
        }
        return total;
    }

    bool DimensionModel::begin(DimensionItem::Kind kind) {
        if (editing_) {
            cancel();
        }
        message_.clear();
        const std::vector<DimensionItem> items = compute();
        const auto item = std::find_if(items.begin(), items.end(), [kind](const DimensionItem& i) { return i.kind == kind; });
        if (item == items.end() || !item->editable) {
            return refuse(item == items.end() ? "nothing to measure" : "this measurement cannot be edited");
        }
        if (!bounds(box_)) {
            return refuse("nothing selected");
        }
        if (!open(editableNodes())) {
            return false;
        }
        kind_ = kind;
        pivotMove_ = false;
        return true;
    }

    BLPoint DimensionModel::through(const Affine& m, double x, double y) {
        return BLPoint(m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f);
    }

    bool DimensionModel::open(const std::vector<NodeRef>& nodes) {
        before_ = document_.snapshotGraph();
        targets_.clear();
        try {
            for (const NodeRef& node : nodes) {
                Target target;
                NodeRef above;
                if (dynamic_cast<const ObjectNode*>(&document_.node(node)) != nullptr) {
                    target.xform = selection_.find(SelectionModel::Role::ObjectXform, node);
                    above = selection_.enclosing(node);
                    document_.centrePivotIfIdentity(node);
                } else if (selection_.find(SelectionModel::Role::Shape, node).valid()) {
                    target.xform = document_.ensureLocalTransformLive(node);
                    above = selection_.find(SelectionModel::Role::Object, node);
                    document_.centrePivotIfIdentity(node);
                }
                if (!target.xform.valid() || !document_.hasNode(target.xform)) {
                    continue;
                }
                target.above = aboveOf(above);
                target.original = fromNode(document_.node(target.xform));
                target.px = document_.node(target.xform).getFloat("px");
                target.py = document_.node(target.xform).getFloat("py");
                targets_.push_back(target);
            }
        } catch (const std::exception& error) {
            document_.restoreGraph(*before_);
            before_.reset();
            return refuse(error.what());
        }
        if (targets_.empty()) {
            document_.restoreGraph(*before_);
            before_.reset();
            return refuse("the selection has no transform to change");
        }
        editing_ = true;
        previewed_ = false;
        return true;
    }

    std::vector<DimensionModel::PivotMark> DimensionModel::pivots() {
        std::vector<PivotMark> marks;
        if (selection_.empty()) {
            return marks;
        }
        const auto add = [&](PivotMark::Level level, const NodeRef& owner, const NodeRef& xform, const Affine& above, double px, double py) {
            for (const PivotMark& existing : marks) {
                if (xform.valid() && existing.xform == xform) {
                    return;
                }
            }
            PivotMark mark;
            mark.level = level;
            mark.owner = owner;
            mark.xform = xform;
            mark.at = through(above, px, py);
            marks.push_back(mark);
        };
        // A transform that does nothing yet has its pivot where it will be put on first use (SketchDocument::centrePivotIfIdentity).
        const auto pivotOf = [&](const NodeRef& owner, const jitnodes::Node& node, double& px, double& py) {
            px = node.getFloat("px");
            py = node.getFloat("py");
            const Affine m = fromNode(node);
            const bool identity = px == 0.0 && py == 0.0 && m.a == 1.0 && m.b == 0.0 && m.c == 0.0 && m.d == 1.0 && m.e == 0.0 && m.f == 0.0;
            if (identity) {
                if (const auto centre = document_.defaultPivot(owner)) {
                    px = centre->first;
                    py = centre->second;
                }
            }
        };
        const auto addObject = [&](const NodeRef& object) {
            const NodeRef xform = selection_.find(SelectionModel::Role::ObjectXform, object);
            if (xform.valid() && document_.hasNode(xform)) {
                double px = 0.0, py = 0.0;
                pivotOf(object, document_.node(xform), px, py);
                add(PivotMark::Level::Object, object, xform, multiply(aboveOf(selection_.enclosing(object)), fromNode(document_.node(xform))), px, py);
            }
        };
        for (const NodeRef& item : editableNodes()) {
            if (dynamic_cast<const ObjectNode*>(&document_.node(item)) != nullptr) {
                addObject(item);
                continue;
            }
            const NodeRef object = selection_.find(SelectionModel::Role::Object, item);
            if (!object.valid()) {
                continue;
            }
            const NodeRef local = selection_.find(SelectionModel::Role::LocalTransform, item);
            if (local.valid() && document_.hasNode(local)) {
                double px = 0.0, py = 0.0;
                pivotOf(item, document_.node(local), px, py);
                add(PivotMark::Level::Shape, item, local, multiply(aboveOf(object), fromNode(document_.node(local))), px, py);
            } else if (const auto centre = document_.defaultPivot(item)) {
                PivotMark mark;   // no local transform yet: where its pivot will be once it has one
                mark.level = PivotMark::Level::Shape;
                mark.owner = item;
                mark.at = through(aboveOf(object), centre->first, centre->second);
                marks.push_back(mark);
            }
            addObject(object);
        }
        return marks;
    }

    bool DimensionModel::beginPivotMove(const PivotMark& mark) {
        if (editing_) {
            cancel();
        }
        message_.clear();
        if (!document_.hasNode(mark.owner) || !open({mark.owner})) {
            return false;
        }
        Target& target = targets_.front();
        if (mark.level == PivotMark::Level::Object && target.xform != mark.xform) {
            cancel();
            return refuse("the pivot no longer belongs to the selection");
        }
        pivotMove_ = true;
        return true;
    }

    bool DimensionModel::previewPivotMove(const BLPoint& at) {
        if (!editing_ || !pivotMove_ || targets_.empty()) {
            return false;
        }
        const Target& target = targets_.front();
        Affine inverseAbove, inverseNode;
        if (!invert(target.above, inverseAbove) || !invert(target.original, inverseNode)) {
            return refuse("the transform is flattened to nothing");
        }
        // The pivot is the point of the shape that stays put while the transform's rotation and scale change: it sits where the transform shows
        // the input point (px, py), at (px + tx, py + ty). So the input point to use is the one that shows at `at`, and the matrix stays as it is.
        const BLPoint shown = through(inverseAbove, at.x, at.y);
        const BLPoint input = through(inverseNode, shown.x, shown.y);
        document_.setGraphFloatLive(target.xform, "px", float(input.x));
        document_.setGraphFloatLive(target.xform, "py", float(input.y));
        document_.setGraphFloatLive(target.xform, "tx", float(shown.x - input.x));
        document_.setGraphFloatLive(target.xform, "ty", float(shown.y - input.y));
        previewed_ = true;
        return true;
    }

    bool DimensionModel::preview(double value, bool aboutPivot) {
        if (!editing_ || pivotMove_) {
            return false;
        }
        if (!std::isfinite(value) || value <= 0.0) {
            return refuse("a size must be more than zero");
        }
        double kx = 1.0, ky = 1.0, ax = box_.x0, ay = box_.y0;
        const double w = box_.x1 - box_.x0, h = box_.y1 - box_.y0;
        switch (kind_) {
            case DimensionItem::Kind::Width:
                kx = value / w;
                break;
            case DimensionItem::Kind::Height:
                ky = value / h;
                break;
            case DimensionItem::Kind::Radius:
                kx = ky = value / (w * 0.5);
                ax = (box_.x0 + box_.x1) * 0.5;
                ay = (box_.y0 + box_.y1) * 0.5;
                break;
            case DimensionItem::Kind::CurveLength:
                return refuse("this measurement cannot be edited");
        }
        if (aboutPivot) {
            double sumX = 0.0, sumY = 0.0;
            for (const Target& target : targets_) {
                const BLPoint at = through(multiply(target.above, target.original), target.px, target.py);
                sumX += at.x;
                sumY += at.y;
            }
            ax = sumX / double(targets_.size());
            ay = sumY / double(targets_.size());
        }
        Affine scale;
        scale.a = kx;
        scale.d = ky;
        scale.e = ax * (1.0 - kx);
        scale.f = ay * (1.0 - ky);

        struct Result {
            float tx, ty, rot, sx, sy, skew;
        };
        std::vector<Result> results;
        for (const Target& target : targets_) {
            Affine inverse;
            if (!invert(target.above, inverse)) {
                return refuse("an object around the selection is flattened to nothing");
            }
            const Affine m = multiply(inverse, multiply(scale, multiply(target.above, target.original)));
            const double sx = std::hypot(m.a, m.b);
            if (sx < kEpsilon) {
                return refuse("the result would be flattened to nothing");
            }
            const double cs = m.a / sx, sn = m.b / sx;
            const double sy = -sn * m.c + cs * m.d;
            if (std::fabs(sy) < kEpsilon) {
                return refuse("the result would be flattened to nothing");
            }
            const double k = (cs * m.c + sn * m.d) / sy;
            Result r;
            r.sx = float(sx);
            r.sy = float(sy);
            r.rot = float(std::atan2(m.b, m.a) / kDegrees);
            r.skew = float(std::atan(k) / kDegrees);
            r.tx = float(m.e - target.px + m.a * target.px + m.c * target.py);
            r.ty = float(m.f - target.py + m.b * target.px + m.d * target.py);
            results.push_back(r);
        }
        for (std::size_t i = 0; i < targets_.size(); ++i) {
            const NodeRef& xform = targets_[i].xform;
            const Result& r = results[i];
            document_.setGraphFloatLive(xform, "sx", r.sx);
            document_.setGraphFloatLive(xform, "sy", r.sy);
            document_.setGraphFloatLive(xform, "rot", r.rot);
            document_.setGraphFloatLive(xform, "skew", r.skew);
            document_.setGraphFloatLive(xform, "tx", r.tx);
            document_.setGraphFloatLive(xform, "ty", r.ty);
        }
        previewed_ = true;
        return true;
    }

    bool DimensionModel::commit() {
        if (!editing_) {
            return false;
        }
        const bool changed = previewed_;
        if (changed) {
            document_.commitGraphEdit(*before_, pivotMove_ ? std::string("Move Pivot") : std::string("Change ") + name(kind_));
            editing_ = false;
            before_.reset();
            targets_.clear();
        } else {
            cancel();
        }
        return changed;
    }

    void DimensionModel::cancel() {
        if (!editing_) {
            return;
        }
        document_.restoreGraph(*before_);
        editing_ = false;
        before_.reset();
        targets_.clear();
    }

    bool DimensionModel::apply(DimensionItem::Kind kind, double value, bool aboutPivot) {
        if (!begin(kind)) {
            return false;
        }
        if (!preview(value, aboutPivot)) {
            cancel();
            return false;
        }
        return commit();
    }

}
