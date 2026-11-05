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

        // Corners of the measured path sharper than this are rounded by the Smooth policy (the turns between the pieces of a flattened
        // curve are far smaller).
        constexpr double kCornerTurn = 15.0 * kPi / 180.0;

        MeasuredPath measuredFrom(std::vector<BLPoint> points) {
            MeasuredPath m;
            m.points = std::move(points);
            m.at.assign(m.points.size(), 0.0);
            for (std::size_t i = 1; i < m.points.size(); ++i) {
                m.at[i] = m.at[i - 1] + std::hypot(m.points[i].x - m.points[i - 1].x, m.points[i].y - m.points[i - 1].y);
            }
            m.total = m.at.empty() ? 0.0 : m.at.back();
            return m;
        }

        // The signed turn (radians) of the path at its vertex `i` (not the first or last).
        double turnAt(const MeasuredPath& m, std::size_t i) {
            const BLPoint &a = m.points[i - 1], &b = m.points[i], &c = m.points[i + 1];
            const double l1 = std::hypot(b.x - a.x, b.y - a.y), l2 = std::hypot(c.x - b.x, c.y - b.y);
            const double x1 = (b.x - a.x) / l1, y1 = (b.y - a.y) / l1, x2 = (c.x - b.x) / l2, y2 = (c.y - b.y) / l2;
            return std::atan2(x1 * y2 - y1 * x2, x1 * x2 + y1 * y2);
        }

        // The path cut at every vertex that turns by more than `angle` radians.
        std::vector<MeasuredPath> splitAtCorners(const MeasuredPath& m, double angle) {
            std::vector<MeasuredPath> pieces;
            std::vector<BLPoint> run{m.points.front()};
            for (std::size_t i = 1; i < m.points.size(); ++i) {
                run.push_back(m.points[i]);
                if (i + 1 < m.points.size() && std::fabs(turnAt(m, i)) > angle) {
                    pieces.push_back(measuredFrom(std::move(run)));
                    run = {m.points[i]};
                }
            }
            pieces.push_back(measuredFrom(std::move(run)));
            return pieces;
        }

        // The path with each sharp corner replaced by a quadratic curve that starts and ends `radius` (at most half a segment) away from it.
        MeasuredPath roundCorners(const MeasuredPath& m, double radius) {
            if (!(radius > 0.0) || m.points.size() < 3) {
                return m;
            }
            std::vector<BLPoint> out{m.points.front()};
            for (std::size_t i = 1; i + 1 < m.points.size(); ++i) {
                const BLPoint &a = m.points[i - 1], &b = m.points[i], &c = m.points[i + 1];
                const double l1 = std::hypot(b.x - a.x, b.y - a.y), l2 = std::hypot(c.x - b.x, c.y - b.y);
                const BLPoint u1((b.x - a.x) / l1, (b.y - a.y) / l1), u2((c.x - b.x) / l2, (c.y - b.y) / l2);
                const double turn = std::atan2(u1.x * u2.y - u1.y * u2.x, u1.x * u2.x + u1.y * u2.y);
                if (std::fabs(turn) < kCornerTurn) {
                    out.push_back(b);
                    continue;
                }
                const double d = std::min({radius, l1 * 0.5, l2 * 0.5});
                const BLPoint from(b.x - u1.x * d, b.y - u1.y * d), to(b.x + u2.x * d, b.y + u2.y * d);
                const int steps = 8;
                for (int k = 0; k <= steps; ++k) {
                    const double t = double(k) / steps, p = (1 - t) * (1 - t), q = 2 * (1 - t) * t, r = t * t;
                    out.push_back(BLPoint(p * from.x + q * b.x + r * to.x, p * from.y + q * b.y + r * to.y));
                }
            }
            out.push_back(m.points.back());
            return measuredFrom(std::move(out));
        }
    }

    // The pen pressure along a freehand stroke as a width profile (both sides alike; the pressure itself is the multiplier, so the finished
    // stroke is as wide as the proxy was): a point about every 4% of the length, and the ends. Empty when the pressure hardly changed.
    std::vector<WidthPoint> pressureProfile(const std::vector<BLPoint>& points, const std::vector<float>& pressure) {
        std::vector<WidthPoint> profile;
        if (points.size() < 2 || pressure.size() != points.size()) {
            return profile;
        }
        const auto [least, most] = std::minmax_element(pressure.begin(), pressure.end());
        if (*most - *least < 0.05f) {
            return profile;
        }
        std::vector<double> at(points.size(), 0.0);
        for (std::size_t i = 1; i < points.size(); ++i) {
            at[i] = at[i - 1] + std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
        }
        if (!(at.back() > 0.0)) {
            return profile;
        }
        double lastT = -1.0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const double t = at[i] / at.back();
            if (i == 0 || i + 1 == points.size() || t - lastT >= 0.04) {
                WidthPoint point;
                point.t = t;
                point.left = point.right = double(pressure[i]);
                profile.push_back(point);
                lastT = t;
            }
        }
        return profile;
    }

    const char* brushKindName(BrushKind kind) {
        static const char* kNames[] = {"Calligraphic", "Scatter", "Art", "Graph"};
        return kNames[std::size_t(kind)];
    }

    std::vector<double> BrushDef::params() const {
        return {angle, roundness, spacing, scatter, sizeVariation, rotation, followPath ? 1.0 : 0.0,
                double(fit), headGuide, tailGuide, double(corner), cornerRadius, miterLimit, breakAngle,
                double(direction), flipAlong ? 1.0 : 0.0, flipAcross ? 1.0 : 0.0};
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
        if (values.size() > 7 && std::isfinite(values[7])) {
            fit = values[7] >= 2.0 ? BrushFit::Repeat : values[7] >= 1.0 ? BrushFit::Guided : BrushFit::Stretch;
        }
        take(8, headGuide, 0.0, 1.0);
        take(9, tailGuide, 0.0, 1.0);
        if (tailGuide < headGuide) {
            std::swap(headGuide, tailGuide);
        }
        if (values.size() > 10 && std::isfinite(values[10])) {
            corner = BrushCorner(int(std::min(3.0, std::max(0.0, std::floor(values[10])))));
        }
        take(11, cornerRadius, 0.0, 1000.0);
        take(12, miterLimit, 1.0, 20.0);
        take(13, breakAngle, 5.0, 175.0);
        if (values.size() > 14 && std::isfinite(values[14])) {
            direction = values[14] >= 1.0 ? BrushDirection::TopToBottom : BrushDirection::LeftToRight;
        }
        if (values.size() > 15) {
            flipAlong = values[15] != 0.0;
        }
        if (values.size() > 16) {
            flipAcross = values[16] != 0.0;
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

    namespace {
        // The width profile's multiplier at `t` (both sides together: the nib and the copies do not have a side), and its largest value.
        double profileAt(const std::vector<WidthPoint>& widths, double t) {
            return 0.5 * (widthScaleAt(widths, t, true) + widthScaleAt(widths, t, false));
        }
        double profileMost(const std::vector<WidthPoint>& widths) {
            double most = 0.0;
            for (const WidthPoint& p : widths) {
                most = std::max(most, std::max(p.left, p.right));
            }
            return widths.empty() ? 1.0 : most;
        }
    }

    double BrushDef::reach(double strokeWidth, const std::vector<WidthPoint>& widths) const {
        strokeWidth *= profileMost(widths);
        if (kind == BrushKind::Graph) {
            return strokeWidth * 1.25;   // dots stay inside the ribbon; leaf stamps stray a little past it
        }
        if (kind == BrushKind::Calligraphic || !art) {
            return strokeWidth * 0.5;
        }
        const double w = art->box.x1 - art->box.x0, h = art->box.y1 - art->box.y0;
        if (kind == BrushKind::Art) {
            return (direction == BrushDirection::TopToBottom ? w : h) * strokeWidth * 0.5;
        }
        return 0.5 * std::hypot(w, h) * strokeWidth * (1.0 + sizeVariation / 100.0) + scatter / 100.0 * h * strokeWidth;
    }

    BLPath BrushDef::nibOutline(const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths) const {
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
                // The nib is as big as the profile says at the two ends of the piece.
                const double fromScale = profileAt(widths, m.at[i - 1] / std::max(m.total, 1e-9)), toScale = profileAt(widths, m.at[i] / std::max(m.total, 1e-9));
                for (const BLPoint& p : nib) {
                    both.push_back(BLPoint(p.x * fromScale + last.x, p.y * fromScale + last.y));
                    both.push_back(BLPoint(p.x * toScale + next.x, p.y * toScale + next.y));
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

    std::vector<BrushPart> BrushDef::bendAlong(const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths) const {
        return art ? bendParts(parts(), art->box, path, strokeWidth, widths) : std::vector<BrushPart>();
    }

    std::vector<BrushPart> BrushDef::bendParts(const std::vector<BrushPart>& artParts, const BLBox& box, const BLPath& path,
        double strokeWidth, const std::vector<WidthPoint>& widths) const {
        std::vector<BrushPart> result;
        if (!(strokeWidth > 0.0)) {
            return result;
        }
        // `along` is the artwork's size in the direction of the path, `across` its size at right angles to it.
        const bool vertical = direction == BrushDirection::TopToBottom;
        const double alongSize = vertical ? box.y1 - box.y0 : box.x1 - box.x0, acrossSize = vertical ? box.x1 - box.x0 : box.y1 - box.y0;
        const double alongStart = vertical ? box.y0 : box.x0, cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
        if (!(alongSize > 0.0)) {
            return result;
        }
        std::vector<MeasuredPath> pieces;
        for (const MeasuredPath& original : measurePath(path)) {
            if (corner == BrushCorner::Break) {
                for (MeasuredPath& piece : splitAtCorners(original, breakAngle * kPi / 180.0)) {
                    if (piece.total > 1e-9) {
                        pieces.push_back(std::move(piece));
                    }
                }
            } else if (corner == BrushCorner::Smooth) {
                pieces.push_back(roundCorners(original, cornerRadius / 100.0 * acrossSize * strokeWidth));
            } else {
                pieces.push_back(original);
            }
        }
        for (const MeasuredPath& m : pieces) {
            // For Miter: tan(turn / 2) at each vertex, limited so that the point is at most miterLimit long.
            std::vector<double> halfTan(m.points.size(), 0.0);
            if (corner == BrushCorner::Miter) {
                const double limit = std::sqrt(std::max(miterLimit * miterLimit - 1.0, 0.0));
                for (std::size_t i = 1; i + 1 < m.points.size(); ++i) {
                    halfTan[i] = std::min(limit, std::max(-limit, std::tan(turnAt(m, i) * 0.5)));
                }
            }
            // The point `across` to the side of the path at length `s`. A miter slides it along its segment so that the offset lines of
            // two segments meet at the corner (the outside is lengthened, the inside shortened).
            auto place = [&](double s, double across) {
                BLPoint p, t;
                if (corner != BrushCorner::Miter) {
                    sampleMeasured(m, s, p, t);
                    return BLPoint(p.x + t.y * across, p.y - t.x * across);
                }
                s = std::min(std::max(s, 0.0), m.total);
                std::size_t i = std::size_t(std::upper_bound(m.at.begin(), m.at.end(), s) - m.at.begin());
                i = std::min(std::max<std::size_t>(i, 1), m.points.size() - 1);
                const BLPoint &a = m.points[i - 1], &b = m.points[i];
                const double length = m.at[i] - m.at[i - 1];
                if (!(length > 0.0)) {
                    return a;
                }
                t = BLPoint((b.x - a.x) / length, (b.y - a.y) / length);
                const double start = std::max(across * halfTan[i - 1], -length * 0.5), end = std::max(across * halfTan[i], -length * 0.5);
                const double on = -start + (s - m.at[i - 1]) / length * (length + start + end);
                return BLPoint(a.x + t.x * on + t.y * across, a.y + t.y * on - t.x * across);
            };
            // Where along the path (0 .. m.total) a position in the artwork (0 .. 1 of its width) lands, for one copy.
            std::size_t copies = 1;
            double headEnd = 0.0, tailStart = 1.0, headLen = 0.0, tailLen = 0.0;   // the guides and the lengths the rigid parts take
            if (fit == BrushFit::Repeat) {
                copies = std::size_t(std::min(2000.0, std::max(1.0, std::round(m.total / (alongSize * strokeWidth)))));
            } else if (fit == BrushFit::Guided) {
                headEnd = headGuide;
                tailStart = tailGuide;
                headLen = headGuide * alongSize * strokeWidth;
                tailLen = (1.0 - tailGuide) * alongSize * strokeWidth;
                if (headLen + tailLen > m.total) {   // too short for both: they shrink together
                    const double shrink = m.total / (headLen + tailLen);
                    headLen *= shrink;
                    tailLen *= shrink;
                }
            }
            const double span = m.total / double(copies);
            auto along = [&](double u) {
                if (fit != BrushFit::Guided) {
                    return u * span;
                }
                if (u < headEnd) {
                    return headEnd > 0.0 ? u / headEnd * headLen : 0.0;
                }
                if (u > tailStart) {
                    return tailStart < 1.0 ? m.total - tailLen + (u - tailStart) / (1.0 - tailStart) * tailLen : m.total;
                }
                return tailStart > headEnd ? headLen + (u - headEnd) / (tailStart - headEnd) * (m.total - headLen - tailLen) : headLen;
            };
            for (std::size_t copy = 0; copy < copies; ++copy) {
                // The artwork x runs along the path, its y across: up is to the left of the direction of travel.
                auto map = [&](double x, double y) {
                    // Up in the artwork is to the left of the direction of travel; a top-to-bottom artwork is turned a quarter so that
                    // its top comes first.
                    double offset = vertical ? x - cx : cy - y;
                    double at = along(((vertical ? y : x) - alongStart) / alongSize);
                    if (flipAlong) {
                        at = span - at;
                    }
                    // The profile says how tall the artwork is here, per side of the path (it is the same on both for pen pressure).
                    const double here = double(copy) * span + at, across = flipAcross ? -offset : offset;
                    const double scale = widths.empty() ? 1.0 : widthScaleAt(widths, here / std::max(m.total, 1e-9), across >= 0.0);
                    return place(here, across * strokeWidth * scale);
                };
                for (const BrushPart& part : artParts) {
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
                            const double piece = alongSize / 150.0;
                            const int pieces = std::min(300, std::max(1, int(std::ceil(length / piece))));
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
        }
        return result;
    }

    std::vector<BLMatrix2D> BrushDef::placements(const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths) const {
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
                const double size = strokeWidth * profileAt(widths, step * i / std::max(m.total, 1e-9)) * (1.0 + (noise(seed * 3u + 1u) - 0.5) * 2.0 * sizeVariation / 100.0);
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
