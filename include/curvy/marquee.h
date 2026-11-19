#pragma once

#include "curvy/selectionmodel.h"
#include "curvy/settings.h"
#include "curvy/shapepaths.h"

#include <newui/geometry.h>

#include <vector>

namespace curvy {

    // One drag of the Select tool's marquee, in the canvas's own pixels. It only tracks the gesture; the controller turns outline() into a
    // region of the document, asks shapesInRegion what it takes and combines the result with the selection when the gesture ends.
    // Rectangle and Ellipse span the corner where the press was to the pointer; Freehand collects the pointer's path; Polygon is one
    // press per corner (the gesture stays open between presses) and closes with the Enter key or a press near the first corner.
    // @reflect ignore=true
    class Marquee {
    public:
        using Shape = Settings::SelectRegion;

        // A press this far (pixels) from where it began is a drag, not a click.
        static constexpr float dragThreshold = 3.0f;
        // A polygon press this close to the first corner closes it.
        static constexpr float closeDistance = 9.0f;

        void begin(Shape shape, const newui::Point& at, RegionMode mode, SelectionModel::Combine how, bool ringOnClick);
        void cancel() { active_ = false; points_.clear(); }

        bool active() const { return active_; }
        bool polygonOpen() const { return active_ && shape_ == Shape::Polygon; }
        bool moved() const { return moved_; }
        Shape shape() const { return shape_; }
        RegionMode mode() const { return mode_; }
        SelectionModel::Combine how() const { return how_; }
        // A press that began with the secondary button and never became a drag is a click that opens the ring.
        bool ringOnClick() const { return ringOnClick_; }

        void moveTo(const newui::Point& at);
        // A polygon press: adds a corner. True when it closed the polygon (at least three corners and the press is near the first).
        bool addCorner(const newui::Point& at);
        // A polygon can be closed by the Enter key once it has three corners.
        bool canClose() const { return polygonOpen() && points_.size() >= 3; }

        // The region as it stands, in canvas pixels: a rectangle or ellipse from the press to the pointer, the freehand path, the polygon's
        // corners plus the pointer (the edge being drawn). Fewer than three points when there is no region yet.
        std::vector<newui::Point> outline() const;

    private:
        bool active_ = false;
        bool moved_ = false;
        bool ringOnClick_ = false;
        Shape shape_ = Shape::Rectangle;
        RegionMode mode_ = RegionMode::Inside;
        SelectionModel::Combine how_ = SelectionModel::Combine::Replace;
        newui::Point start_, current_;
        std::vector<newui::Point> points_;
    };

}
