#include "curvy/pathnode.h"

#include "curvy/appearancenode.h"
#include "curvy/sketchobject.h"

#include <jitnodes/graph/network.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <sstream>
#include <mutex>

namespace curvy {

    namespace {
        // Six significant digits, the precision the SVG and .skb files keep: the node never claims more than a saved document holds.
        // Objects are only rebuilt from a node whose text differs from theirs at this precision, so undo does not round their curves.
        void appendNumber(std::string& out, double value) {
            if (std::fabs(value) < 1e-9) {
                out += '0';
                return;
            }
            const int length = std::snprintf(nullptr, 0, "%.6g", value);
            const std::size_t at = out.size();
            out.resize(at + std::size_t(length) + 1);   // snprintf writes the terminator too
            std::snprintf(&out[at], std::size_t(length) + 1, "%.6g", value);
            out.resize(at + std::size_t(length));
        }

        void appendPoint(std::string& out, const BLPoint& point) {
            appendNumber(out, point.x);
            out += ' ';
            appendNumber(out, point.y);
        }

        // Files keep fewer digits than the document works in, so a figure that was closed can come back with its end a hair off its start.
        bool sameSpot(const BLPoint& a, const BLPoint& b) {
            const double slack = 1e-4 * (1.0 + std::max({std::fabs(a.x), std::fabs(a.y), std::fabs(b.x), std::fabs(b.y)}));
            return std::fabs(a.x - b.x) < slack && std::fabs(a.y - b.y) < slack;
        }

        // One primitive per sub path. A figure of straight lines is a polyline (its anchors); any other is a cubic: anchor, out handle, in handle,
        // anchor ..., lines carried as cubics with their handles on the anchors and quads raised to cubics (exact). A closed figure ends on
        // its first anchor without repeating it.
        void fillFigures(jitnodes::Geometry& geometry, const BLPath& path) {
            struct Figure {
                std::vector<BLPoint> points;   // anchor, then (out handle, in handle, anchor) per segment
                bool closed = false;
                bool curved = false;
            };
            std::vector<Figure> figures;
            const std::uint8_t* command = path.command_data();
            const BLPoint* vertex = path.vertex_data();
            BLPoint start(0, 0), last(0, 0);
            bool open = false;
            auto begin = [&](const BLPoint& at) {
                figures.emplace_back();
                figures.back().points.push_back(at);
                start = last = at;
                open = true;
            };
            auto segment = [&](const BLPoint& c1, const BLPoint& c2, const BLPoint& end, bool curve) {
                if (!open) {
                    begin(last);
                }
                figures.back().points.insert(figures.back().points.end(), {c1, c2, end});
                figures.back().curved = figures.back().curved || curve;
                last = end;
            };
            for (std::size_t i = 0; i < path.size(); ++i) {
                switch (command[i]) {
                    case BL_PATH_CMD_MOVE: begin(vertex[i]); break;
                    case BL_PATH_CMD_ON: segment(last, vertex[i], vertex[i], false); break;
                    case BL_PATH_CMD_QUAD:
                        if (i + 1 < path.size()) {
                            const BLPoint q = vertex[i], end = vertex[i + 1];
                            segment(BLPoint(last.x + 2.0 / 3.0 * (q.x - last.x), last.y + 2.0 / 3.0 * (q.y - last.y)),
                                BLPoint(end.x + 2.0 / 3.0 * (q.x - end.x), end.y + 2.0 / 3.0 * (q.y - end.y)), end, true);
                        }
                        i += 1;
                        break;
                    case BL_PATH_CMD_CUBIC:
                        if (i + 2 < path.size()) {
                            segment(vertex[i], vertex[i + 1], vertex[i + 2], true);
                        }
                        i += 2;
                        break;
                    case BL_PATH_CMD_CLOSE:
                        if (open && figures.back().points.size() > 1) {
                            Figure& figure = figures.back();
                            if (!sameSpot(last, start)) {
                                figure.points.insert(figure.points.end(), {last, start, start});
                            }
                            figure.points.pop_back();   // the closing segment ends on the first anchor
                            figure.closed = true;
                        }
                        open = false;
                        last = start;
                        break;
                    default: break;
                }
            }
            for (const Figure& figure : figures) {
                if (figure.points.size() < 4) {
                    continue;
                }
                std::vector<BLPoint> points;
                if (figure.curved) {
                    points = figure.points;
                } else {
                    for (std::size_t k = 0; k < figure.points.size(); k += 3) {
                        points.push_back(figure.points[k]);
                    }
                }
                const std::uint32_t first = std::uint32_t(geometry.pointCount());
                geometry.resizePoints(first + points.size());
                float* x = geometry.x();
                float* y = geometry.y();
                for (std::size_t k = 0; k < points.size(); ++k) {
                    x[first + k] = float(points[k].x);
                    y[first + k] = float(points[k].y);
                }
                geometry.addPrimitive({first, std::uint32_t(points.size()), figure.closed,
                    figure.curved ? jitnodes::PrimKind::cubic : jitnodes::PrimKind::polyline});
            }
        }

        class PathNode : public jitnodes::Node {
        public:
            PathNode(const char* type, std::initializer_list<const char*> extras) : Node(type, 0) {
                declareGeometry("curve");
                for (const char* name : extras) {
                    declareString(name, "");
                }
            }

            // The authored curve goes out as it is: polyline and cubic primitives (jitnodes PrimKind); nodes that only know polylines flatten
            // curves first.
            jitnodes::GeometryHandle cook(jitnodes::CookContext&, const std::vector<jitnodes::GeometryHandle>&) override {
                return geometryParam("curve");
            }
        };
    }

    namespace {
        constexpr double kDegrees = 57.295779513082320876798;

        // The placement is translate * pivot * rotate * shear(x) * scale * pivot^-1, which can express any 2D affine matrix; a flip is a
        // negative scale. The pivot is where rotate, shear and scale hold still; translate moves the pivot itself.
        struct Placement {
            float translateX = 0, translateY = 0, rotation = 0, scaleX = 1, scaleY = 1, shear = 0, pivotX = 0, pivotY = 0;
        };

        Placement placementOf(const BLMatrix2D& m, float pivotX, float pivotY) {
            Placement p;
            p.pivotX = pivotX;
            p.pivotY = pivotY;
            double angle, sx, sy, k = 0.0;
            sx = std::hypot(m.m00, m.m01);
            if (sx > 1e-12) {
                angle = std::atan2(m.m01, m.m00);
                const double ux = m.m00 / sx, uy = m.m01 / sx;
                sy = ux * m.m11 - uy * m.m10;
                if (std::fabs(sy) > 1e-12) {
                    k = (ux * m.m10 + uy * m.m11) / sy;
                }
            } else {
                angle = std::atan2(-m.m10, m.m11);
                sy = std::hypot(m.m10, m.m11);
            }
            p.rotation = float(angle * kDegrees);
            p.scaleX = float(sx);
            p.scaleY = float(sy);
            p.shear = float(std::atan(k) * kDegrees);
            p.translateX = float(m.m20 - pivotX + m.m00 * pivotX + m.m10 * pivotY);
            p.translateY = float(m.m21 - pivotY + m.m01 * pivotX + m.m11 * pivotY);
            return p;
        }

        BLMatrix2D matrixOf(const Placement& p) {
            const double angle = double(p.rotation) / kDegrees;
            const double c = std::cos(angle), s = std::sin(angle), k = std::tan(double(p.shear) / kDegrees);
            const double sx = p.scaleX, sy = p.scaleY;
            const double m00 = c * sx, m01 = s * sx, m10 = (c * k - s) * sy, m11 = (s * k + c) * sy;
            return BLMatrix2D(m00, m01, m10, m11, p.translateX + p.pivotX - (m00 * p.pivotX + m10 * p.pivotY),
                p.translateY + p.pivotY - (m01 * p.pivotX + m11 * p.pivotY));
        }

        void movePoints(jitnodes::Geometry& geometry, const BLMatrix2D& matrix) {
            if (geometry.pointCount() > 0) {
                float* x = geometry.x();
                float* y = geometry.y();
                for (std::size_t i = 0; i < geometry.pointCount(); ++i) {
                    const BLPoint p = matrix.map_point(double(x[i]), double(y[i]));
                    x[i] = float(p.x);
                    y[i] = float(p.y);
                }
            }
        }

        constexpr const char* kWorldDetails[6] = {"w0", "w1", "w2", "w3", "w4", "w5"};

        // A transform node's output carries where its shape sits on the page (its own placement after its parent's), which is what a child's
        // node reads from its second input.
        BLMatrix2D worldFromDetails(const jitnodes::Geometry& geometry) {
            const BLMatrix2D identity = BLMatrix2D::make_identity();
            BLMatrix2D world;
            for (int i = 0; i < 6; ++i) {
                world.m[i] = double(geometry.detail(kWorldDetails[i], float(identity.m[i])));
            }
            return world;
        }

        class TransformNode : public jitnodes::Node {
        public:
            TransformNode() : Node(kTransformNodeType, 1) {
                const Placement none;
                declareFloat("translateX", none.translateX);
                declareFloat("translateY", none.translateY);
                declareFloat("rotation", none.rotation);
                declareFloat("scaleX", none.scaleX);
                declareFloat("scaleY", none.scaleY);
                declareFloat("shear", none.shear);
                declareFloat("pivotX", none.pivotX);
                declareFloat("pivotY", none.pivotY);
                declareString("paintWarp", "");
            }

            // Input 0 is the shape; input 1, when connected, is the parent's transform node.
            bool hasVariableInputs() const override { return true; }

            jitnodes::GeometryHandle cook(jitnodes::CookContext&, const std::vector<jitnodes::GeometryHandle>& inputs) override {
                if (inputs.empty() || !inputs.front()) {
                    return std::make_shared<jitnodes::Geometry>();
                }
                BLMatrix2D world = resolvedTransformOf(*this);
                if (inputs.size() > 1 && inputs[1]) {
                    world.post_transform(worldFromDetails(*inputs[1]));
                }
                auto placed = std::make_shared<jitnodes::Geometry>(*inputs.front());
                movePoints(*placed, world);
                for (int i = 0; i < 6; ++i) {
                    placed->setDetail(kWorldDetails[i], float(world.m[i]));
                }
                return placed;
            }
        };
    }

    namespace {
        class MovePointsNode : public jitnodes::Node {
        public:
            MovePointsNode() : Node(kMovePointsNodeType, 1) {
                const Placement none;
                declareFloat("translateX", none.translateX);
                declareFloat("translateY", none.translateY);
                declareFloat("rotation", none.rotation);
                declareFloat("scaleX", none.scaleX);
                declareFloat("scaleY", none.scaleY);
                declareFloat("shear", none.shear);
                declareFloat("pivotX", none.pivotX);
                declareFloat("pivotY", none.pivotY);
                declareString("group", "");
            }

            jitnodes::GeometryHandle cook(jitnodes::CookContext&, const std::vector<jitnodes::GeometryHandle>& inputs) override {
                if (inputs.empty() || !inputs.front()) {
                    return std::make_shared<jitnodes::Geometry>();
                }
                auto moved = std::make_shared<jitnodes::Geometry>(*inputs.front());
                const std::string& group = getString("group");
                const jitnodes::AttribArray* flags = group.empty() ? nullptr : moved->findAttrib(jitnodes::AttribClass::point, "group." + group);
                if (moved->pointCount() == 0 || (!group.empty() && flags == nullptr)) {
                    return moved;
                }
                const BLMatrix2D matrix = resolvedTransformOf(*this);
                float* x = moved->x();
                float* y = moved->y();
                for (std::size_t i = 0; i < moved->pointCount(); ++i) {
                    if (flags == nullptr || flags->ints()[i] != 0) {
                        const BLPoint p = matrix.map_point(double(x[i]), double(y[i]));
                        x[i] = float(p.x);
                        y[i] = float(p.y);
                    }
                }
                return moved;
            }
        };
    }

    namespace {
        // Which points of a primitive are anchors; a cubic's handles sit between them (out handle after its anchor, in handle before the next).
        bool isAnchor(const jitnodes::Primitive& prim, std::uint32_t local) {
            return prim.kind != jitnodes::PrimKind::cubic || local % 3 == 0;
        }

        // The anchor a handle belongs to, for the tag to follow.
        std::uint32_t anchorOf(const jitnodes::Primitive& prim, std::uint32_t local) {
            if (isAnchor(prim, local)) {
                return local;
            }
            return local % 3 == 1 ? local - 1 : (local + 1) % prim.count;
        }

        bool parseIds(const std::string& text, std::vector<std::pair<long, long>>& ranges) {
            std::string token;
            auto flush = [&]() {
                if (token.empty()) {
                    return;
                }
                char* end = nullptr;
                const long first = std::strtol(token.c_str(), &end, 10);
                long last = first;
                if (end != token.c_str() && *end == '-') {
                    last = std::strtol(end + 1, nullptr, 10);
                }
                ranges.push_back({std::min(first, last), std::max(first, last)});
                token.clear();
            };
            for (char c : text) {
                if (c == ',' || c == ' ') {
                    flush();
                } else {
                    token += c;
                }
            }
            flush();
            return !ranges.empty();
        }

        class TagNode : public jitnodes::Node {
        public:
            TagNode() : Node(kTagNodeType, 1) {
                declareString("group", "group1");
                declareString("ids", "");
                declareFloat("every", 0.0f);
                declareFloat("offset", 0.0f);
                declareString("box", "");
                declareString("kind", "any");
                declareString("operation", "replace");
            }

            jitnodes::GeometryHandle cook(jitnodes::CookContext&, const std::vector<jitnodes::GeometryHandle>& inputs) override {
                if (inputs.empty() || !inputs.front()) {
                    return std::make_shared<jitnodes::Geometry>();
                }
                const std::string& name = getString("group");
                if (name.empty()) {
                    return inputs.front();
                }
                auto tagged = std::make_shared<jitnodes::Geometry>(*inputs.front());
                const std::size_t count = tagged->pointCount();
                const jitnodes::AttribArray* idArray = tagged->findAttrib(jitnodes::AttribClass::point, kPointIdAttrib);
                std::vector<std::pair<long, long>> idRanges;
                const bool byIds = parseIds(getString("ids"), idRanges);
                const int every = int(std::lround(baseFloat("every"))), offset = int(std::lround(baseFloat("offset")));
                double box[4] = {0, 0, 0, 0};
                bool byBox = false;
                {
                    std::istringstream in(getString("box"));
                    byBox = bool(in >> box[0] >> box[1] >> box[2] >> box[3]) && box[2] > box[0] && box[3] > box[1];
                }
                const std::string& kind = getString("kind");

                std::vector<char> member(count, 0);
                auto matches = [&](std::size_t point, std::uint32_t anchorNumber) {
                    if (byIds) {
                        const long id = idArray != nullptr && idArray->type() == jitnodes::AttribType::int32 ? long(idArray->ints()[point]) : long(point);
                        for (const auto& range : idRanges) {
                            if (id >= range.first && id <= range.second) {
                                return true;
                            }
                        }
                    }
                    if (every > 0 && int(anchorNumber) >= offset && (int(anchorNumber) - offset) % every == 0) {
                        return true;
                    }
                    return byBox && tagged->x()[point] >= box[0] && tagged->x()[point] <= box[2] && tagged->y()[point] >= box[1] && tagged->y()[point] <= box[3];
                };
                auto sameSpot = [&](std::size_t a, std::size_t b) { return tagged->x()[a] == tagged->x()[b] && tagged->y()[a] == tagged->y()[b]; };
                auto isCorner = [&](const jitnodes::Primitive& prim, std::uint32_t local) {
                    if (prim.kind != jitnodes::PrimKind::cubic) {
                        return true;
                    }
                    const std::size_t at = prim.start + local;
                    const bool outFlat = local + 1 >= prim.count && !prim.closed ? true : sameSpot(at, prim.start + (local + 1) % prim.count);
                    const bool inFlat = local == 0 && !prim.closed ? true : sameSpot(at, prim.start + (local + prim.count - 1) % prim.count);
                    return outFlat && inFlat;
                };

                std::vector<char> covered(count, 0);
                for (const jitnodes::Primitive& prim : tagged->primitives()) {
                    if (std::size_t(prim.start) + prim.count > count) {
                        continue;
                    }
                    std::uint32_t anchorNumber = 0;
                    std::vector<char> matched(prim.count, 0);   // by anchor: did a selector pick it
                    for (std::uint32_t local = 0; local < prim.count; ++local) {
                        covered[prim.start + local] = 1;
                        if (!isAnchor(prim, local)) {
                            continue;
                        }
                        const bool in = matches(prim.start + local, anchorNumber++);
                        matched[local] = in ? 1 : 0;
                        const bool kept = kind == "handles" ? false : kind == "corners" ? in && isCorner(prim, local) : kind == "smooth" ? in && !isCorner(prim, local) : in;
                        member[prim.start + local] = kept ? 1 : 0;
                    }
                    const bool handlesFollow = kind != "anchors" && kind != "corners";
                    for (std::uint32_t local = 0; local < prim.count; ++local) {
                        if (!isAnchor(prim, local)) {
                            const std::uint32_t owner = anchorOf(prim, local);
                            const bool ownerIn = kind == "smooth" ? member[prim.start + owner] != 0 : matched[owner] != 0;
                            member[prim.start + local] = handlesFollow && ownerIn ? 1 : 0;
                        }
                    }
                }
                for (std::size_t i = 0; i < count; ++i) {
                    if (!covered[i]) {
                        member[i] = matches(i, std::uint32_t(i)) && kind != "handles" ? 1 : 0;
                    }
                }

                const std::string& operation = getString("operation");
                const bool existed = tagged->hasAttrib(jitnodes::AttribClass::point, "group." + name);
                jitnodes::AttribArray& group = tagged->createGroup(jitnodes::AttribClass::point, name);
                int* flags = group.ints().data();
                for (std::size_t i = 0; i < count; ++i) {
                    const bool before = existed && flags[i] != 0, now = member[i] != 0;
                    bool result = now;
                    if (operation == "union") {
                        result = before || now;
                    } else if (operation == "subtract") {
                        result = before && !now;
                    } else if (operation == "intersect") {
                        result = before && now;
                    }
                    flags[i] = result ? 1 : 0;
                }
                return tagged;
            }
        };
    }

    std::vector<int> pointIdList(const std::string& text) {
        std::vector<std::pair<long, long>> ranges;
        std::vector<int> ids;
        if (parseIds(text, ranges)) {
            for (const auto& range : ranges) {
                for (long id = std::max(0L, range.first); id <= std::min(range.second, range.first + 1000000L); ++id) {
                    ids.push_back(int(id));
                }
            }
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }

    std::string pointIdText(std::vector<int> ids) {
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        std::string out;
        for (std::size_t i = 0; i < ids.size();) {
            std::size_t last = i;
            while (last + 1 < ids.size() && ids[last + 1] == ids[last] + 1) {
                ++last;
            }
            out += (out.empty() ? "" : ",") + std::to_string(ids[i]);
            if (last > i) {
                out += "-" + std::to_string(ids[last]);
            }
            i = last + 1;
        }
        return out;
    }

    jitnodes::NodeId shapeBehind(const jitnodes::Network& network, jitnodes::NodeId node) {
        auto passes = [&network](jitnodes::NodeId at) { return network.node(at).typeName() == kTagNodeType || network.node(at).typeName() == kMovePointsNodeType; };
        for (int guard = 0; node != jitnodes::invalidNodeId && guard < 1000 && passes(node); ++guard) {
            node = network.inputOf(node, 0);
        }
        return node;
    }

    namespace {
        Placement placementFrom(const jitnodes::Node& node, bool resolved) {
            auto value = [&](const char* name) { return resolved ? node.getFloat(name) : node.baseFloat(name); };
            Placement p;
            p.translateX = value("translateX");
            p.translateY = value("translateY");
            p.rotation = value("rotation");
            p.scaleX = value("scaleX");
            p.scaleY = value("scaleY");
            p.shear = value("shear");
            p.pivotX = value("pivotX");
            p.pivotY = value("pivotY");
            return p;
        }
    }

    BLMatrix2D transformOf(const jitnodes::Node& node) {
        return matrixOf(placementFrom(node, false));
    }

    BLMatrix2D resolvedTransformOf(const jitnodes::Node& node) {
        return matrixOf(placementFrom(node, true));
    }

    void setTransformPivot(jitnodes::Node& node, double x, double y) {
        const BLMatrix2D held = transformOf(node);
        const float px = float(x), py = float(y);
        node.setFloat("pivotX", px);
        node.setFloat("pivotY", py);
        node.setFloat("translateX", float(held.m20 - px + held.m00 * px + held.m10 * py));
        node.setFloat("translateY", float(held.m21 - py + held.m01 * px + held.m11 * py));
    }

    bool pivotShownInParent(const jitnodes::Node& node, const std::string& name) {
        return node.typeName() == kTransformNodeType && (name == "pivotX" || name == "pivotY") && node.keyframes(name).empty() &&
               !node.isAnimated("translateX") && !node.isAnimated("translateY");
    }

    double shownParam(const jitnodes::Node& node, const std::string& name) {
        if (!pivotShownInParent(node, name)) {
            return node.baseFloat(name);
        }
        const bool x = name == "pivotX";
        return double(node.baseFloat(name)) + node.baseFloat(x ? "translateX" : "translateY");
    }

    void setPivotInParent(jitnodes::Node& node, const std::string& name, double value) {
        const BLMatrix2D held = transformOf(node);
        const double det = held.m00 * held.m11 - held.m01 * held.m10;
        const double px = node.baseFloat("pivotX"), py = node.baseFloat("pivotY");
        double dx = name == "pivotX" ? value - shownParam(node, "pivotX") : 0.0;
        double dy = name == "pivotY" ? value - shownParam(node, "pivotY") : 0.0;
        if (std::fabs(det) < 1e-12) {
            setTransformPivot(node, px + dx, py + dy);
            return;
        }
        // The local step that lands dx, dy in the parent space is the inverse of the matrix's linear part.
        setTransformPivot(node, px + (held.m11 * dx - held.m10 * dy) / det, py + (-held.m01 * dx + held.m00 * dy) / det);
    }

    void centerTransformPivot(jitnodes::Node& node, const jitnodes::Geometry& shape) {
        if (shape.pointCount() == 0) {
            return;
        }
        float x0 = shape.x()[0], x1 = x0, y0 = shape.y()[0], y1 = y0;
        for (std::size_t i = 1; i < shape.pointCount(); ++i) {
            x0 = std::min(x0, shape.x()[i]);
            x1 = std::max(x1, shape.x()[i]);
            y0 = std::min(y0, shape.y()[i]);
            y1 = std::max(y1, shape.y()[i]);
        }
        node.setFloat("pivotX", (x0 + x1) * 0.5f);
        node.setFloat("pivotY", (y0 + y1) * 0.5f);
    }

    void storeTransform(jitnodes::Node& node, const BLMatrix2D& matrix) {
        // A node whose own numbers already give this matrix keeps them (a scale of -1 would otherwise come back as a turn and a flip).
        const BLMatrix2D held = transformOf(node);
        bool same = true;
        for (int i = 0; i < 6; ++i) {
            same = same && std::fabs(held.m[i] - matrix.m[i]) <= 1e-6 * (1.0 + std::fabs(matrix.m[i]));
        }
        if (same) {
            return;
        }
        const Placement p = placementOf(matrix, node.baseFloat("pivotX"), node.baseFloat("pivotY"));
        const std::pair<const char*, float> values[6] = {{"translateX", p.translateX}, {"translateY", p.translateY}, {"rotation", p.rotation},
                                                         {"scaleX", p.scaleX}, {"scaleY", p.scaleY}, {"shear", p.shear}};
        for (const auto& [name, value] : values) {
            if (node.baseFloat(name) != value) {
                node.setFloat(name, value);
            }
        }
    }

    namespace {
        std::string paintWarpText(const PaintWarp& warp) {
            std::string text;
            if (!warp.isIdentity()) {
                for (int i = 0; i < 9; ++i) {
                    text += i == 0 ? "" : " ";
                    appendNumber(text, warp.m[i]);
                }
            }
            return text;
        }
    }

    void storePaintWarp(jitnodes::Node& node, const PaintWarp& warp) {
        if (paintWarpDiffers(node, warp)) {
            node.setString("paintWarp", paintWarpText(warp));
        }
    }

    bool paintWarpDiffers(const jitnodes::Node& node, const PaintWarp& warp) {
        return node.getString("paintWarp") != paintWarpText(warp);
    }

    PaintWarp paintWarpOf(const jitnodes::Node& node) {
        PaintWarp warp;
        std::istringstream in(node.getString("paintWarp"));
        for (int i = 0; i < 9; ++i) {
            if (!(in >> warp.m[i])) {
                return PaintWarp();
            }
        }
        return warp;
    }

    jitnodes::GeometryHandle placeGeometry(const jitnodes::Geometry& geometry, const BLMatrix2D& matrix) {
        auto placed = std::make_shared<jitnodes::Geometry>(geometry);
        movePoints(*placed, matrix);
        return placed;
    }

    BLMatrix2D worldTransformOf(const jitnodes::Network& network, jitnodes::NodeId place) {
        BLMatrix2D world = resolvedTransformOf(network.node(place));
        for (jitnodes::NodeId up = network.inputOf(place, 1); up != jitnodes::invalidNodeId; up = network.inputOf(up, 1)) {
            world.post_transform(resolvedTransformOf(network.node(up)));
        }
        return world;
    }

    void registerCurvyNodes() {
        static std::once_flag once;
        std::call_once(once, [] {
            using Extras = std::initializer_list<const char*>;
            auto& registry = jitnodes::NodeRegistry::instance();
            registry.registerType(kPathNodeType, [] { return std::make_unique<PathNode>(kPathNodeType, Extras{}); });
            registry.registerType(kTextNodeType, [] { return std::make_unique<PathNode>(kTextNodeType, Extras{"text", "font", "size", "matrix", "style", "warp"}); });
            registry.registerType(kSymbolNodeType, [] { return std::make_unique<PathNode>(kSymbolNodeType, Extras{"symbol"}); });
            registry.registerType(kImageNodeType, [] { return std::make_unique<PathNode>(kImageNodeType, Extras{"image"}); });
            registry.registerType(kTransformNodeType, [] { return std::make_unique<TransformNode>(); });
            registry.registerType(kTagNodeType, [] { return std::make_unique<TagNode>(); });
            registry.registerType(kMovePointsNodeType, [] { return std::make_unique<MovePointsNode>(); });
            registerAppearanceNodes();
        });
    }

    namespace {
        std::string numberText(double value) {
            std::string out;
            appendNumber(out, value);
            return out;
        }

        std::string matrixText(const BLMatrix2D& matrix) {
            std::string out;
            for (int i = 0; i < 6; ++i) {
                out += i == 0 ? "" : " ";
                appendNumber(out, matrix.m[i]);
            }
            return out;
        }

        std::string styleText(const TextStyle& style) {
            return std::string(style.bold ? "1" : "0") + ' ' + (style.italic ? "1" : "0") + ' ' + std::to_string(style.align) + ' ' +
                numberText(style.tracking) + ' ' + numberText(style.leading);
        }

        // "kind start flip|first path|second path", empty for plain text.
        std::string warpText(const TextWarp& warp) {
            if (!warp.active()) {
                return {};
            }
            return std::to_string(int(warp.kind)) + ' ' + numberText(warp.start) + ' ' + (warp.flip ? "1" : "0") + '|' + pathToText(warp.first) + '|' +
                pathToText(warp.second);
        }
    }

    bool isShapeNodeType(const std::string& typeName) {
        return typeName == kPathNodeType || typeName == kTextNodeType || typeName == kSymbolNodeType || typeName == kImageNodeType;
    }

    const char* shapeNodeType(const SketchObject& object) {
        switch (object.type()) {
            case SketchObjectType::Text: return kTextNodeType;
            case SketchObjectType::Symbol: return kSymbolNodeType;
            case SketchObjectType::Image: return kImageNodeType;
            default: return kPathNodeType;
        }
    }

    std::vector<std::pair<std::string, std::string>> shapeNodeParams(const SketchObject& object) {
        std::vector<std::pair<std::string, std::string>> params;
        switch (object.type()) {
            case SketchObjectType::Text:
                params = {{"text", object.text()}, {"font", object.fontName()}, {"size", numberText(object.fontSize())},
                    {"matrix", matrixText(object.textMatrix())}, {"style", styleText(object.textStyle())}, {"warp", warpText(object.textWarp())}};
                break;
            case SketchObjectType::Symbol:
                params = {{"symbol", object.symbol() ? std::to_string(object.symbol()->id) : std::string()}};
                break;
            case SketchObjectType::Image:
                params = {{"image", object.image() ? object.image()->name : std::string()}};
                break;
            default: break;
        }
        return params;
    }

    bool applyTextNodeParams(SketchObject& object, const jitnodes::Node& node) {
        if (!object.isText()) {
            return false;
        }
        bool same = true;
        for (const auto& [name, value] : shapeNodeParams(object)) {
            same = same && node.getString(name) == value;
        }
        if (same) {
            return false;
        }
        double size = 0.0;
        const std::string& sizeText = node.getString("size");
        if (std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), size).ec != std::errc() || size <= 0.0) {
            return false;
        }
        BLMatrix2D matrix;
        {
            std::istringstream in(node.getString("matrix"));
            for (int i = 0; i < 6; ++i) {
                if (!(in >> matrix.m[i])) {
                    return false;
                }
            }
        }
        TextStyle style;
        {
            std::istringstream in(node.getString("style"));
            int bold = 0, italic = 0;
            if (!(in >> bold >> italic >> style.align >> style.tracking >> style.leading)) {
                return false;
            }
            style.bold = bold != 0;
            style.italic = italic != 0;
        }
        TextWarp warp;
        const std::string& warpSource = node.getString("warp");
        if (!warpSource.empty()) {
            const std::size_t bar1 = warpSource.find('|');
            const std::size_t bar2 = bar1 == std::string::npos ? bar1 : warpSource.find('|', bar1 + 1);
            int kind = 0, flip = 0;
            std::istringstream in(warpSource.substr(0, bar1));
            if (bar2 == std::string::npos || !(in >> kind >> warp.start >> flip) || kind < 0 || kind > int(TextWarp::Kind::Between) ||
                !pathFromText(warpSource.substr(bar1 + 1, bar2 - bar1 - 1), warp.first) || !pathFromText(warpSource.substr(bar2 + 1), warp.second)) {
                return false;
            }
            warp.kind = TextWarp::Kind(kind);
            warp.flip = flip != 0;
        }
        // The last call rebuilds the outline with everything set before it, the placement included.
        SketchObject::Prototype::setTextWarp(object, warp);
        SketchObject::Prototype::setTextStyle(object, style);
        SketchObject::Prototype::setTextState(object, node.getString("text"), node.getString("font"), size, matrix);
        return true;
    }

    jitnodes::GeometryHandle geometryFromPath(const BLPath& path, const jitnodes::Geometry* keepAttributes) {
        auto geometry = std::make_shared<jitnodes::Geometry>();
        fillFigures(*geometry, path);
        const std::size_t count = geometry->pointCount();
        // Attributes on anchors (pressure, width...) belong to the node, not to the object; they stay while only positions move.
        if (keepAttributes != nullptr && keepAttributes->pointCount() == count && sameCurveShape(*keepAttributes, *geometry, 0.0, false)) {
            for (const auto& [name, array] : keepAttributes->attribs(jitnodes::AttribClass::point)) {
                if (name != "x" && name != "y") {
                    geometry->attrib(jitnodes::AttribClass::point, name, array.type()) = array;
                }
            }
        } else if (keepAttributes != nullptr && keepAttributes->pointCount() > 0 && count > 0) {
            // Points were added or taken away: the ones still where they were (met in the same order, so a gap is skipped over) keep their
            // attributes and the rest start from zero.
            const jitnodes::Geometry& old = *keepAttributes;
            constexpr std::size_t kLookahead = 64;
            std::vector<std::size_t> from(count, old.pointCount());
            std::size_t next = 0;
            for (std::size_t i = 0; i < count; ++i) {
                for (std::size_t j = next; j < std::min(old.pointCount(), next + kLookahead); ++j) {
                    const double slack = 1e-4 * (1.0 + std::max(std::fabs(double(old.x()[j])), std::fabs(double(old.y()[j]))));
                    if (std::fabs(double(old.x()[j]) - geometry->x()[i]) <= slack && std::fabs(double(old.y()[j]) - geometry->y()[i]) <= slack) {
                        from[i] = j;
                        next = j + 1;
                        break;
                    }
                }
            }
            for (const auto& [name, array] : old.attribs(jitnodes::AttribClass::point)) {
                if (name == "x" || name == "y") {
                    continue;
                }
                jitnodes::AttribArray& target = geometry->attrib(jitnodes::AttribClass::point, name, array.type());
                for (std::size_t i = 0; i < count; ++i) {
                    if (from[i] == old.pointCount()) {
                        continue;
                    }
                    if (array.type() == jitnodes::AttribType::int32) {
                        target.ints()[i] = array.ints()[from[i]];
                    } else {
                        target.floats()[i] = array.floats()[from[i]];
                    }
                }
            }
            // Fresh ids for the new points, above every id the old shape had.
            const jitnodes::AttribArray* oldIds = old.findAttrib(jitnodes::AttribClass::point, kPointIdAttrib);
            if (oldIds != nullptr && oldIds->type() == jitnodes::AttribType::int32) {
                std::int32_t top = -1;
                for (std::size_t j = 0; j < old.pointCount(); ++j) {
                    top = std::max(top, oldIds->ints()[j]);
                }
                std::int32_t* ids = geometry->attrib(jitnodes::AttribClass::point, kPointIdAttrib, jitnodes::AttribType::int32).ints().data();
                for (std::size_t i = 0; i < count; ++i) {
                    if (from[i] == old.pointCount()) {
                        ids[i] = ++top;
                    }
                }
                return geometry;
            }
        }
        const jitnodes::AttribArray* ids = geometry->findAttrib(jitnodes::AttribClass::point, kPointIdAttrib);
        if (ids == nullptr && count > 0) {
            std::int32_t* fresh = geometry->attrib(jitnodes::AttribClass::point, kPointIdAttrib, jitnodes::AttribType::int32).ints().data();
            for (std::size_t i = 0; i < count; ++i) {
                fresh[i] = std::int32_t(i);
            }
        }
        return geometry;
    }

    bool sameCurveShape(const jitnodes::Geometry& a, const jitnodes::Geometry& b, double tolerance, bool positions) {
        if (a.pointCount() != b.pointCount() || a.primitiveCount() != b.primitiveCount()) {
            return false;
        }
        for (std::size_t i = 0; i < a.primitiveCount(); ++i) {
            const jitnodes::Primitive& p = a.primitives()[i];
            const jitnodes::Primitive& q = b.primitives()[i];
            if (p.start != q.start || p.count != q.count || p.closed != q.closed || p.kind != q.kind) {
                return false;
            }
        }
        if (!positions || a.pointCount() == 0) {
            return true;
        }
        for (std::size_t i = 0; i < a.pointCount(); ++i) {
            const double slack = tolerance * (1.0 + std::max(std::fabs(a.x()[i]), std::fabs(a.y()[i])));
            if (std::fabs(double(a.x()[i]) - b.x()[i]) > slack || std::fabs(double(a.y()[i]) - b.y()[i]) > slack) {
                return false;
            }
        }
        return true;
    }

    bool pathFromGeometry(const jitnodes::Geometry& geometry, BLPath& path) {
        path.clear();
        if (geometry.pointCount() == 0) {
            return true;
        }
        const float* x = geometry.x();
        const float* y = geometry.y();
        auto at = [&](std::size_t i) { return BLPoint(x[i], y[i]); };
        for (const jitnodes::Primitive& prim : geometry.primitives()) {
            if (prim.count == 0 || std::size_t(prim.start) + prim.count > geometry.pointCount()) {
                return false;
            }
            path.move_to(at(prim.start));
            if (prim.kind == jitnodes::PrimKind::polyline) {
                for (std::uint32_t k = 1; k < prim.count; ++k) {
                    path.line_to(at(prim.start + k));
                }
            } else {
                const std::uint32_t segments = jitnodes::cubicSegments(prim);
                if (segments == 0) {
                    return false;
                }
                for (std::uint32_t s = 0; s < segments; ++s) {
                    const std::size_t i = prim.start + 3 * s;
                    const std::size_t end = prim.start + (3 * s + 3) % prim.count;
                    // a straight segment: both handles sit on their anchors
                    if (x[i + 1] == x[i] && y[i + 1] == y[i] && x[i + 2] == x[end] && y[i + 2] == y[end]) {
                        path.line_to(at(end));
                    } else {
                        path.cubic_to(at(i + 1), at(i + 2), at(end));
                    }
                }
            }
            if (prim.closed) {
                path.close();
            }
        }
        return true;
    }

    std::string pathToText(const BLPath& path) {
        std::string out;
        const std::uint8_t* command = path.command_data();
        const BLPoint* vertex = path.vertex_data();
        const std::size_t count = path.size();
        auto emit = [&](char letter, std::size_t first, std::size_t points) {
            if (!out.empty()) {
                out += ' ';
            }
            out += letter;
            for (std::size_t i = 0; i < points; ++i) {
                out += i == 0 ? "" : " ";
                appendPoint(out, vertex[first + i]);
            }
        };
        for (std::size_t i = 0; i < count; ++i) {
            switch (command[i]) {
                case BL_PATH_CMD_MOVE: emit('M', i, 1); break;
                case BL_PATH_CMD_ON: emit('L', i, 1); break;
                case BL_PATH_CMD_QUAD:
                    if (i + 1 < count) {
                        emit('Q', i, 2);
                    }
                    i += 1;
                    break;
                case BL_PATH_CMD_CUBIC:
                    if (i + 2 < count) {
                        emit('C', i, 3);
                    }
                    i += 2;
                    break;
                case BL_PATH_CMD_CLOSE:
                    if (!out.empty()) {
                        out += ' ';
                    }
                    out += 'Z';
                    break;
                default: break;
            }
        }
        return out;
    }

    bool pathFromText(const std::string& text, BLPath& path) {
        path.clear();
        const char* p = text.data();
        const char* end = p + text.size();
        auto skip = [&] { while (p < end && *p == ' ') { ++p; } };
        auto number = [&](double& value) {
            skip();
            const auto result = std::from_chars(p, end, value);
            if (result.ec != std::errc()) {
                return false;
            }
            p = result.ptr;
            return true;
        };
        auto points = [&](BLPoint* out, int count) {
            for (int i = 0; i < count; ++i) {
                if (!number(out[i].x) || !number(out[i].y)) {
                    return false;
                }
            }
            return true;
        };
        for (skip(); p < end; skip()) {
            const char letter = *p++;
            BLPoint v[3];
            switch (letter) {
                case 'M': if (!points(v, 1)) { return false; } path.move_to(v[0]); break;
                case 'L': if (!points(v, 1)) { return false; } path.line_to(v[0]); break;
                case 'Q': if (!points(v, 2)) { return false; } path.quad_to(v[0], v[1]); break;
                case 'C': if (!points(v, 3)) { return false; } path.cubic_to(v[0], v[1], v[2]); break;
                case 'Z': path.close(); break;
                default: return false;
            }
        }
        return true;
    }

}
