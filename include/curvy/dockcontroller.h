#pragma once

#include "curvy/settings.h"

#include <newui/frame.h>
#include <newui/splitter.h>
#include <newui/subview.h>

#include <functional>

namespace curvy {

    enum class DockSide { Top, Left, Right, Bottom };

    // The four dock areas around the canvas (Resources/workspace.newui: topSplitter > bottomSplitter > leftSplitter > rightSplitter, each
    // holding its area and the rest). A double-click on a splitter hides the area beside it and a second one brings it back at the size it
    // had; dragging resizes it, never below Settings::minDockSize and never leaving the canvas less than that, and dragging a hidden area's
    // splitter opens it. The sizes and hidden flags live in Settings::docks; `changed` is called after each one so the owner can save.
    // @reflect ignore=true
    class DockController {
    public:
        DockController(newui::Frame& frame, Settings& settings, std::function<void()> changed);
        DockController(const DockController&) = delete;
        DockController& operator=(const DockController&) = delete;

        bool collapsed(DockSide side) const { return settings_.docks[index(side)].collapsed; }
        // The size the area has, or comes back at.
        float size(DockSide side) const { return settings_.docks[index(side)].size; }
        void setCollapsed(DockSide side, bool collapsed);
        void toggle(DockSide side) { setCollapsed(side, !collapsed(side)); }
        void setSize(DockSide side, float size);
        // Shows the areas as Settings::docks now says (after the settings were changed from elsewhere); nothing is saved.
        void refresh();

        newui::Splitter* splitter(DockSide side) const { return parts_[index(side)].splitter; }
        newui::SubView* area(DockSide side) const { return parts_[index(side)].area; }

        static const char* name(DockSide side) { return Settings::dockName(index(side)); }

    private:
        struct Part {
            newui::Splitter* splitter = nullptr;
            newui::SubView* area = nullptr;
        };
        static int index(DockSide side) { return int(side); }

        void bind(DockSide side);
        void apply(DockSide side);
        void guardDrag(DockSide side);
        void dragged(DockSide side);

        Settings& settings_;
        std::function<void()> changed_;
        Part parts_[Settings::dockCount];
    };

}
