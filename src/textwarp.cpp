#include "curvy/textwarp.h"

#include <algorithm>
#include <cmath>

namespace curvy {

    TextWarper::TextWarper(const TextWarp& warp, double x0, double x1, double yTop, double yBottom)
        : kind_(warp.kind), x0_(x0), x1_(x1), yTop_(yTop), yBottom_(yBottom) {
        auto firstOf = [](const BLPath& path, MeasuredPath& out) {
            std::vector<MeasuredPath> all = measurePath(path);
            if (all.empty()) {
                return false;
            }
            out = std::move(all.front());
            return true;
        };
        if (kind_ == TextWarp::Kind::OnPath) {
            valid_ = firstOf(warp.first, first_);
            if (valid_ && warp.flip) {
                first_ = reversedPath(first_);
            }
            start_ = warp.start / 100.0 * first_.total;
        } else if (kind_ == TextWarp::Kind::Between) {
            valid_ = firstOf(warp.first, first_) && firstOf(warp.second, second_);
            if (valid_ && warp.flip) {
                std::swap(first_, second_);   // top and bottom the other way round
            }
        }
    }

    BLPoint TextWarper::map(double x, double y) const {
        if (!valid_) {
            return BLPoint(x, y);
        }
        BLPoint p, t;
        if (kind_ == TextWarp::Kind::OnPath) {
            sampleMeasured(first_, start_ + x, p, t, true);
            return BLPoint(p.x + t.y * -y, p.y - t.x * -y);   // up (-y) is to the left of the way the path runs
        }
        const double across = x1_ - x0_;
        const double u = across > 1e-9 ? std::min(1.0, std::max(0.0, (x - x0_) / across)) : 0.0;
        const double height = yBottom_ - yTop_;
        const double v = height > 1e-9 ? (y - yTop_) / height : 0.0;
        BLPoint top, bottom, direction;
        sampleMeasured(first_, u * first_.total, top, direction);
        sampleMeasured(second_, u * second_.total, bottom, direction);
        return BLPoint(top.x + (bottom.x - top.x) * v, top.y + (bottom.y - top.y) * v);
    }

    BLPath TextWarper::apply(const BLPath& outline) const {
        if (!valid_) {
            return outline;
        }
        const double piece = std::max(0.4, (yBottom_ - yTop_) / 18.0);
        BLPath out;
        for (Polyline& line : flattenPath(outline)) {
            std::vector<BLPoint> pts = line.points;
            if (line.closed) {
                pts.push_back(pts.front());
            }
            for (std::size_t i = 0; i < pts.size(); ++i) {
                if (i == 0) {
                    BLPoint p = map(pts[0].x, pts[0].y);
                    out.move_to(p.x, p.y);
                    continue;
                }
                const double length = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
                const int pieces = std::min(200, std::max(1, int(std::ceil(length / piece))));
                for (int k = 1; k <= pieces; ++k) {
                    const double f = double(k) / pieces;
                    BLPoint p = map(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * f, pts[i - 1].y + (pts[i].y - pts[i - 1].y) * f);
                    out.line_to(p.x, p.y);
                }
            }
            if (line.closed) {
                out.close();
            }
        }
        return out;
    }

}
