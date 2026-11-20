#pragma once

#include "curvy/snapper.h"
#include "curvy/tool.h"
#include "curvy/tracelayer.h"

#include <optional>
#include <string>

namespace curvy {

    // Draws a rectangle on the trace layer: press at one corner, drag to the opposite one, release. Shift makes it a square, Alt grows it from
    // the press point as its centre, Escape drops it. Both corners snap to the trace layer's shapes and the ruler guides (see Snapper; Ctrl
    // turns it off) and a mark shows where the pointer landed. The shape goes straight into SketchDocument's trace layer (one undo step) and is
    // painted with plain Blend2D; nothing here touches the node graph.
    // @reflect ignore=true
    class TraceRectTool : public Tool {
    public:
        explicit TraceRectTool(ToolContext context) : context_(std::move(context)), snapper_(context_.document) {}

        std::string id() const override { return "traceRect"; }
        std::string title() const override { return "Trace rectangle"; }

        void cancel() override;
        bool busy() const override { return start_.has_value(); }
        bool inputDown(const ToolEvent& event) override;
        void inputMove(const ToolEvent& event) override;
        void inputUp(const ToolEvent& event) override;
        bool keyDown(const ToolKeyEvent& event) override;
        void paintOverlay(BLContext& ctx) const override;

        // The rect between the press point and a document point, with the Shift and Alt rules applied.
        TraceItem itemTo(const BLPoint& to, std::uint32_t keys) const;

    private:
        // The pointer's document point, pulled onto what is near it unless Ctrl is held; the result is kept to draw its mark.
        BLPoint snapped(const ToolEvent& event);

        ToolContext context_;
        Snapper snapper_;
        SnapResult lastSnap_;
        std::optional<BLPoint> start_;
        std::optional<TraceItem> preview_;
    };

}
