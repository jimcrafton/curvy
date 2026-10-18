#pragma once

#include "curvy/sketchdocument.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/rootview.h>

#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // The Symbols and Patterns panels of sketch.newui. Each has a drop-down of its library (choosing an entry makes it the active one the
    // Object menu acts on), a name field for the active entry and buttons: New (from the selection), Place / Redefine / Break Link /
    // Delete for symbols, Fill / Stroke / Delete for patterns. The pattern panel also edits the scale and angle of the selection's
    // pattern. Every edit is an undo step on the document.
    //
    // A newui::Controller that, like the other controllers here, does not call Controller::setModel(): the document is observed
    // through a plain reference and must outlive it.
    // @reflect ignore=true
    class LibraryPanel : public newui::Controller {
    public:
        LibraryPanel(newui::RootView& root, SketchDocument& document);
        ~LibraryPanel() override = default;
        LibraryPanel(const LibraryPanel&) = delete;
        LibraryPanel& operator=(const LibraryPanel&) = delete;

        // Re-reads both libraries and the selection into the controls.
        void refresh();

    private:
        void onButton(const char* name, std::function<void()> action);
        void fillCombo(newui::DropDownList* combo, const std::vector<std::shared_ptr<const SymbolDef>>& library, std::vector<std::string>& shown, std::size_t active);
        void applyPatternTransform();

        newui::RootView& root_;
        SketchDocument& document_;
        std::vector<std::string> symbolNames_, patternNames_;   // what the drop-downs currently list
        newui::DropDownList* symbolCombo_ = nullptr;
        newui::DropDownList* patternCombo_ = nullptr;
        newui::TextField* symbolName_ = nullptr;
        newui::TextField* patternName_ = nullptr;
        newui::TextField* scale_ = nullptr;
        newui::TextField* angle_ = nullptr;
        bool updating_ = false;   // refresh() is writing the controls: their events are not edits
    };

}
