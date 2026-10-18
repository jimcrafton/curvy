#include "curvy/pathflatten.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace curvy {

    std::vector<Polyline> flattenPath(const BLPath& path) {
        std::vector<Polyline> result;
        const uint8_t* cmd = path.command_data();
        const BLPoint* v = path.vertex_data();
        std::size_t n = path.size();
        auto curveSteps = [](double length) { return std::min(96, std::max(8, int(length / 3.0))); };
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
                        int steps = curveSteps(std::hypot(c.x - p0.x, c.y - p0.y) + std::hypot(p1.x - c.x, p1.y - c.y));
                        for (int s = 1; s <= steps; ++s) {
                            double t = double(s) / steps, u = 1.0 - t;
                            result.back().points.push_back(BLPoint(u * u * p0.x + 2 * u * t * c.x + t * t * p1.x,
                                u * u * p0.y + 2 * u * t * c.y + t * t * p1.y));
                        }
                    }
                    i += 1;
                    break;
                case BL_PATH_CMD_CUBIC:
                    if (i + 2 < n && !result.empty()) {
                        BLPoint p0 = result.back().points.back(), c1 = v[i], c2 = v[i + 1], p1 = v[i + 2];
                        int steps = curveSteps(std::hypot(c1.x - p0.x, c1.y - p0.y) + std::hypot(c2.x - c1.x, c2.y - c1.y) +
                            std::hypot(p1.x - c2.x, p1.y - c2.y));
                        for (int s = 1; s <= steps; ++s) {
                            double t = double(s) / steps, u = 1.0 - t;
                            result.back().points.push_back(BLPoint(
                                u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p1.x,
                                u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p1.y));
                        }
                    }
                    i += 2;
                    break;
                case BL_PATH_CMD_CLOSE:
                    if (!result.empty()) {
                        result.back().closed = true;
                        result.emplace_back();  // a path may continue from the start point; MOVE normally follows
                        result.back().points.push_back(result[result.size() - 2].points.front());
                    }
                    break;
                default: break;
            }
        }
        result.erase(std::remove_if(result.begin(), result.end(), [](const Polyline& p) { return p.points.size() < 2; }), result.end());
        return result;
    }

    std::vector<MeasuredPath> measurePath(const BLPath& path) {
        std::vector<MeasuredPath> result;
        for (Polyline& line : flattenPath(path)) {
            MeasuredPath m;
            for (const BLPoint& p : line.points) {
                if (m.points.empty() || std::hypot(p.x - m.points.back().x, p.y - m.points.back().y) > 1e-9) {
                    m.points.push_back(p);
                }
            }
            if (line.closed && m.points.size() > 1) {
                m.points.push_back(m.points.front());
            }
            if (m.points.size() < 2) {
                continue;
            }
            m.at.assign(m.points.size(), 0.0);
            for (std::size_t i = 1; i < m.points.size(); ++i) {
                m.at[i] = m.at[i - 1] + std::hypot(m.points[i].x - m.points[i - 1].x, m.points[i].y - m.points[i - 1].y);
            }
            m.total = m.at.back();
            if (m.total > 1e-9) {
                result.push_back(std::move(m));
            }
        }
        return result;
    }

    MeasuredPath reversedPath(const MeasuredPath& path) {
        MeasuredPath out;
        out.points.assign(path.points.rbegin(), path.points.rend());
        out.at.resize(path.at.size());
        for (std::size_t i = 0; i < path.at.size(); ++i) {
            out.at[i] = path.total - path.at[path.at.size() - 1 - i];
        }
        out.total = path.total;
        return out;
    }

    void sampleMeasured(const MeasuredPath& m, double s, BLPoint& point, BLPoint& dir, bool extend) {
        const double wanted = s;
        s = std::min(std::max(s, 0.0), m.total);
        std::size_t i = std::size_t(std::upper_bound(m.at.begin(), m.at.end(), s) - m.at.begin());
        i = std::min(std::max<std::size_t>(i, 1), m.points.size() - 1);
        const BLPoint& a = m.points[i - 1];
        const BLPoint& b = m.points[i];
        const double length = m.at[i] - m.at[i - 1];
        const double u = length > 0.0 ? (s - m.at[i - 1]) / length : 0.0;
        point = BLPoint(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
        dir = length > 0.0 ? BLPoint((b.x - a.x) / length, (b.y - a.y) / length) : BLPoint(1.0, 0.0);
        if (extend && wanted != s) {
            point = BLPoint(point.x + dir.x * (wanted - s), point.y + dir.y * (wanted - s));
        }
    }

}
