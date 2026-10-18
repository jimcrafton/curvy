#pragma once

#include "curvy/sketchdocument.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/rootview.h>

#include <string>
#include <vector>

namespace curvy {

    // The Artboards panel of sketch.newui: a drop-down listing the artboards (choosing one makes it the active one), buttons to add one
    // beside the others and delete the active one, and the name, position and size of the active artboard as fields. Every edit is one
    // undo step on the document (SketchDocument::setArtboard and friends).
    //
    // A newui::Controller that, like the other controllers here, does not call Controller::setModel(): the document is observed
    // through a plain reference and must outlive it.
    // @reflect ignore=true
    class ArtboardPanel : public newui::Controller {
    public:
        ArtboardPanel(newui::RootView& root, SketchDocument& document);
        ~ArtboardPanel() override = default;
        ArtboardPanel(const ArtboardPanel&) = delete;
        ArtboardPanel& operator=(const ArtboardPanel&) = delete;

        // Re-reads the artboards into the controls.
        void refresh();

    private:
        void applyField(newui::TextField* field, double Artboard::*member);
        void applyName();

        newui::RootView& root_;
        SketchDocument& document_;
        std::vector<std::string> names_;   // what the drop-down currently lists
        newui::DropDownList* combo_ = nullptr;
        newui::TextField* name_ = nullptr;
        newui::TextField* x_ = nullptr;
        newui::TextField* y_ = nullptr;
        newui::TextField* width_ = nullptr;
        newui::TextField* height_ = nullptr;
        newui::ToolbarButton* add_ = nullptr;
        newui::ToolbarButton* delete_ = nullptr;
        bool updating_ = false;   // refresh() is writing the controls: their events are not edits
    };

}
