#include "curvy/brush.h"

#include "curvy/pathflatten.h"
#include "curvy/sketchdocument.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Brushes: the geometry of the three kinds (see brush.h). Painting them is SketchObject's job (sketchobject.cpp).

namespace curvy {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        // 0 <= value < 1, the same for the same key (so scattered copies do not shimmer when anything is redrawn).
        double noise(std::uint32_t key) {
            key *= 2654435761u;
            key ^= key >> 15;
            key *= 2246822519u;
            key ^= key >> 13;
            key *= 3266489917u;
            key ^= key >> 16;
            return double(key & 0xFFFFFFu) / 16777216.0;
        }

        double cross(const BLPoint& o, const BLPoint& a, const BLPoint& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        }

        // The convex hull of the points (Andrew monotone chain): always wound the same way, so filling many of them non-zero is their
        // union.
        std::vector<BLPoint> convexHull(std::vector<BLPoint> pts) {
            std::sort(pts.begin(), pts.end(), [](const BLPoint& a, const BLPoint& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
            std::vector<BLPoint> hull(pts.size() * 2);
            std::size_t k = 0;
            for (std::size_t i = 0; i < pts.size(); ++i) {
                while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.0) { --k; }
                hull[k++] = pts[i];
            }
            for (std::size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
                while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i - 1]) <= 0.0) { --k; }
                hull[k++] = pts[i - 1];
            }
            hull.resize(k > 1 ? k - 1 : k);
            return hull;
        }
    }

    const char* brushKindName(BrushKind kind) {
        static const char* kNames[] = {"Calligraphic", "Scatter", "Art"};
        return kNames[std::size_t(kind)];
    }

    std::vector<double> BrushDef::params() const {
        return {angle, roundness, spacing, scatter, sizeVariation, rotation, followPath ? 1.0 : 0.0};
    }

    void BrushDef::setParams(const std::vector<double>& values) {
        auto take = [&](std::size_t i, double& target, double lo, double hi) {
            if (i < values.size() && std::isfinite(values[i])) {
                target = std::min(hi, std::max(lo, values[i]));
            }
        };
        take(0, angle, -180.0, 180.0);
        take(1, roundness, 1.0, 100.0);
        take(2, spacing, 5.0, 1000.0);
        take(3, scatter, 0.0, 500.0);
        take(4, sizeVariation, 0.0, 100.0);
        take(5, rotation, 0.0, 180.0);
        if (values.size() > 6) {
            followPath = values[6] != 0.0;
        }
    }

    bool BrushDef::sameLook(const BrushDef& o) const {
        return kind == o.kind && params() == o.params();
    }

    const std::vector<BrushPart>& BrushDef::parts() const {
        if (!partsBuilt_) {
            partsBuilt_ = true;
            if (art && art->content) {
                for (std::size_t i = 0; i < art->content->objectCount(); ++i) {
                    const SketchObject& object = art->content->object(i);
                    if (!object.isVisible() || object.isSymbol()) {
                        continue;
                    }
                    BrushPart part;
                    part.path = object.geometry();
                    part.closed = object.isClosed();
                    part.fill = object.fill();
                    part.stroke = object.stroke();
                    part.strokeWidth = object.strokeWidth();
                    parts_.push_back(std::move(part));
                }
            }
        }
        return parts_;
    }

    double BrushDef::reach(double strokeWidth) const {
        if (kind == BrushKind::Calligraphic || !art) {
            return strokeWidth * 0.5;
        }
        const double w = art->box.x1 - art->box.x0, h = art->box.y1 - art->box.y0;
        if (kind == BrushKind::Art) {
            return h * strokeWidth * 0.5;
        }
        return 0.5 * std::hypot(w, h) * strokeWidth * (1.0 + sizeVariation / 100.0) + scatter / 100.0 * h * strokeWidth;
    }

    BLPath BrushDef::nibOutline(const BLPath& path, double strokeWidth) const {
        const int kSides = 16;
        const double major = strokeWidth * 0.5, minor = std::max(major * roundness / 100.0, 0.05);
        const double turn = -angle * kPi / 180.0, c = std::cos(turn), s = std::sin(turn);
        std::vector<BLPoint> nib;
        for (int k = 0; k < kSides; ++k) {
            const double t = 2.0 * kPi * k / kSides, x = major * std::cos(t), y = minor * std::sin(t);
            nib.push_back(BLPoint(x * c - y * s, x * s + y * c));
        }
        BLPath out;
        for (const MeasuredPath& m : measurePath(path)) {
            BLPoint last = m.points.front();
            for (std::size_t i = 1; i < m.points.size(); ++i) {
                const BLPoint& next = m.points[i];
                if (std::hypot(next.x - last.x, next.y - last.y) < 0.05 && i + 1 < m.points.size()) {
                    continue;
                }
                std::vector<BLPoint> both;
                for (const BLPoint& p : nib) {
                    both.push_back(BLPoint(p.x + last.x, p.y + last.y));
                    both.push_back(BLPoint(p.x + next.x, p.y + next.y));
                }
                std::vector<BLPoint> hull = convexHull(std::move(both));
                if (hull.size() >= 3) {
                    out.move_to(hull[0]);
                    for (std::size_t k = 1; k < hull.size(); ++k) {
                        out.line_to(hull[k]);
                    }
                    out.close();
                }
                last = next;
            }
        }
        return out;
    }

    std::vector<BrushPart> BrushDef::bendAlong(const BLPath& path, double strokeWidth) const {
        std::vector<BrushPart> result;
        if (!art || !(strokeWidth > 0.0)) {
            return result;
        }
        const BLBox& box = art->box;
        const double w = box.x1 - box.x0, cy = (box.y0 + box.y1) * 0.5;
        if (!(w > 0.0)) {
            return result;
        }
        for (const MeasuredPath& m : measurePath(path)) {
            // The artwork x runs along the path, its y across: up is to the left of the direction of travel.
            auto map = [&](double x, double y) {
                BLPoint p, t;
                sampleMeasured(m, (x - box.x0) / w * m.total, p, t);
                const double across = (cy - y) * strokeWidth;
                return BLPoint(p.x + t.y * across, p.y - t.x * across);
            };
            for (const BrushPart& part : parts()) {
                BrushPart bent;
                bent.closed = part.closed;
                bent.fill = part.fill;
                bent.stroke = part.stroke;
                bent.strokeWidth = part.strokeWidth * strokeWidth;
                for (Polyline& line : flattenPath(part.path)) {
                    std::vector<BLPoint> pts = line.points;
                    if (line.closed) {
                        pts.push_back(pts.front());
                    }
                    for (std::size_t i = 0; i < pts.size(); ++i) {
                        if (i == 0) {
                            bent.path.move_to(map(pts[0].x, pts[0].y));
                            continue;
                        }
                        // Long edges are cut up so that they bend with the path.
                        const double length = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
                        const int pieces = std::min(300, std::max(1, int(std::ceil(length / (w / 150.0)))));
                        for (int k = 1; k <= pieces; ++k) {
                            const double u = double(k) / pieces;
                            bent.path.line_to(map(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * u, pts[i - 1].y + (pts[i].y - pts[i - 1].y) * u));
                        }
                    }
                    if (line.closed) {
                        bent.path.close();
                    }
                }
                result.push_back(std::move(bent));
            }
        }
        return result;
    }

    std::vector<BLMatrix2D> BrushDef::placements(const BLPath& path, double strokeWidth) const {
        std::vector<BLMatrix2D> result;
        if (!art || !(strokeWidth > 0.0)) {
            return result;
        }
        const BLBox& box = art->box;
        const double w = box.x1 - box.x0, h = box.y1 - box.y0, cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
        const double step = std::max(w * strokeWidth * spacing / 100.0, 0.5);
        std::uint32_t seed = 1;
        for (const MeasuredPath& m : measurePath(path)) {
            const int count = std::min(5000, int(std::floor(m.total / step)) + 1);
            for (int i = 0; i < count; ++i, ++seed) {
                BLPoint p, t;
                sampleMeasured(m, step * i, p, t);
                const double across = (noise(seed * 3u) - 0.5) * 2.0 * scatter / 100.0 * h * strokeWidth;
                const double size = strokeWidth * (1.0 + (noise(seed * 3u + 1u) - 0.5) * 2.0 * sizeVariation / 100.0);
                double turn = (noise(seed * 3u + 2u) - 0.5) * 2.0 * rotation * kPi / 180.0;
                if (followPath) {
                    turn += std::atan2(t.y, t.x);
                }
                BLMatrix2D matrix = BLMatrix2D::make_translation(-cx, -cy);
                matrix.post_scale(size, size);
                matrix.post_rotate(turn);
                matrix.post_translate(p.x + t.y * across, p.y - t.x * across);
                result.push_back(matrix);
            }
        }
        return result;
    }

}
