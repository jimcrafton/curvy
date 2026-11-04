#pragma once

#include "curvy/liveshape.h"
#include "curvy/sketchdocument.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/rootview.h>

namespace curvy {

    // The Shape box of the property dock: the numbers of the lone selected star, polygon, rounded rectangle or arc (points or sides, inner
    // radius, corner radius, arc start and sweep), the same ones the canvas handles drag. Each entry rewrites the outline as one undo step.
    // Only the rows that fit the shape are shown. A newui::Controller that, like the other panels, observes the document through a plain
    // reference that must outlive it.
    // @reflect ignore=true
    class ShapePanel : public newui::Controller {
    public:
        ShapePanel(newui::RootView& root, SketchDocument& document);
        ~ShapePanel() override = default;
        ShapePanel(const ShapePanel&) = delete;
        ShapePanel& operator=(const ShapePanel&) = delete;

        void refresh();

    private:
        enum class Field { Count, Inner, Radius, Start, Sweep };
        void bind(newui::TextField* field, Field which);
        void apply(Field which);
        bool current(LiveShape& out) const;
        static void show(newui::View* view, bool visible);

        newui::RootView& root_;
        SketchDocument& document_;
        newui::Label* info_ = nullptr;
        newui::Label* countLabel_ = nullptr;
        newui::TextField* count_ = nullptr;
        newui::TextField* inner_ = nullptr;
        newui::TextField* radius_ = nullptr;
        newui::TextField* start_ = nullptr;
        newui::TextField* sweep_ = nullptr;
        bool updating_ = false;   // refresh() is writing the fields: their events are not edits
    };

}
