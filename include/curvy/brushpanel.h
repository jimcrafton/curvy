#pragma once

#include "curvy/sketchdocument.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/rootview.h>

#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // The Brushes panel of sketch.newui: a drop-down of the document brushes (the chosen one is the active brush that the Paintbrush and
    // the Apply button use), its name and numbers (the nib for a calligraphic brush; spacing, scatter, size and turn for a scatter brush),
    // and buttons: new calligraphic / art / scatter brush (the last two from the selection), apply to the selection, back to a plain
    // stroke, add the presets, delete. Choosing an object that has a brush makes that brush the active one. Every edit is an undo step.
    //
    // A newui::Controller that, like the other controllers here, does not call Controller::setModel(): the document is observed through
    // a plain reference and must outlive it.
    // @reflect ignore=true
    class BrushPanel : public newui::Controller {
    public:
        BrushPanel(newui::RootView& root, SketchDocument& document);
        ~BrushPanel() override = default;
        BrushPanel(const BrushPanel&) = delete;
        BrushPanel& operator=(const BrushPanel&) = delete;

        // Re-reads the library and the selection into the controls.
        void refresh();

    private:
        void onButton(const char* name, std::function<void()> action);
        // Reads the fields back into the active brush.
        void applyFields();

        newui::RootView& root_;
        SketchDocument& document_;
        std::vector<std::string> shown_;   // what the drop-down currently lists
        newui::DropDownList* combo_ = nullptr;
        newui::TextField* name_ = nullptr;
        newui::TextField* angle_ = nullptr;
        newui::TextField* roundness_ = nullptr;
        newui::TextField* spacing_ = nullptr;
        newui::TextField* scatter_ = nullptr;
        newui::TextField* sizeVariation_ = nullptr;
        newui::TextField* rotation_ = nullptr;
        newui::ToolbarButton* follow_ = nullptr;
        bool updating_ = false;   // refresh() is writing the controls: their events are not edits
    };

}
