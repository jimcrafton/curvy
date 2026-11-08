#pragma once

#include "curvy/brushlist.h"
#include "curvy/graphbrush.h"
#include "curvy/sketchdocument.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/rootview.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace curvy {

    // The Brushes panel of sketch.newui: a drop-down of the document brushes (the chosen one is the active brush that the Paintbrush and
    // the Apply button use), its name and numbers (the nib for a calligraphic brush; spacing, scatter, size and turn for a scatter brush),
    // for an art brush also how the artwork fits (stretch / guided ends / repeat), the guides, the corner policy, the
    // direction, the flips and the symbol it is linked to, and buttons: new calligraphic / art / scatter brush (the last two from the selection), apply to the selection, back to a plain
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
        // Scrolls the sidebar so that the panel's header is at the top of it (the panel is far down the sidebar). False when it cannot.
        bool reveal();

    private:
        void onButton(const char* name, std::function<void()> action);
        // Reads the fields back into the active brush.
        void applyFields();
        // In the dock only: shows the rows of the active brush's kind and sizes the box to them.
        void fitRows(const BrushDef* def);
        // Graph brush: one label and field per setting of the asset, made the first time that asset is shown (and then kept, hidden when
        // another brush is active) as children of the panel body. Shows the rows of `info` (null: none) with the brush's values.
        void showSettingRows(const BrushDef* def, const GraphBrushInfo* info);
        // Writes the fields into the selected strokes that have this brush (their own settings, one undo step); the brush itself is not changed.
        void applySettingRows(const BrushDef& def, const GraphBrushInfo& info);
        // The stroke options of the first selected object drawn with `def`, or null.
        const StrokeOptions* selectedGraphOptions(const BrushDef& def) const;

        struct SettingRow {
            std::string name;
            double defaultValue = 0.0;
            newui::SubView* row = nullptr;
            newui::TextField* field = nullptr;   // null for the last row of an asset: the Reset button
            newui::Button* button = nullptr;
        };
        std::map<std::string, std::vector<SettingRow>> settingRows_;   // by asset type

        newui::RootView& root_;
        SketchDocument& document_;
        std::vector<std::string> shown_;   // what the drop-down currently lists
        BrushList* list_ = nullptr;
        newui::DropDownList* combo_ = nullptr;
        newui::TextField* name_ = nullptr;
        newui::TextField* angle_ = nullptr;
        newui::TextField* roundness_ = nullptr;
        newui::TextField* spacing_ = nullptr;
        newui::TextField* scatter_ = nullptr;
        newui::TextField* sizeVariation_ = nullptr;
        newui::TextField* rotation_ = nullptr;
        newui::ToolbarButton* follow_ = nullptr;
        // Art brush: how the artwork fits the path, its corners, direction, mirroring and link to a library symbol.
        newui::DropDownList* fit_ = nullptr;
        newui::DropDownList* direction_ = nullptr;
        newui::DropDownList* corner_ = nullptr;
        newui::DropDownList* link_ = nullptr;
        newui::TextField* headGuide_ = nullptr;
        newui::TextField* tailGuide_ = nullptr;
        newui::TextField* cornerRadius_ = nullptr;
        newui::TextField* miterLimit_ = nullptr;
        newui::TextField* breakAngle_ = nullptr;
        newui::ToolbarButton* flipAlong_ = nullptr;
        newui::ToolbarButton* flipAcross_ = nullptr;
        std::vector<std::size_t> linkIds_;   // symbol id of each entry of link_ after "(none)"
        std::vector<std::string> linkShown_;
        bool updating_ = false;   // refresh() is writing the controls: their events are not edits
    };

}
