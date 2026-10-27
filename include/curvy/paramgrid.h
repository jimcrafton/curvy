#pragma once

#include "curvy/expressionfield.h"
#include "curvy/numberscrubber.h"

#include <newui/controls.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace curvy {

    // The rows of the graph dock's parameter box. Every row is drawn by the grid itself (caption, value, key diamond, colour chip, slider);
    // only the row the user clicks gets a real control, one ExpressionField that is moved over the value and goes away when the entry ends.
    // That keeps the box at a handful of views however many parameters a node has. The pattern is the Visual Designer's PropertiesGrid
    // (code/cpp_codetools/extension/NativeEditControls): paint the value, create the editor on demand.
    //
    //   caption  drag sideways to scrub the number (rows with `scrub`)
    //   value    click to type: a number, or an expression (see ExpressionField)
    //   diamond  click to key the number at the current time, or take the key there away
    //   chip     click to open the colour card
    //   slider   drag, applied when the button is let go
    //
    // The grid only turns clicks into calls; what they change is the owner's business (Callbacks).
    // @reflect ignore=true
    class ParamGrid : public newui::SubView {
    public:
        // Choice: one of `options`, shown as text with a chevron; a click steps to the next, Shift+click to the previous (commit gets the name).
        enum class Kind { Text, Colour, Gradient, Slider, Choice };

        struct Row {
            bool shown = false;
            Kind kind = Kind::Text;
            std::string caption;
            std::string text;                   // the value of a Text row, the hex of a Colour row
            bool editable = false;              // a click on the value opens the editor
            bool expression = false;            // text is "=expression"
            ExpressionField::KeyState key = ExpressionField::KeyState::None;
            bool scrub = false;                 // dragging the caption changes the number
            bool keyable = false;               // the diamond can be clicked to key the number
            std::uint32_t chipColor = 0;        // Colour: rgb
            double chipAlpha = 1.0;
            std::vector<std::pair<double, std::uint32_t>> stops;   // Gradient
            bool radial = false;
            double percent = 0.0;               // Slider: 0..100
            std::vector<std::string> options;   // Choice
        };

        struct Palette {
            std::uint32_t caption = 0x999999, text = 0xFFFFFF, accent = 0xFFB454, field = 0x202020, border = 0x444444;
        };

        ParamGrid();

        // The grid only turns clicks into these events; what they change is the owner's business. `row` is the index of the row.
        newui::Delegate<ParamGrid, std::size_t, const std::string&> onCommit;   // the editor ended with this text (or a Choice row stepped to this name)
        newui::Delegate<ParamGrid, std::size_t, double> onSliderReleased;       // percent
        newui::Delegate<ParamGrid> onEditEnded;                                 // the editor had the focus and is gone: give it back
        newui::Delegate<ParamGrid, std::size_t> onChipClicked;
        newui::Delegate<ParamGrid, std::size_t> onKeyToggled;
        // Caption scrubbing of a `scrub` row. onScrubBegin sets `value` and returns Handled to start the drag (Ignored: nothing to scrub);
        // onScrubUnit may set the plain step of the row, default 1.
        newui::Delegate<ParamGrid, std::size_t, double&> onScrubBegin;
        newui::Delegate<ParamGrid, std::size_t, double> onScrubChange;
        newui::Delegate<ParamGrid, std::size_t, bool> onScrubEnd;               // changed
        newui::Delegate<ParamGrid, std::size_t, double&> onScrubUnit;

        void setPalette(const Palette& palette);

        void resize(std::size_t count);
        std::size_t rowCount() const { return rows_.size(); }
        const Row& row(std::size_t index) const { return rows_[index]; }
        // Edit a row in place, then call changed().
        Row& edit(std::size_t index) { return rows_[index]; }
        void changed();
        void clearRows();

        // Space left open under row `index` (the path buttons live there); 0 for none.
        void setGapAfter(std::size_t index, float height);
        float rowTop(std::size_t index) const { return index < tops_.size() ? tops_[index] : 0.0f; }
        // The height of what is shown.
        float contentHeight() const { return contentHeight_; }

        // Opens the editor on row `index`; false when the row cannot be typed into.
        bool beginEdit(std::size_t index);
        void endEdit(bool commit);
        static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
        // The row being typed into, or kNone.
        std::size_t editingRow() const { return editing_; }
        ExpressionField& editor() { return *editor_; }

        static constexpr float kRowHeight = 24.0f, kCaptionWidth = 74.0f, kMargin = 8.0f, kTop = 4.0f, kControlHeight = 20.0f;

    private:
        std::size_t rowAt(float y) const;
        newui::Rect valueRect(std::size_t index) const;
        newui::Rect diamondRect(std::size_t index) const;
        newui::Rect sliderTrack(std::size_t index) const;
        double percentAt(std::size_t index, float x) const;
        void layoutRows();
        void paint(BLContext& ctx) override;

        std::vector<Row> rows_;
        std::vector<float> tops_;
        float contentHeight_ = 0.0f;
        std::size_t gapRow_ = kNone;
        float gap_ = 0.0f;
        Palette palette_;
        ExpressionField* editor_ = nullptr;
        std::size_t editing_ = kNone;
        std::size_t hover_ = kNone;
        std::size_t scrubbing_ = kNone;
        std::size_t sliding_ = kNone;
        NumberScrubber scrubber_;
    };

}
