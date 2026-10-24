#pragma once

#include "curvy/canvasview.h"

#include <newui/controls.h>
#include <newui/geometry.h>
#include <newui/subview.h>

#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // The card a tool's options open in (docs/ui-design.md "Tool options"; layout from docs/mockups/toolOptionsCard.html -> Resources/toolOptionsCard.newui).
    // Options are defaults for the next shape the tool makes (the star's points, the rounded rectangle's corner radius, how much a pencil stroke is
    // simplified); they never change shapes already drawn. The card shows one or two slider rows for the tool, a Reset and a note, and closes on
    // Esc or a press outside it (WorkspaceController). The values live in the CanvasView, so the Up / Down arrows while dragging a shape and the card
    // always agree.
    // @reflect ignore=true
    class ToolOptionsCard {
    public:
        // `parent` is the view the card floats in (the canvas), in whose coordinates `at` is given.
        ToolOptionsCard(newui::SubView& parent, CanvasView& canvas);
        ToolOptionsCard(const ToolOptionsCard&) = delete;
        ToolOptionsCard& operator=(const ToolOptionsCard&) = delete;

        static bool hasOptions(SketchTool tool);
        // The command a ring slot or button uses for this tool ("options.star"), and the tool a command names.
        static std::string commandFor(SketchTool tool);
        static bool toolFromCommand(const std::string& command, SketchTool& tool);

        // Opens the card for `tool` with its left edge at `at` (clamped inside the parent). Does nothing for a tool without options.
        void show(SketchTool tool, const newui::Point& at);
        void close();
        bool isOpen() const { return open_; }
        SketchTool tool() const { return tool_; }
        bool contains(const newui::Point& pt) const;
        // What row `row` (0 or 1) holds now, for the checks.
        double value(std::size_t row) const;

        // Colours from Theme::current().
        void restyle();

        // The card's x was pressed: the host puts focus back on the canvas so Space still opens the ring.
        std::function<void()> onDismissed;

    private:
        struct Row {
            const char* label;
            double min, max;
            double reset;
            std::function<double()> get;
            std::function<void(double)> set;
        };
        std::vector<Row> rowsFor(SketchTool tool) const;
        void refresh();
        void apply(std::size_t row, double value);

        newui::SubView& parent_;
        CanvasView& canvas_;
        newui::SubView* card_ = nullptr;
        newui::Label* title_ = nullptr;
        newui::Slider* sliders_[2] = {nullptr, nullptr};
        newui::TextField* fields_[2] = {nullptr, nullptr};
        newui::SubView* rows_[2] = {nullptr, nullptr};
        newui::Label* labels_[2] = {nullptr, nullptr};
        newui::Button* reset_ = nullptr;
        newui::Button* close_ = nullptr;
        SketchTool tool_ = SketchTool::Select;
        bool open_ = false;
        std::vector<Row> active_;
        bool updating_ = false;
    };

}
