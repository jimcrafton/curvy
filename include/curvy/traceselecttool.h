#pragma once

#include "curvy/snapper.h"
#include "curvy/tool.h"
#include "curvy/tracelayer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // Selects, moves and deletes the shapes of the trace layer. A press on a shape's outline selects it (Shift adds or removes it) and a drag from
    // there moves everything selected; a press on empty canvas clears the selection and a drag from it is a marquee that takes the shapes it fully
    // contains. A move snaps (see Snapper; Ctrl turns it off). Delete or Backspace removes the selection, Escape drops a drag or, with none going
    // on, clears the selection. A move or a delete is one undo step and goes straight to SketchDocument's trace layer; the node graph and the canvas selection are never touched.
    // @reflect ignore=true
    class TraceSelectTool : public Tool {
    public:
        explicit TraceSelectTool(ToolContext context) : context_(std::move(context)), snapper_(context_.document) {}

        std::string id() const override { return "traceSelect"; }
        std::string title() const override { return "Trace select"; }

        void activate() override;
        void cancel() override;
        bool busy() const override { return mode_ != Mode::Idle; }
        bool inputDown(const ToolEvent& event) override;
        void inputMove(const ToolEvent& event) override;
        void inputUp(const ToolEvent& event) override;
        bool keyDown(const ToolKeyEvent& event) override;
        void paintOverlay(BLContext& ctx) const override;

        // The selected shapes (indices into the trace layer), in ascending order. Indices that no longer exist are left out.
        std::vector<std::size_t> selection() const;
        // Deletes the selected shapes; false when nothing was selected or the layer refused (locked).
        bool deleteSelection();

    private:
        enum class Mode { Idle, Pressed, Moving, Marquee };

        std::size_t hitAt(const BLPoint& document) const;
        bool selected(std::size_t index) const;
        void dropStale();
        // The marquee's rectangle in document points, between the press and the pointer.
        BLBox marqueeBox() const;
        void finishMarquee();

        // How far the selection has been dragged: the pointer's travel, pulled so that one of the selection's own snap points lands on a
        // neighbouring shape or guide (the selection itself does not attract), unless Ctrl is held. Keeps the mark in lastSnap_.
        BLPoint dragDelta(const ToolEvent& event);

        ToolContext context_;
        Snapper snapper_;
        SnapResult lastSnap_;
        BLPoint delta_;
        std::vector<std::size_t> selected_;
        Mode mode_ = Mode::Idle;
        BLPoint pressAt_;
        BLPoint pointer_;
    };

}
