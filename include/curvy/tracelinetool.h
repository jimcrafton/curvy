#pragma once

#include "curvy/snapper.h"
#include "curvy/tool.h"
#include "curvy/tracelayer.h"

#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // Draws a straight line on the trace layer: press at one end, drag to the other, release. Shift locks the angle to steps of 45 degrees,
    // Escape drops it. A line started on an edge of another shape locks square to that edge when the drag comes within 5 degrees of it (a
    // green right-angle mark shows it). Both ends snap to the trace layer's shapes (corners, midpoints, centres, edge crossings, edges) and the ruler guides (see
    // Snapper; Ctrl turns it off) and a mark shows where the pointer landed. The line goes straight into SketchDocument's trace layer (one undo
    // step); nothing here touches the node graph.
    // @reflect ignore=true
    class TraceLineTool : public Tool {
    public:
        explicit TraceLineTool(ToolContext context) : context_(std::move(context)), snapper_(context_.document) {}

        std::string id() const override { return "traceLine"; }
        std::string title() const override { return "Trace line"; }

        void cancel() override;
        bool busy() const override { return start_.has_value(); }
        bool inputDown(const ToolEvent& event) override;
        void inputMove(const ToolEvent& event) override;
        void inputUp(const ToolEvent& event) override;
        bool keyDown(const ToolKeyEvent& event) override;
        void paintOverlay(BLContext& ctx) const override;

        // The line from the press point to a document point, with the Shift rule applied (the end is moved onto the nearest 45 degree ray).
        TraceItem itemTo(const BLPoint& to, std::uint32_t keys) const;

        // Within this many degrees of square to a shape's edge the line locks to exactly square.
        static constexpr double kPerpendicularDegrees = 5.0;

    private:
        BLPoint snapped(const ToolEvent& event);
        // Where the line's end goes for this pointer: Shift locks 45 degree steps, otherwise the end snaps, and a drag that is nearly square to
        // an edge the start sits on is projected onto the exact perpendicular (Ctrl turns snapping and this off).
        BLPoint endPoint(const ToolEvent& event);
        // Unit directions of the edges that pass through the start.
        void findReferences();

        ToolContext context_;
        Snapper snapper_;
        SnapResult lastSnap_;
        std::optional<BLPoint> start_;
        std::optional<TraceItem> preview_;
        std::vector<BLPoint> references_;
        std::optional<BLPoint> perpendicularTo_;   // the edge direction the drag is currently square to
    };

}
