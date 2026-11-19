#include "curvy/shapepaths.h"

#include <algorithm>
#include <cmath>

namespace curvy {

    BLPath shapePath(const jitnodes::Geometry& geo, const jitnodes::ShapeRange& shape, double originX, double originY, double scale) {
        const float* px = geo.x();
        const float* py = geo.y();
        BLPath path;
        for (std::uint32_t p = shape.first; p < shape.first + shape.count; ++p) {
            const jitnodes::Primitive& prim = geo.primitives()[p];
            if (prim.count < 2) {
                continue;
            }
            const auto at = [&](std::uint32_t k) { return BLPoint(originX + double(px[prim.start + k]) * scale, originY + double(py[prim.start + k]) * scale); };
            path.move_to(at(0));
            if (prim.kind == jitnodes::PrimKind::cubic) {
                const std::uint32_t segments = jitnodes::cubicSegments(prim);
                for (std::uint32_t s = 0; s < segments; ++s) {
                    const std::uint32_t k = s * 3;
                    path.cubic_to(at(k + 1), at(k + 2), at((k + 3) % prim.count));
                }
            } else {
                for (std::uint32_t k = 1; k < prim.count; ++k) {
                    path.line_to(at(k));
                }
            }
            if (prim.closed) {
                path.close();
            }
        }
        return path;
    }

    std::optional<ShapeHit> shapeAt(const jitnodes::Geometry& geo, float x, float y, float tolerance) {
        if (geo.primitiveCount() == 0 || !geo.hasPointAttrib("x")) {
            return std::nullopt;
        }
        const auto column = [&](const char* name) -> const float* {
            const jitnodes::AttribArray* a = geo.findAttrib(jitnodes::AttribClass::primitive, name);
            return a != nullptr ? a->floats().data() : nullptr;
        };
        const float* fillA = column("fill.a");
        const float* strokeA = column("stroke.a");
        const float* strokeW = column("stroke.width");
        const float* evenOdd = column("fill.evenOdd");

        const BLPoint at = BLPoint(double(x), double(y));
        const std::vector<jitnodes::ShapeRange> shapes = geo.shapes();
        for (std::size_t s = shapes.size(); s-- > 0;) {
            const jitnodes::ShapeRange& shape = shapes[s];
            const std::uint32_t i = shape.first;   // a shape is painted with its first primitive's look
            const bool filled = fillA != nullptr && fillA[i] > 0.0f;
            const bool stroked = strokeA != nullptr && strokeW != nullptr && strokeA[i] > 0.0f && strokeW[i] > 0.0f;
            if (!filled && !stroked) {
                continue;
            }
            const BLPath path = shapePath(geo, shape, 0.0, 0.0, 1.0);
            if (path.is_empty()) {
                continue;
            }
            bool hit = false;
            if (filled) {
                const BLFillRule rule = evenOdd != nullptr && evenOdd[i] != 0.0f ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO;
                hit = path.hit_test(at, rule) == BL_HIT_TEST_IN;
            }
            if (!hit && stroked) {
                BLStrokeOptions options;
                options.width = std::max(double(strokeW[i]), 2.0 * double(tolerance));
                BLPath outline;
                outline.add_stroked_path(path, options, bl_default_approximation_options);
                hit = outline.hit_test(at, BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN;
            }
            if (hit) {
                return ShapeHit{s, shape.first, std::string(shape.key)};
            }
        }
        return std::nullopt;
    }

    namespace {

        bool insidePolygon(const BLPoint& p, const std::vector<BLPoint>& polygon) {
            bool in = false;
            for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
                const BLPoint& a = polygon[i];
                const BLPoint& b = polygon[j];
                if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
                    in = !in;
                }
            }
            return in;
        }

        // Points along the shape's outline: polylines are cut every few units (a concave region can leave between two vertices), cubic
        // segments are sampled.
        std::vector<BLPoint> outlinePoints(const jitnodes::Geometry& geo, const jitnodes::ShapeRange& shape) {
            const float* px = geo.x();
            const float* py = geo.y();
            std::vector<BLPoint> out;
            for (std::uint32_t p = shape.first; p < shape.first + shape.count; ++p) {
                const jitnodes::Primitive& prim = geo.primitives()[p];
                if (prim.count < 1) {
                    continue;
                }
                const auto at = [&](std::uint32_t k) { return BLPoint(double(px[prim.start + k]), double(py[prim.start + k])); };
                if (prim.kind == jitnodes::PrimKind::cubic) {
                    const std::uint32_t segments = jitnodes::cubicSegments(prim);
                    for (std::uint32_t s = 0; s < segments; ++s) {
                        const std::uint32_t k = s * 3;
                        const BLPoint p0 = at(k), p1 = at(k + 1), p2 = at(k + 2), p3 = at((k + 3) % prim.count);
                        for (int i = 0; i < 16; ++i) {
                            const double t = double(i) / 16.0, u = 1.0 - t;
                            out.push_back(BLPoint(u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                                                  u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y));
                        }
                    }
                    if (segments > 0 && !prim.closed) {
                        out.push_back(at(prim.count - 1));
                    }
                } else {
                    const std::uint32_t last = prim.closed ? prim.count : prim.count - 1;
                    for (std::uint32_t k = 0; k < last; ++k) {
                        const BLPoint a = at(k), b = at((k + 1) % prim.count);
                        const int cuts = std::clamp(int(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / 4.0)), 1, 400);
                        for (int i = 0; i < cuts; ++i) {
                            const double t = double(i) / double(cuts);
                            out.push_back(BLPoint(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t));
                        }
                    }
                    if (!prim.closed) {
                        out.push_back(at(prim.count - 1));
                    }
                }
            }
            return out;
        }

        // The region's edge cut into points, at most about `budget` of them.
        std::vector<BLPoint> edgePoints(const std::vector<BLPoint>& polygon, std::size_t budget) {
            double length = 0.0;
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const BLPoint& a = polygon[i];
                const BLPoint& b = polygon[(i + 1) % polygon.size()];
                length += std::hypot(b.x - a.x, b.y - a.y);
            }
            const double step = std::max(1.0, length / double(budget));
            std::vector<BLPoint> out;
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const BLPoint& a = polygon[i];
                const BLPoint& b = polygon[(i + 1) % polygon.size()];
                const int cuts = std::max(1, int(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / step)));
                for (int k = 0; k < cuts; ++k) {
                    const double t = double(k) / double(cuts);
                    out.push_back(BLPoint(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t));
                }
            }
            return out;
        }

    }

    std::vector<std::string> shapesInRegion(const jitnodes::Geometry& geo, const std::vector<BLPoint>& polygon, RegionMode mode) {
        std::vector<std::string> keys;
        if (polygon.size() < 3 || geo.primitiveCount() == 0 || !geo.hasPointAttrib("x")) {
            return keys;
        }
        const auto column = [&](const char* name) -> const float* {
            const jitnodes::AttribArray* a = geo.findAttrib(jitnodes::AttribClass::primitive, name);
            return a != nullptr ? a->floats().data() : nullptr;
        };
        const float* fillA = column("fill.a");
        const float* strokeA = column("stroke.a");
        const float* strokeW = column("stroke.width");
        const float* evenOdd = column("fill.evenOdd");
        const std::vector<BLPoint> edge = mode == RegionMode::Touches ? edgePoints(polygon, 600) : std::vector<BLPoint>();

        for (const jitnodes::ShapeRange& shape : geo.shapes()) {
            if (shape.key.empty()) {
                continue;
            }
            const std::uint32_t i = shape.first;
            const bool filled = fillA != nullptr && fillA[i] > 0.0f;
            const bool stroked = strokeA != nullptr && strokeW != nullptr && strokeA[i] > 0.0f && strokeW[i] > 0.0f;
            if (!filled && !stroked) {
                continue;
            }
            const std::vector<BLPoint> outline = outlinePoints(geo, shape);
            if (outline.empty()) {
                continue;
            }
            bool taken = false;
            if (mode == RegionMode::Inside) {
                taken = std::all_of(outline.begin(), outline.end(), [&](const BLPoint& p) { return insidePolygon(p, polygon); });
            } else {
                taken = std::any_of(outline.begin(), outline.end(), [&](const BLPoint& p) { return insidePolygon(p, polygon); });
                if (!taken) {
                    const BLPath path = shapePath(geo, shape, 0.0, 0.0, 1.0);
                    BLPath strokeOutline;
                    if (stroked) {
                        BLStrokeOptions options;
                        options.width = std::max(double(strokeW[i]), 1.0);
                        strokeOutline.add_stroked_path(path, options, bl_default_approximation_options);
                    }
                    const BLFillRule rule = evenOdd != nullptr && evenOdd[i] != 0.0f ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO;
                    for (const BLPoint& p : edge) {
                        if ((filled && path.hit_test(p, rule) == BL_HIT_TEST_IN) || (stroked && strokeOutline.hit_test(p, BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN)) {
                            taken = true;
                            break;
                        }
                    }
                }
            }
            if (taken) {
                keys.push_back(std::string(shape.key));
            }
        }
        return keys;
    }

    std::vector<BLPoint> rectRegion(double x0, double y0, double x1, double y1) {
        return {BLPoint(x0, y0), BLPoint(x1, y0), BLPoint(x1, y1), BLPoint(x0, y1)};
    }

    std::vector<BLPoint> ellipseRegion(double x0, double y0, double x1, double y1) {
        constexpr int steps = 64;
        const double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5, rx = std::abs(x1 - x0) * 0.5, ry = std::abs(y1 - y0) * 0.5;
        std::vector<BLPoint> out;
        out.reserve(steps);
        for (int i = 0; i < steps; ++i) {
            const double t = 6.283185307179586 * double(i) / double(steps);
            out.push_back(BLPoint(cx + rx * std::cos(t), cy + ry * std::sin(t)));
        }
        return out;
    }

}
