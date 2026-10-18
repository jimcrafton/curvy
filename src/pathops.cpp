#include "curvy/pathops.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace curvy {

    namespace {

        // Every point the boolean operations touch is moved onto this grid first (a hundred-thousandth of a point), so two points that
        // are "the same" really are - the pieces of two edges that meet at a crossing share one exact point.
        constexpr double kGrid = 1e-5;

        BLPoint snap(const BLPoint& p) { return BLPoint(std::round(p.x / kGrid) * kGrid, std::round(p.y / kGrid) * kGrid); }

        struct Key {
            std::int64_t x = 0, y = 0;
            bool operator==(const Key& o) const { return x == o.x && y == o.y; }
            bool operator!=(const Key& o) const { return !(*this == o); }
            bool operator<(const Key& o) const { return x != o.x ? x < o.x : y < o.y; }
        };
        struct KeyHash {
            std::size_t operator()(const Key& k) const { return std::size_t(k.x * 73856093LL) ^ std::size_t(k.y * 19349663LL); }
        };
        Key keyOf(const BLPoint& p) { return Key{std::llround(p.x / kGrid), std::llround(p.y / kGrid)}; }

        double cross(const BLPoint& a, const BLPoint& b) { return a.x * b.y - a.y * b.x; }
        BLPoint minus(const BLPoint& a, const BLPoint& b) { return BLPoint(a.x - b.x, a.y - b.y); }
        BLPoint lerp(const BLPoint& a, const BLPoint& b, double t) { return BLPoint(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }

        // The winding number of `loops` around `p`: 0 outside, otherwise inside (non-zero rule).
        int windingAt(const BLPoint& p, const Loops& loops) {
            int winding = 0;
            for (const Loop& loop : loops) {
                std::size_t n = loop.size();
                for (std::size_t i = 0; i < n; ++i) {
                    const BLPoint& a = loop[i];
                    const BLPoint& b = loop[(i + 1) % n];
                    if (a.y <= p.y) {
                        if (b.y > p.y && cross(minus(b, a), minus(p, a)) > 0.0) { ++winding; }
                    } else if (b.y <= p.y && cross(minus(b, a), minus(p, a)) < 0.0) {
                        --winding;
                    }
                }
            }
            return winding;
        }

        // How many straight pieces a curve gets so it stays within `tolerance` (a heuristic on the length of its control polygon).
        int piecesFor(double controlLength, double tolerance) {
            return std::min(256, std::max(4, int(std::ceil(std::sqrt(controlLength / (8.0 * tolerance))))));
        }

        struct Line {
            std::vector<BLPoint> points;
            bool closed = false;
        };

        std::vector<Line> flatten(const BLPath& path, double tolerance) {
            std::vector<Line> result;
            const std::uint8_t* cmd = path.command_data();
            const BLPoint* v = path.vertex_data();
            std::size_t n = path.size();
            for (std::size_t i = 0; i < n; ++i) {
                switch (cmd[i]) {
                    case BL_PATH_CMD_MOVE:
                        result.emplace_back();
                        result.back().points.push_back(v[i]);
                        break;
                    case BL_PATH_CMD_ON:
                        if (!result.empty()) { result.back().points.push_back(v[i]); }
                        break;
                    case BL_PATH_CMD_QUAD:
                        if (i + 1 < n && !result.empty()) {
                            BLPoint p0 = result.back().points.back(), c = v[i], p1 = v[i + 1];
                            int steps = piecesFor(std::hypot(c.x - p0.x, c.y - p0.y) + std::hypot(p1.x - c.x, p1.y - c.y), tolerance);
                            for (int s = 1; s <= steps; ++s) {
                                double t = double(s) / steps, u = 1.0 - t;
                                result.back().points.push_back(BLPoint(u * u * p0.x + 2 * u * t * c.x + t * t * p1.x, u * u * p0.y + 2 * u * t * c.y + t * t * p1.y));
                            }
                        }
                        i += 1;
                        break;
                    case BL_PATH_CMD_CUBIC:
                        if (i + 2 < n && !result.empty()) {
                            BLPoint p0 = result.back().points.back(), c1 = v[i], c2 = v[i + 1], p1 = v[i + 2];
                            int steps = piecesFor(std::hypot(c1.x - p0.x, c1.y - p0.y) + std::hypot(c2.x - c1.x, c2.y - c1.y) + std::hypot(p1.x - c2.x, p1.y - c2.y), tolerance);
                            for (int s = 1; s <= steps; ++s) {
                                double t = double(s) / steps, u = 1.0 - t;
                                result.back().points.push_back(BLPoint(u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p1.x,
                                    u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p1.y));
                            }
                        }
                        i += 2;
                        break;
                    case BL_PATH_CMD_CLOSE:
                        if (!result.empty()) {
                            result.back().closed = true;
                            result.emplace_back();   // a path may carry on from the start point; a MOVE normally follows
                            result.back().points.push_back(result[result.size() - 2].points.front());
                        }
                        break;
                    default:
                        break;
                }
            }
            result.erase(std::remove_if(result.begin(), result.end(), [](const Line& l) { return l.points.size() < 2; }), result.end());
            return result;
        }

        // Without a repeated last point, and without consecutive duplicates.
        Loop tidyLoop(const std::vector<BLPoint>& points) {
            Loop loop;
            for (const BLPoint& p : points) {
                if (loop.empty() || std::hypot(p.x - loop.back().x, p.y - loop.back().y) > 1e-9) { loop.push_back(p); }
            }
            while (loop.size() > 1 && std::hypot(loop.front().x - loop.back().x, loop.front().y - loop.back().y) <= 1e-9) { loop.pop_back(); }
            return loop;
        }

    }

    double signedArea(const Loop& loop) {
        double area = 0.0;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            area += cross(loop[i], loop[(i + 1) % loop.size()]);
        }
        return area * 0.5;
    }

    Loops flattenToLoops(const BLPath& path, double tolerance) {
        Loops loops;
        for (Line& line : flatten(path, tolerance)) {
            Loop loop = tidyLoop(line.points);
            if (loop.size() >= 3) { loops.push_back(std::move(loop)); }
        }
        return loops;
    }

    std::vector<std::vector<BLPoint>> flattenToLines(const BLPath& path, std::vector<bool>& closed, double tolerance) {
        std::vector<std::vector<BLPoint>> lines;
        closed.clear();
        for (Line& line : flatten(path, tolerance)) {
            lines.push_back(line.closed ? tidyLoop(line.points) : line.points);
            closed.push_back(line.closed);
        }
        return lines;
    }

    BLPath loopsToPath(const Loops& loops) {
        BLPath path;
        for (const Loop& loop : loops) {
            if (loop.size() < 3) { continue; }
            path.move_to(loop[0]);
            for (std::size_t i = 1; i < loop.size(); ++i) { path.line_to(loop[i]); }
            path.close();
        }
        return path;
    }

    bool pointInLoops(const BLPoint& point, const Loops& loops) {
        return windingAt(point, loops) != 0;
    }

    // ------------------------------------------------------------------ boolean operations
    //
    // The method: put every edge of both shapes in one pool and cut each one wherever it crosses, touches or overlaps another. Each little
    // piece now lies wholly inside, wholly outside, or exactly on the boundary of each shape, so it is decided by looking at the two
    // sides of its middle: the piece is part of the result's outline when the result is "in" on one side and "out" on the other. Pieces
    // that were doubled (the same edge in both shapes) collapse into one. The kept pieces, each pointing so that the inside is on its
    // left, are then chained into closed outlines.

    namespace {

        struct Edge {
            BLPoint a, b;
        };

        bool resultInside(BoolOp op, bool inA, bool inB) {
            switch (op) {
                case BoolOp::Union: return inA || inB;
                case BoolOp::Intersect: return inA && inB;
                case BoolOp::Subtract: return inA && !inB;
                default: return inA != inB;
            }
        }

        double distanceToSegment(const BLPoint& p, const BLPoint& a, const BLPoint& b) {
            BLPoint ab = minus(b, a);
            double len2 = ab.x * ab.x + ab.y * ab.y;
            double u = len2 > 0.0 ? std::min(1.0, std::max(0.0, ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2)) : 0.0;
            return std::hypot(p.x - (a.x + ab.x * u), p.y - (a.y + ab.y * u));
        }

        bool boxesTouch(const Edge& e, const Edge& f, double pad) {
            return std::min(e.a.x, e.b.x) <= std::max(f.a.x, f.b.x) + pad && std::max(e.a.x, e.b.x) >= std::min(f.a.x, f.b.x) - pad &&
                std::min(e.a.y, e.b.y) <= std::max(f.a.y, f.b.y) + pad && std::max(e.a.y, e.b.y) >= std::min(f.a.y, f.b.y) - pad;
        }

        // The pieces of `edges` after cutting each at every crossing with another and at every vertex that lies on it.
        std::vector<Edge> splitEdges(const std::vector<Edge>& edges) {
            std::size_t n = edges.size();
            std::vector<std::vector<BLPoint>> cuts(n);
            std::vector<BLPoint> vertices;
            {
                std::unordered_map<Key, bool, KeyHash> seen;
                for (const Edge& e : edges) {
                    for (const BLPoint& p : {e.a, e.b}) {
                        if (seen.emplace(keyOf(p), true).second) { vertices.push_back(p); }
                    }
                }
            }
            const double onEdge = 5e-7;
            for (std::size_t i = 0; i < n; ++i) {
                const Edge& e = edges[i];
                BLPoint r = minus(e.b, e.a);
                // Vertices lying on this edge (an edge that ends on, or runs along, another).
                for (const BLPoint& v : vertices) {
                    if (v.x < std::min(e.a.x, e.b.x) - onEdge || v.x > std::max(e.a.x, e.b.x) + onEdge || v.y < std::min(e.a.y, e.b.y) - onEdge ||
                        v.y > std::max(e.a.y, e.b.y) + onEdge) {
                        continue;
                    }
                    if (keyOf(v) == keyOf(e.a) || keyOf(v) == keyOf(e.b)) { continue; }
                    if (distanceToSegment(v, e.a, e.b) <= onEdge) { cuts[i].push_back(v); }
                }
                // Proper crossings with the edges after it (both get the same, snapped, point).
                for (std::size_t j = i + 1; j < n; ++j) {
                    const Edge& f = edges[j];
                    if (!boxesTouch(e, f, onEdge)) { continue; }
                    BLPoint s = minus(f.b, f.a);
                    double denom = cross(r, s);
                    if (std::fabs(denom) <= 1e-14 * std::hypot(r.x, r.y) * std::hypot(s.x, s.y)) { continue; }   // parallel: handled by the vertices
                    BLPoint d = minus(f.a, e.a);
                    double t = cross(d, s) / denom, u = cross(d, r) / denom;
                    if (t <= 0.0 || t >= 1.0 || u <= 0.0 || u >= 1.0) { continue; }
                    BLPoint p = snap(BLPoint(e.a.x + r.x * t, e.a.y + r.y * t));
                    if (keyOf(p) != keyOf(e.a) && keyOf(p) != keyOf(e.b)) { cuts[i].push_back(p); }
                    if (keyOf(p) != keyOf(f.a) && keyOf(p) != keyOf(f.b)) { cuts[j].push_back(p); }
                }
            }
            std::vector<Edge> pieces;
            for (std::size_t i = 0; i < n; ++i) {
                const Edge& e = edges[i];
                BLPoint r = minus(e.b, e.a);
                double len2 = r.x * r.x + r.y * r.y;
                std::vector<std::pair<double, BLPoint>> along;
                along.push_back({0.0, e.a});
                for (const BLPoint& p : cuts[i]) { along.push_back({((p.x - e.a.x) * r.x + (p.y - e.a.y) * r.y) / len2, p}); }
                along.push_back({1.0, e.b});
                std::sort(along.begin(), along.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
                for (std::size_t k = 0; k + 1 < along.size(); ++k) {
                    if (keyOf(along[k].second) == keyOf(along[k + 1].second)) { continue; }
                    pieces.push_back(Edge{along[k].second, along[k + 1].second});
                }
            }
            return pieces;
        }

        // The loops with every point moved onto the grid, repeated points removed and degenerate loops dropped. The side tests below must use
        // these same snapped loops: moving a point by up to half a grid step is more than the distance they probe at.
        Loops snapLoops(const Loops& loops) {
            Loops out;
            for (const Loop& loop : loops) {
                Loop snapped;
                for (const BLPoint& p : loop) {
                    BLPoint q = snap(p);
                    if (snapped.empty() || keyOf(q) != keyOf(snapped.back())) { snapped.push_back(q); }
                }
                while (snapped.size() > 1 && keyOf(snapped.front()) == keyOf(snapped.back())) { snapped.pop_back(); }
                if (snapped.size() >= 3) { out.push_back(std::move(snapped)); }
            }
            return out;
        }

        void appendEdges(const Loops& loops, std::vector<Edge>& edges) {
            for (const Loop& loop : loops) {
                for (std::size_t i = 0; i < loop.size(); ++i) { edges.push_back(Edge{loop[i], loop[(i + 1) % loop.size()]}); }
            }
        }

        // Drops points that sit on the straight line between their neighbours, then loops that collapsed.
        Loops cleanLoops(Loops loops) {
            Loops out;
            for (Loop& loop : loops) {
                bool again = true;
                while (again && loop.size() >= 3) {
                    again = false;
                    for (std::size_t i = 0; i < loop.size(); ++i) {
                        const BLPoint& prev = loop[(i + loop.size() - 1) % loop.size()];
                        const BLPoint& next = loop[(i + 1) % loop.size()];
                        BLPoint u = minus(loop[i], prev), w = minus(next, loop[i]);
                        double scale = std::hypot(u.x, u.y) * std::hypot(w.x, w.y);
                        if (scale > 0.0 && std::fabs(cross(u, w)) <= 1e-9 * scale && u.x * w.x + u.y * w.y > 0.0) {
                            loop.erase(loop.begin() + std::ptrdiff_t(i));
                            again = true;
                            break;
                        }
                    }
                }
                if (loop.size() >= 3 && std::fabs(signedArea(loop)) > 1e-6) { out.push_back(std::move(loop)); }
            }
            return out;
        }

    }

    Loops combineLoops(const Loops& rawA, const Loops& rawB, BoolOp op) {
        const Loops a = snapLoops(rawA), b = snapLoops(rawB);
        std::vector<Edge> edges;
        appendEdges(a, edges);
        appendEdges(b, edges);
        if (edges.empty()) {
            return Loops();
        }
        double x0 = edges[0].a.x, x1 = x0, y0 = edges[0].a.y, y1 = y0;
        for (const Edge& e : edges) {
            x0 = std::min(x0, std::min(e.a.x, e.b.x)); x1 = std::max(x1, std::max(e.a.x, e.b.x));
            y0 = std::min(y0, std::min(e.a.y, e.b.y)); y1 = std::max(y1, std::max(e.a.y, e.b.y));
        }
        // How far to step off an edge to see which side is which: more than a piece can sit off the true edge it was cut from (a snapped
        // crossing moves up to ~1.5 grid steps), so the step always lands on the correct side of the shapes' own edges.
        const double step = 6.0 * kGrid + 1e-10 * std::max(x1 - x0, y1 - y0);

        // The kept pieces, each pointing so that the result is on its left. Doubled pieces (the same edge twice) are kept once.
        std::vector<Edge> kept;
        std::set<std::pair<Key, Key>> have;
        for (const Edge& piece : splitEdges(edges)) {
            BLPoint dir = minus(piece.b, piece.a);
            double len = std::hypot(dir.x, dir.y);
            if (len <= 0.0) { continue; }
            BLPoint left(-dir.y / len, dir.x / len);
            BLPoint mid((piece.a.x + piece.b.x) * 0.5, (piece.a.y + piece.b.y) * 0.5);
            BLPoint onLeft(mid.x + left.x * step, mid.y + left.y * step), onRight(mid.x - left.x * step, mid.y - left.y * step);
            bool leftIn = resultInside(op, windingAt(onLeft, a) != 0, windingAt(onLeft, b) != 0);
            bool rightIn = resultInside(op, windingAt(onRight, a) != 0, windingAt(onRight, b) != 0);
            if (leftIn == rightIn) { continue; }
            Edge directed = leftIn ? piece : Edge{piece.b, piece.a};
            if (have.insert({keyOf(directed.a), keyOf(directed.b)}).second) { kept.push_back(directed); }
        }

        // Chain them into closed outlines. At a point where several outlines meet, the sharpest turn to the right is taken, which keeps
        // pieces that only touch at a point as separate outlines.
        std::unordered_map<Key, std::vector<std::size_t>, KeyHash> from;
        for (std::size_t i = 0; i < kept.size(); ++i) { from[keyOf(kept[i].a)].push_back(i); }
        std::vector<bool> used(kept.size(), false);
        Loops loops;
        for (std::size_t first = 0; first < kept.size(); ++first) {
            if (used[first]) { continue; }
            Loop loop;
            std::vector<std::size_t> taken;
            std::size_t current = first;
            bool closed = false;
            while (true) {
                used[current] = true;
                taken.push_back(current);
                loop.push_back(kept[current].a);
                Key end = keyOf(kept[current].b);
                if (end == keyOf(kept[first].a)) { closed = true; break; }
                auto it = from.find(end);
                if (it == from.end()) { break; }
                BLPoint in = minus(kept[current].b, kept[current].a);
                double best = 1e9;
                std::size_t next = kept.size();
                for (std::size_t candidate : it->second) {
                    if (used[candidate]) { continue; }
                    BLPoint out = minus(kept[candidate].b, kept[candidate].a);
                    double turn = std::atan2(cross(in, out), in.x * out.x + in.y * out.y);
                    if (turn < best) { best = turn; next = candidate; }
                }
                if (next == kept.size()) { break; }
                current = next;
            }
            if (closed) {
                loops.push_back(std::move(loop));
            }
        }
        return cleanLoops(std::move(loops));
    }

    std::vector<Loops> splitIntoRegions(const Loops& loops) {
        std::vector<Loops> regions;
        std::vector<double> areas;
        std::vector<std::size_t> outers;   // indices into `loops` of the positive outlines
        for (std::size_t i = 0; i < loops.size(); ++i) {
            double area = signedArea(loops[i]);
            if (area > 0.0) {
                outers.push_back(i);
                regions.push_back(Loops{loops[i]});
                areas.push_back(area);
            }
        }
        for (const Loop& hole : loops) {
            if (signedArea(hole) >= 0.0 || hole.size() < 2) { continue; }
            // A point just inside the filled side of the hole's first edge (the left, as for every outline here).
            BLPoint dir = minus(hole[1], hole[0]);
            double len = std::hypot(dir.x, dir.y);
            if (len <= 0.0) { continue; }
            BLPoint probe((hole[0].x + hole[1].x) * 0.5 - dir.y / len * 1e-4, (hole[0].y + hole[1].y) * 0.5 + dir.x / len * 1e-4);
            std::size_t owner = regions.size();
            for (std::size_t k = 0; k < regions.size(); ++k) {
                if (windingAt(probe, regions[k]) != 0 && (owner == regions.size() || areas[k] < areas[owner])) { owner = k; }
            }
            if (owner != regions.size()) { regions[owner].push_back(hole); }
        }
        return regions;
    }

    Loops strokeToLoops(const std::vector<BLPoint>& line, double width) {
        BLPath path;
        if (line.empty() || width <= 0.0) {
            return Loops();
        }
        bool single = true;
        for (const BLPoint& p : line) { single = single && std::hypot(p.x - line[0].x, p.y - line[0].y) < 1e-9; }
        if (single) {
            path.add_circle(BLCircle(line[0].x, line[0].y, width * 0.5));
            return flattenToLoops(path, 0.02);
        }
        BLPath open;
        open.move_to(line[0]);
        for (std::size_t i = 1; i < line.size(); ++i) { open.line_to(line[i]); }
        BLStrokeOptions stroke;
        stroke.width = width;
        stroke.start_cap = stroke.end_cap = BL_STROKE_CAP_ROUND;
        stroke.join = BL_STROKE_JOIN_ROUND;
        if (path.add_stroked_path(open, stroke, BLApproximationOptions()) != BL_SUCCESS) {
            return Loops();
        }
        return flattenToLoops(path, 0.02);
    }

    std::vector<std::vector<BLPoint>> clipLineOutside(const std::vector<BLPoint>& line, bool closed, const Loops& region, bool& touched) {
        touched = false;
        std::vector<std::vector<BLPoint>> result;
        std::size_t n = line.size();
        if (n < 2) {
            return result;
        }
        std::size_t segments = closed ? n : n - 1;
        // Each segment cut at its crossings with the region's edges; each piece is kept when its middle is outside.
        struct Piece { BLPoint a, b; bool outside; };
        std::vector<Piece> pieces;
        for (std::size_t i = 0; i < segments; ++i) {
            BLPoint a = line[i], b = line[(i + 1) % n];
            BLPoint r = minus(b, a);
            std::vector<double> ts{0.0, 1.0};
            for (const Loop& loop : region) {
                for (std::size_t k = 0; k < loop.size(); ++k) {
                    const BLPoint& c = loop[k];
                    const BLPoint& d = loop[(k + 1) % loop.size()];
                    BLPoint s = minus(d, c);
                    double denom = cross(r, s);
                    if (std::fabs(denom) < 1e-14) { continue; }
                    BLPoint w = minus(c, a);
                    double t = cross(w, s) / denom, u = cross(w, r) / denom;
                    if (t > 0.0 && t < 1.0 && u >= 0.0 && u <= 1.0) { ts.push_back(t); }
                }
            }
            std::sort(ts.begin(), ts.end());
            for (std::size_t k = 0; k + 1 < ts.size(); ++k) {
                if (ts[k + 1] - ts[k] < 1e-12) { continue; }
                BLPoint p = lerp(a, b, ts[k]), q = lerp(a, b, ts[k + 1]);
                bool outside = windingAt(lerp(p, q, 0.5), region) == 0;
                touched = touched || !outside;
                pieces.push_back(Piece{p, q, outside});
            }
        }
        if (!touched) {
            return result;
        }
        std::vector<std::vector<BLPoint>> runs;
        for (const Piece& piece : pieces) {
            if (!piece.outside) { runs.emplace_back(); continue; }
            if (runs.empty() || runs.back().empty()) {
                if (runs.empty()) { runs.emplace_back(); }
                runs.back().push_back(piece.a);
            }
            runs.back().push_back(piece.b);
        }
        // A closed line that was cut open: the run that ends where the line began continues the one that starts there.
        if (closed && runs.size() >= 2 && !pieces.empty() && pieces.front().outside && pieces.back().outside && runs.front().size() >= 2 && runs.back().size() >= 2) {
            std::vector<BLPoint> joined = runs.back();
            joined.insert(joined.end(), runs.front().begin() + 1, runs.front().end());
            runs.front() = joined;
            runs.pop_back();
        }
        for (std::vector<BLPoint>& run : runs) {
            if (run.size() >= 2) { result.push_back(std::move(run)); }
        }
        return result;
    }

    std::vector<Loops> cutAlongLine(const Loops& region, const std::vector<BLPoint>& cut) {
        std::vector<Loops> pieces;
        if (cut.size() < 2 || region.empty()) {
            return pieces;
        }
        double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
        for (const std::vector<BLPoint>* set : {&cut}) {
            for (const BLPoint& p : *set) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
        }
        for (const Loop& loop : region) {
            for (const BLPoint& p : loop) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
        }
        const double reach = 4.0 * std::hypot(x1 - x0, y1 - y0) + 10.0;
        auto unit = [](const BLPoint& from, const BLPoint& to) {
            BLPoint d = minus(to, from);
            double len = std::hypot(d.x, d.y);
            return len > 0.0 ? BLPoint(d.x / len, d.y / len) : BLPoint(1.0, 0.0);
        };
        // The cut carried on past both ends, so the two sides are properly separated.
        std::vector<BLPoint> line = cut;
        BLPoint head = unit(cut[1], cut[0]), tail = unit(cut[cut.size() - 2], cut.back());
        line.insert(line.begin(), BLPoint(cut[0].x + head.x * reach, cut[0].y + head.y * reach));
        line.push_back(BLPoint(cut.back().x + tail.x * reach, cut.back().y + tail.y * reach));
        BLPoint startDir = unit(line[0], line[1]), endDir = unit(line[line.size() - 2], line.back());
        BLPoint startLeft(-startDir.y, startDir.x), endLeft(-endDir.y, endDir.x);
        Loops sides[2];
        for (int side = 0; side < 2; ++side) {
            double sign = side == 0 ? 1.0 : -1.0;
            Loop polygon = line;
            polygon.push_back(BLPoint(line.back().x + endLeft.x * reach * sign, line.back().y + endLeft.y * reach * sign));
            polygon.push_back(BLPoint(line.front().x + startLeft.x * reach * sign, line.front().y + startLeft.y * reach * sign));
            sides[side] = combineLoops(region, Loops{polygon}, BoolOp::Intersect);
        }
        if (sides[0].empty() || sides[1].empty()) {
            return pieces;   // the line does not divide the shape
        }
        for (const Loops& side : sides) {
            for (Loops& piece : splitIntoRegions(side)) { pieces.push_back(std::move(piece)); }
        }
        return pieces.size() >= 2 ? pieces : std::vector<Loops>();
    }

    // ------------------------------------------------------------------ scissors

    namespace {
        struct Segment {
            BLPoint p0, p1, p2, p3;
            bool curved = false;
        };

        Segment segmentOf(const SubPath& sub, std::size_t i) {
            const PenAnchor& a = sub.anchors[i];
            const PenAnchor& b = sub.anchors[(i + 1) % sub.anchors.size()];
            Segment s;
            s.p0 = a.point;
            s.p1 = a.hasOut ? a.out : a.point;
            s.p2 = b.hasIn ? b.in : b.point;
            s.p3 = b.point;
            s.curved = a.hasOut || b.hasIn;
            return s;
        }

        BLPoint pointOn(const Segment& s, double t) {
            if (!s.curved) {
                return lerp(s.p0, s.p3, t);   // a straight segment is walked evenly (a cubic with its handles on the ends is not)
            }
            double u = 1.0 - t;
            return BLPoint(u * u * u * s.p0.x + 3 * u * u * t * s.p1.x + 3 * u * t * t * s.p2.x + t * t * t * s.p3.x,
                u * u * u * s.p0.y + 3 * u * u * t * s.p1.y + 3 * u * t * t * s.p2.y + t * t * t * s.p3.y);
        }
    }

    bool nearestOnSubPath(const SubPath& sub, const BLPoint& point, std::size_t& segment, double& t, double& distance) {
        std::size_t n = sub.anchors.size();
        if (n < 2) {
            return false;
        }
        std::size_t segments = sub.closed ? n : n - 1;
        distance = 1e300;
        for (std::size_t i = 0; i < segments; ++i) {
            Segment s = segmentOf(sub, i);
            const int samples = s.curved ? 48 : 1;
            double bestT = 0.0, bestD = 1e300;
            for (int k = 0; k <= samples; ++k) {
                double u = double(k) / samples;
                BLPoint p = pointOn(s, u);
                double d = std::hypot(p.x - point.x, p.y - point.y);
                if (d < bestD) { bestD = d; bestT = u; }
            }
            if (!s.curved) {
                // A straight segment: the exact foot of the perpendicular.
                BLPoint ab = minus(s.p3, s.p0);
                double len2 = ab.x * ab.x + ab.y * ab.y;
                bestT = len2 > 0.0 ? std::min(1.0, std::max(0.0, ((point.x - s.p0.x) * ab.x + (point.y - s.p0.y) * ab.y) / len2)) : 0.0;
                BLPoint p = pointOn(s, bestT);
                bestD = std::hypot(p.x - point.x, p.y - point.y);
            } else {
                double lo = std::max(0.0, bestT - 1.0 / samples), hi = std::min(1.0, bestT + 1.0 / samples);
                for (int iteration = 0; iteration < 30; ++iteration) {
                    double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
                    BLPoint p1 = pointOn(s, m1), p2 = pointOn(s, m2);
                    if (std::hypot(p1.x - point.x, p1.y - point.y) < std::hypot(p2.x - point.x, p2.y - point.y)) { hi = m2; } else { lo = m1; }
                }
                bestT = (lo + hi) * 0.5;
                BLPoint p = pointOn(s, bestT);
                bestD = std::hypot(p.x - point.x, p.y - point.y);
            }
            if (bestD < distance) { distance = bestD; segment = i; t = bestT; }
        }
        return true;
    }

    bool splitSubPath(const SubPath& sub, std::size_t segment, double t, std::vector<SubPath>& pieces) {
        pieces.clear();
        std::size_t n = sub.anchors.size();
        if (n < 2 || segment >= (sub.closed ? n : n - 1)) {
            return false;
        }
        std::vector<PenAnchor> anchors = sub.anchors;
        std::size_t cutAt;   // the anchor the path is cut at
        if (t <= 1e-3) {
            cutAt = segment;
        } else if (t >= 1.0 - 1e-3) {
            cutAt = (segment + 1) % n;
        } else {
            // A new anchor on the segment, made by splitting the curve there (de Casteljau) so its shape does not change.
            std::size_t next = (segment + 1) % n;
            Segment s = segmentOf(sub, segment);
            BLPoint q0 = lerp(s.p0, s.p1, t), q1 = lerp(s.p1, s.p2, t), q2 = lerp(s.p2, s.p3, t);
            BLPoint r0 = lerp(q0, q1, t), r1 = lerp(q1, q2, t), m = s.curved ? lerp(r0, r1, t) : lerp(s.p0, s.p3, t);
            PenAnchor mid;
            mid.point = m;
            if (s.curved) {
                anchors[segment].out = q0;
                anchors[segment].hasOut = true;
                anchors[next].in = q2;
                anchors[next].hasIn = true;
                mid.in = r0;
                mid.out = r1;
                mid.hasIn = mid.hasOut = true;
            }
            anchors.insert(anchors.begin() + std::ptrdiff_t(segment + 1), mid);
            cutAt = segment + 1;
            n += 1;
        }
        if (sub.closed) {
            // One open path from the cut all the way round and back to it.
            SubPath open;
            for (std::size_t k = 0; k < n; ++k) { open.anchors.push_back(anchors[(cutAt + k) % n]); }
            PenAnchor end = anchors[cutAt];
            open.anchors.front().hasIn = false;
            end.hasOut = false;
            open.anchors.push_back(end);
            pieces.push_back(std::move(open));
            return true;
        }
        if (cutAt == 0 || cutAt + 1 == n) {
            return false;   // the end of an open path: nothing to cut
        }
        SubPath first, second;
        first.anchors.assign(anchors.begin(), anchors.begin() + std::ptrdiff_t(cutAt + 1));
        first.anchors.back().hasOut = false;
        second.anchors.assign(anchors.begin() + std::ptrdiff_t(cutAt), anchors.end());
        second.anchors.front().hasIn = false;
        pieces.push_back(std::move(first));
        pieces.push_back(std::move(second));
        return true;
    }


    std::vector<ShapeCell> arrangeCells(const std::vector<Loops>& shapes) {
        std::vector<ShapeCell> cells;
        for (std::size_t s = 0; s < shapes.size(); ++s) {
            std::vector<ShapeCell> next;
            Loops outside = shapes[s];   // the part of this shape no cell covers yet
            for (ShapeCell& cell : cells) {
                Loops inside = combineLoops(cell.region, shapes[s], BoolOp::Intersect);
                Loops rest = combineLoops(cell.region, shapes[s], BoolOp::Subtract);
                if (!inside.empty()) {
                    ShapeCell both{std::move(inside), cell.members};
                    both.members.push_back(s);
                    next.push_back(std::move(both));
                    outside = combineLoops(outside, cell.region, BoolOp::Subtract);
                }
                if (!rest.empty()) {
                    next.push_back(ShapeCell{std::move(rest), cell.members});
                }
            }
            if (!outside.empty()) {
                next.push_back(ShapeCell{std::move(outside), {s}});
            }
            cells = std::move(next);
        }
        // Each cell may be several separate pieces; the user picks pieces.
        std::vector<ShapeCell> pieces;
        for (const ShapeCell& cell : cells) {
            for (Loops& region : splitIntoRegions(cell.region)) {
                double area = 0.0;
                for (const Loop& loop : region) {
                    area += signedArea(loop);
                }
                if (std::fabs(area) > 0.01) {
                    pieces.push_back(ShapeCell{std::move(region), cell.members});
                }
            }
        }
        return pieces;
    }

}
