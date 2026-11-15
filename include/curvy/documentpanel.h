#pragma once

#include "curvy/panel.h"
#include "curvy/sketchdocument.h"
#include "curvy/swatchstrip.h"

#include <newui/controls.h>
#include <newui/subview.h>

#include <string>
#include <vector>

namespace curvy {

    // The Document panel of the right dock (docs/mockups/dockPanels.html): a Panel (the chrome) whose body is a TabControl with a Document tab (name,
    // page, colour mode) and a Swatches tab (the palettes strip). Library and Fonts tabs come later. It is only the view: the owner connects the
    // strip to the document (SwatchPaletteController) and keeps that controller no longer than the document.
    // @reflect ignore=true
    class DocumentPanel : public Panel {
    public:
        explicit DocumentPanel(SketchDocument& document);

        // Reads the document again (after New or Open, which change it without telling the panel).
        void refresh();

        newui::TabControl& tabs() { return *tabs_; }
        SwatchStrip& strip() { return *strip_; }
        std::string nameText() const;
        std::string pageText() const;

    private:
        void layoutParts();

        SketchDocument& document_;
        newui::TabControl* tabs_ = nullptr;
        newui::SubView* swatchPage_ = nullptr;
        std::vector<newui::Label*> rows_;   // caption, value, caption, value, ...
        newui::Label* name_ = nullptr;
        newui::Label* page_ = nullptr;
        newui::Label* mode_ = nullptr;
        SwatchStrip* strip_ = nullptr;
    };

}
