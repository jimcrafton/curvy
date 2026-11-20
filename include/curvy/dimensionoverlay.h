#pragma once

#include "curvy/dimensions.h"
#include "curvy/numberscrubber.h"
#include "curvy/tool.h"

#include <newui/controls.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace curvy {

    // The selection's measurements on the canvas: the bracket of its box and one size label per measurement (width at the bottom, height at the
    // right, a circle's radius at its centre, a curve's length at its corner), in the document's units. Dragging a label sideways changes the
    // measurement live and ends as one undo step (the Transform panel's scrub: Shift and Ctrl+Shift give finer steps); a double-click on one
    // opens a field over it where a value is typed ("12", "3mm", "1 in"), Enter applies it and Esc leaves it. Owned by the Select tool, which
    // offers it presses and moves first. Labels are hidden while the box is too small on screen to hold them, and when Settings::showDimensions
    // is off. The model is DimensionModel, so a script reaches the same numbers.
    // @reflect ignore=true
    class DimensionOverlay {
    public:
        explicit DimensionOverlay(ToolContext context);
        DimensionOverlay(const DimensionOverlay&) = delete;
        DimensionOverlay& operator=(const DimensionOverlay&) = delete;

        struct Label {
            DimensionItem item;
            newui::Rect rect;   // in the canvas's own pixels
            std::string text;
        };

        // The labels as they are drawn now (empty when hidden), measured from the selection and the artwork on each call.
        const std::vector<Label>& labels();
        // The label under a canvas point; an editable one only when `editableOnly`.
        std::optional<std::size_t> labelAt(const newui::Point& view, bool editableOnly = false);

        // A press on an editable label starts a scrub, a press on a pivot mark starts moving that pivot; moves and the release follow it. False
        // when the press hit neither. Holding Alt during a scrub holds the pivot of the transform being edited still instead of the box's corner.
        bool press(const newui::Point& view);
        void move(const newui::Point& view, std::uint32_t keys);
        void release();
        // True while a scrub or a pivot drag is in progress.
        bool scrubbing() const { return scrub_.active() || pivotDrag_; }

        // The pivot marks as they are drawn now (empty when hidden). A shape's is a ring with a cross, a layer's or group's a filled diamond.
        const std::vector<DimensionModel::PivotMark>& pivotMarks();
        std::optional<std::size_t> pivotAt(const newui::Point& view);

        // A double-click on an editable label opens the field over it.
        bool beginEdit(const newui::Point& view);
        bool editing() const { return editing_; }
        // Applies typed text to the label being edited (what Enter does); false when it was refused (the reason goes to the status line).
        bool commitEdit(const std::string& text);
        void cancelEdit();
        // The field's text now, for tests.
        std::string editText() const;

        // Drops a scrub or an edit in progress.
        void cancel();

        void paint(BLContext& ctx);

    private:
        bool shown() const;
        void rebuild();
        std::string textFor(const DimensionItem& item) const;
        void closeField();
        void paintPivots(BLContext& ctx);

        ToolContext context_;
        DimensionModel model_;
        std::vector<Label> labels_;
        std::vector<DimensionModel::PivotMark> marks_;
        bool pivotDrag_ = false;
        bool aboutPivot_ = false;
        NumberScrubber scrub_;
        std::optional<DimensionItem::Kind> scrubKind_;
        bool abandon_ = false;
        newui::TextField* field_ = nullptr;   // a child of the canvas, made on first use and kept (hidden) for as long as the canvas lives
        std::optional<DimensionItem::Kind> editKind_;
        bool editing_ = false;
        std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    };

}
