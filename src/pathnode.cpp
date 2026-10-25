#include "curvy/pathnode.h"

#include "curvy/appearancenode.h"
#include "curvy/sketchobject.h"

#include <jitnodes/graph/network.h>

#include <charconv>
#include <cmath>
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

    void registerCurvyNodes() {
        static std::once_flag once;
        std::call_once(once, [] {
            using Extras = std::initializer_list<const char*>;
            auto& registry = jitnodes::NodeRegistry::instance();
            registry.registerType(kPathNodeType, [] { return std::make_unique<PathNode>(kPathNodeType, Extras{}); });
            registry.registerType(kTextNodeType, [] { return std::make_unique<PathNode>(kTextNodeType, Extras{"text", "font", "size", "matrix", "style", "warp"}); });
            registry.registerType(kSymbolNodeType, [] { return std::make_unique<PathNode>(kSymbolNodeType, Extras{"symbol"}); });
            registry.registerType(kImageNodeType, [] { return std::make_unique<PathNode>(kImageNodeType, Extras{"image"}); });
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
        object.setTextWarp(warp);
        object.setTextStyle(style);
        object.setTextState(node.getString("text"), node.getString("font"), size, matrix);
        return true;
    }

    jitnodes::GeometryHandle geometryFromPath(const BLPath& path, const jitnodes::Geometry* keepAttributes) {
        auto geometry = std::make_shared<jitnodes::Geometry>();
        fillFigures(*geometry, path);
        // Attributes on anchors (pressure, width...) belong to the node, not to the object; they stay while only positions move.
        if (keepAttributes != nullptr && keepAttributes->pointCount() == geometry->pointCount() && sameCurveShape(*keepAttributes, *geometry, 0.0, false)) {
            for (const auto& [name, array] : keepAttributes->attribs(jitnodes::AttribClass::point)) {
                if (name != "x" && name != "y") {
                    geometry->attrib(jitnodes::AttribClass::point, name, array.type()) = array;
                }
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
