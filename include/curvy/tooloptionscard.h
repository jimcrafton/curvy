#pragma once

#include "curvy/tool.h"

#include <newui/controls.h>
#include <newui/geometry.h>
#include <newui/segmentedcontrol.h>
#include <newui/subview.h>

#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // The card a tool's options open in (docs/ui-design.md "Tool options"). The frame (head with the tool's name and a close button, a body,
    // a foot with Reset and the tool's note) comes from Resources/toolOptionsCard.newui; the rows are made from Tool::options(): a choice is a
    // SegmentedControl under its label, a toggle a check box with its text, a number a slider and a field. A row whose `enabled` says no is
    // greyed. Options are what the tool will do next; the card never changes anything already made. Esc or a press outside closes it
    // (WorkspaceController).
    // @reflect ignore=true
    class ToolOptionsCard {
    public:
        // `parent` is the view the card floats in (the canvas), in whose coordinates `at` is given.
        explicit ToolOptionsCard(newui::SubView& parent);
        ToolOptionsCard(const ToolOptionsCard&) = delete;
        ToolOptionsCard& operator=(const ToolOptionsCard&) = delete;

        // Opens the card for `tool` beside `anchor` (the option box that was tapped, or the pointer): to its right and just above it, to its left
        // when there is no room, always inside the parent and never over the anchor when the parent is wide enough. False, and nothing
        // opens, when the tool has no options.
        bool show(Tool& tool, const newui::Point& anchor);
        newui::Rect bounds() const { return card_ != nullptr ? card_->bounds() : newui::Rect(); }
        // The Reset / note strip, for tests.
        newui::View* footView() const { return foot_; }
        void close();
        bool isOpen() const { return open_; }
        bool contains(const newui::Point& pt) const;
        const std::string& toolId() const { return toolId_; }
        // What row `row` holds now, for the checks.
        double value(std::size_t row) const;
        std::size_t rowCount() const { return rows_.size(); }
        // Sets row `row` as the user would (a click on the segment, the check box, a typed number); the tool's setter runs.
        void apply(std::size_t row, double value);
        // The control of a row, for automation and tests.
        newui::View* controlOf(std::size_t row) const;

        // Colours from Theme::current().
        void restyle();

        // The card's x was pressed: the host puts focus back on the canvas so Space still opens the ring.
        std::function<void()> onDismissed;

    private:
        struct Row {
            ToolOption option;
            newui::Label* label = nullptr;
            newui::SegmentedControl* segments = nullptr;
            newui::Toggle* toggle = nullptr;
            newui::Slider* slider = nullptr;
            newui::TextField* field = nullptr;
        };

        void clearRows();
        void refresh();

        newui::SubView& parent_;
        newui::SubView* card_ = nullptr;
        newui::SubView* inner_ = nullptr;
        newui::SubView* body_ = nullptr;
        newui::SubView* foot_ = nullptr;
        newui::Label* title_ = nullptr;
        newui::Label* note_ = nullptr;
        newui::Button* reset_ = nullptr;
        newui::ToolbarButton* close_ = nullptr;
        std::vector<Row> rows_;
        std::string toolId_;
        bool open_ = false;
        bool updating_ = false;
    };

}
