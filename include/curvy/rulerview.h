#pragma once

#include "curvy/rulers.h"

#include <newui/subview.h>

#include <optional>

namespace curvy {

    class CanvasView;

    // One ruler strip, a child of the canvas (CanvasView makes and places both): the top one reads x, the left one y. It reads the canvas's zoom, pan
    // and the document's unit each time it paints, so it follows them with no state of its own, and it marks only itself dirty when the pointer marker
    // moves. Presses and moves on it are handed to the canvas as if they had landed on the canvas at the same spot, so guides can be pulled out of it
    // and a drag that started here keeps going (the view that took the press is captured).
    // @reflect ignore=true
    class RulerView : public newui::SubView {
    public:
        RulerView(CanvasView& canvas, bool horizontal);

        bool horizontal() const { return horizontal_; }
        // The pointer's place along this ruler's axis, in the canvas's own pixels; none hides the marker.
        void setMarker(std::optional<float> along);

        void paint(BLContext& ctx) override;

    private:
        newui::Point toCanvas(const newui::Point& local) const;

        CanvasView& canvas_;
        bool horizontal_;
        std::optional<float> marker_;
    };

}
