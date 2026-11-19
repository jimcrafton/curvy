#pragma once

#include "curvy/panel.h"
#include "curvy/sketchdocument.h"

#include <newui/controls.h>
#include <newui/subview.h>

#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // The Document panel of the right dock (docs/mockups/dockPanels.html): a Panel (the chrome) whose body is a TabControl with a Document tab (name,
    // page in the document's unit, colour mode, and the units, which can be changed here) and a Swatches tab, which is empty until the owner puts a SwatchesTab into swatchPage() (and keeps it no longer than the
    // document). Library and Fonts tabs come later. It is only the view.
    // @reflect ignore=true
    class DocumentPanel : public Panel {
    public:
        explicit DocumentPanel(SketchDocument& document);
        ~DocumentPanel() override;

        // Reads the document again (after New or Open, which change it without telling the panel).
        void refresh();

        newui::TabControl& tabs() { return *tabs_; }
        // The page of the Swatches tab, for the owner's SwatchesTab.
        newui::SubView& swatchPage() { return *swatchPage_; }
        std::string nameText() const;
        std::string pageText() const;

        // Called with the size of the Swatches page whenever the panel is laid out, and when that tab is chosen.
        std::function<void(float width, float height)> layoutSwatches;
        // How tall the Swatches content wants to be (the owner's SwatchesTab knows it). The panel asks for that much height while that tab shows, up to a
        // limit, and the page scrolls beyond it.
        std::function<float()> swatchesContentHeight;
        // Sets the panel's preferred height for the tab that shows (a no-op once the user has resized the panel). Call it when the content's height changed.
        void fitHeight();
        std::function<void()> swatchesShown;
        // Called when the panel is destroyed (closing the window destroys the view tree before its owner).
        std::function<void()> onDestroyed;

    private:
        void layoutParts();

        SketchDocument& document_;
        newui::TabControl* tabs_ = nullptr;
        newui::SubView* swatchPage_ = nullptr;
        std::vector<newui::Label*> rows_;   // caption, value, caption, value, ...
        newui::Label* name_ = nullptr;
        newui::Label* page_ = nullptr;
        newui::Label* mode_ = nullptr;
        newui::DropDownList* lengthUnit_ = nullptr;   // the document's units, edited here
        newui::DropDownList* angleUnit_ = nullptr;
        bool refreshing_ = false;
    };

}
