#include "curvy/shapepaths.h"

#include <algorithm>

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

}
