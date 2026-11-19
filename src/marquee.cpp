#include "curvy/marquee.h"

#include <algorithm>
#include <cmath>

namespace curvy {

    void Marquee::begin(Shape shape, const newui::Point& at, RegionMode mode, SelectionModel::Combine how, bool ringOnClick) {
        active_ = true;
        moved_ = false;
        shape_ = shape;
        mode_ = mode;
        how_ = how;
        ringOnClick_ = ringOnClick;
        start_ = at;
        current_ = at;
        points_.clear();
        points_.push_back(at);
    }

    void Marquee::moveTo(const newui::Point& at) {
        if (!active_) {
            return;
        }
        current_ = at;
        if (!moved_ && std::hypot(at.x - start_.x, at.y - start_.y) > dragThreshold) {
            moved_ = true;
        }
        if (shape_ == Shape::Freehand && moved_) {
            points_.push_back(at);
        }
    }

    bool Marquee::addCorner(const newui::Point& at) {
        if (!polygonOpen()) {
            return false;
        }
        current_ = at;
        moved_ = true;
        const newui::Point& first = points_.front();
        if (points_.size() >= 3 && std::hypot(at.x - first.x, at.y - first.y) <= closeDistance) {
            return true;
        }
        points_.push_back(at);
        return false;
    }

    std::vector<newui::Point> Marquee::outline() const {
        if (!active_) {
            return {};
        }
        switch (shape_) {
            case Shape::Rectangle:
                return {start_, newui::Point(current_.x, start_.y), current_, newui::Point(start_.x, current_.y)};
            case Shape::Ellipse: {
                constexpr int steps = 64;
                const double cx = (double(start_.x) + double(current_.x)) * 0.5, cy = (double(start_.y) + double(current_.y)) * 0.5;
                const double rx = std::abs(double(current_.x) - double(start_.x)) * 0.5, ry = std::abs(double(current_.y) - double(start_.y)) * 0.5;
                std::vector<newui::Point> out;
                out.reserve(steps);
                for (int i = 0; i < steps; ++i) {
                    const double t = 6.283185307179586 * double(i) / double(steps);
                    out.push_back(newui::Point(float(cx + rx * std::cos(t)), float(cy + ry * std::sin(t))));
                }
                return out;
            }
            case Shape::Freehand:
                return points_;
            case Shape::Polygon: {
                std::vector<newui::Point> out = points_;
                out.push_back(current_);
                return out;
            }
        }
        return {};
    }

}
