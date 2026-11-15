#pragma once

#include "curvy/docklayout.h"
#include "curvy/panelframe.h"
#include "curvy/paneldock.h"

#include <newui/toolframe.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace curvy {

    // The floating side of DockLayout: one ToolFrame (a real tool window) for every panel the layout says is floating. The panel view itself moves into
    // the frame and back into its dock, so it keeps its state and its controllers. Closing the frame, or the panel's own Close while it floats, docks
    // the panel back at the bottom of its dock (the layout does that; sync() follows). A frame is hidden, never destroyed, when its panel goes back: the
    // close arrives from inside the frame's own message handler, and the next tear-off reuses it.
    // @reflect ignore=true
    class FloatingPanels {
    public:
        static constexpr float kWidth = 320.0f;
        static constexpr float kFrameChrome = 36.0f;   // the caption and borders around the panel, added to its height for the window

        // owner: the main window (keeps the frames above it). dockFor gives the dock view that holds a side's panels.
        FloatingPanels(HWND owner, DockLayout& layout, std::function<PanelDock*(DockSide)> dockFor);

        // Makes the frames match the layout; call after every layout change.
        void sync();

        // Null when the panel has never floated.
        newui::ToolFrame* frame(const std::string& id) const;
        bool showing(const std::string& id) const;
        // The window's caption is being dragged with the pointer at `screen`: outlines the dock when the pointer is over it. drop() is the release: over the
        // dock the panel goes back into it at the line, anywhere else it stays where it is.
        void hover(const std::string& id, const newui::Point& screen);
        void drop(const std::string& id, const newui::Point& screen);
        // Where the window is on screen now; false when it is not showing.
        bool screenPosition(const std::string& id, float& x, float& y) const;

    private:
        struct Entry {
            std::unique_ptr<PanelFrame> frame;
            Panel* panel = nullptr;
        };

        Entry& ensure(const PanelInfo& info, PanelDock& dock);

        HWND owner_;
        DockLayout& layout_;
        std::function<PanelDock*(DockSide)> dockFor_;
        std::map<std::string, Entry> entries_;
    };

}
