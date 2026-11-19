#pragma once

#include "curvy/canvasview.h"
#include "curvy/sketchdocument.h"

#include <functional>
#include <optional>

namespace curvy {

    // The drag gesture of the guides, above every tool: a press in a ruler pulls a new guide out of it, a press on an unlocked guide picks it up, and a press
    // in the corner where the rulers meet drags the ruler origin out (crosshair preview, set on release; a corner press that hardly moves is a click and
    // calls onCornerClick instead, which the owner uses for the unit menu). While
    // the pointer moves only the canvas's preview changes; the document gets one edit (and so one undo step) when the button is released:
    // add or move the guide, or delete a picked-up one that was dropped back on a ruler.
    // @reflect ignore=true
    class GuideDrag {
    public:
        GuideDrag(SketchDocument& document, CanvasView& canvas);

        // True when this press starts a drag (the caller then does not give it to the tool).
        bool begin(const newui::Point& viewPoint);
        // Follows the pointer; false when no drag is running.
        bool move(const newui::Point& viewPoint);
        // Ends the drag and makes the edit; false when no drag was running.
        bool end(const newui::Point& viewPoint);
        // Drops the drag without an edit.
        void cancel();

        bool active() const { return active_; }
        // Called when a press in the rulers' corner was released without a drag (a click).
        std::function<void()> onCornerClick;
        // The guide as it would be dropped now (only while active).
        const Guide& guide() const { return guide_; }

    private:
        bool overOwnRuler(const newui::Point& viewPoint) const;
        void follow(const newui::Point& viewPoint);

        SketchDocument& document_;
        CanvasView& canvas_;
        bool active_ = false;
        bool origin_ = false;                 // dragging the ruler origin out of the corner
        newui::Point pressedAt_;
        std::optional<std::size_t> index_;   // the guide being moved; empty for one pulled out of a ruler
        Guide guide_;
    };

}
