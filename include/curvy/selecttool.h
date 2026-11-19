#pragma once

#include "curvy/marquee.h"
#include "curvy/tool.h"

#include <string>
#include <vector>

namespace curvy {

    // The default tool: a click selects the shape under the pointer (the node that made it; a click on what is selected goes up one level),
    // and a drag on empty canvas is a marquee. Settings::selectRegion, selectTwoButton and selectContainment are its options. Primary button
    // = takes what the region fully contains, secondary = what it touches (selectTwoButton; off: both use selectContainment). Shift/Ctrl add,
    // Alt subtracts. The polygon takes one press per corner and closes with Enter or a press on its first corner; Escape cancels a region.
    // Also here, because they are selection commands of this tool: Ctrl+A / Tab cycling (selectAll, cycle), M cycles the region, Escape clears.
    // @reflect ignore=true
    class SelectTool : public Tool {
    public:
        explicit SelectTool(ToolContext context) : context_(std::move(context)) {}

        std::string id() const override { return "select"; }
        std::string title() const override { return "Select"; }

        void cancel() override;
        bool busy() const override { return marquee_.active(); }
        bool inputDown(const ToolEvent& event) override;
        void inputMove(const ToolEvent& event) override;
        void inputUp(const ToolEvent& event) override;
        bool keyDown(const ToolKeyEvent& event) override;
        void paintOverlay(BLContext& ctx) const override;
        std::vector<ToolOption> options() override;
        std::string optionsNote() const override { return "Applies to the next selection"; }

        const Marquee& marquee() const { return marquee_; }
        // The keys of the shapes the marquee would take now (what the canvas outlines while the region is being drawn).
        const std::vector<std::string>& marqueeHits() const { return marqueeHits_; }
        // Selects the shape (the node that made it) under a canvas point, or clears the selection on empty canvas; `additive` adds instead.
        // True when the press did something.
        bool selectAt(const newui::Point& pt, bool additive);
        // Cycles the region shape (Rectangle, Ellipse, Freehand, Polygon) and says which.
        void cycleRegion();
        // Every shape of the document (its nodes) replaces the selection.
        void selectAll();
        // The next / previous shape in drawing order replaces the selection (wrapping round). False when no shape is selected.
        bool cycle(int step);

    private:
        bool marqueeMayStart(const newui::Point& pt) const;
        void beginMarquee(const newui::Point& pt, bool secondary, std::uint32_t keys);
        void updateMarqueeHits();
        void finishMarquee();

        ToolContext context_;
        Marquee marquee_;
        std::vector<std::string> marqueeHits_;
    };

}
