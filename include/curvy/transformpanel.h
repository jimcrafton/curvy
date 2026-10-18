#pragma once

#include "curvy/sketchdocument.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/rootview.h>

#include <vector>

namespace curvy {

    // The Transform panel of sketch.newui: X, Y, W, H and the angle of the selection, shown as numbers and editable. A value typed
    // in a field (Enter) moves, resizes or turns the selection as one undo step; with the link button down, changing W or H keeps the
    // proportions. The fields follow the selection and every change of the document, and are disabled with nothing selected.
    //
    // X / Y / W / H describe the bounding box of the selection (its top-left corner and size, in points). The angle is the turn of the
    // selection's frame in degrees (what the rotate handle shows).
    //
    // It is a newui::Controller (the C between the panel's fields and the document) but, like the other controllers here, does not
    // call Controller::setModel(): the document is observed through a plain reference and must outlive it.
    // @reflect ignore=true
    class TransformPanel : public newui::Controller {
    public:
        TransformPanel(newui::RootView& root, SketchDocument& document);
        ~TransformPanel() override = default;
        TransformPanel(const TransformPanel&) = delete;
        TransformPanel& operator=(const TransformPanel&) = delete;

        // Re-reads the selection into the fields.
        void refresh();

    private:
        enum class Field { X, Y, W, H, Angle };
        void apply(Field field);
        newui::TextField* field(Field which) const;

        // Scrubbing the caption of a field (numberscrubber.h): the drag edits the selection live and is one undo step when let go.
        void bindScrub(Field which, const char* labelName, double unit, double minimum, double maximum);
        BLBox boxFor(Field which, double value, const BLBox& base, bool linked) const;
        struct Scrub {
            std::vector<GeometryState> before;
            FrameState frameBefore;
            BLBox box;
            double angle = 0.0;
            BLMatrix2D matrix = BLMatrix2D::make_identity();
            double angleDelta = 0.0;
            bool active = false;
        };
        Scrub scrub_;

        newui::RootView& root_;
        SketchDocument& document_;
        newui::TextField* fields_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        newui::ToolbarButton* link_ = nullptr;
        bool updating_ = false;   // refresh() is writing the fields: their events are not edits
    };

}
