#pragma once

#include "curvy/docklayout.h"
#include "curvy/panel.h"

#include <newui/geometry.h>

#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // One line of a panel menu: built as data so the lines can be tested, then shown as a native popup.
    // @reflect ignore=true
    struct PanelMenuEntry {
        std::string text;
        bool checked = false;
        bool separator = false;
        std::function<void()> run;
    };

    // The two menus of docks and panels (docs/mockups/dockPanels.html): the top bar's Panels button lists every panel with a tick on the shown ones
    // (clicking one shows or hides it, and a shown docked panel needs its dock open), and a panel's own "..." button offers Collapse, Float or Dock, and
    // Hide. They only change the DockLayout (and the panel's collapsed state, which reports back to it); the views follow from the layout.
    // @reflect ignore=true
    class PanelsMenu {
    public:
        // openDock: makes sure the panel dock is showing (a panel shown from the menu is no use in a closed dock). dockShown / toggleDock: the last line.
        static std::vector<PanelMenuEntry> forPanels(DockLayout& layout, bool dockShown, std::function<void()> openDock, std::function<void()> toggleDock);

        // floatAt: where a torn-off window opens (screen coordinates).
        static std::vector<PanelMenuEntry> forPanel(DockLayout& layout, Panel& panel, const newui::Point& floatAt);

        // Shows the entries as a native popup at the screen point; blocks until a line ran or the menu was dismissed. Does nothing for a null window.
        static void popup(HWND owner, const std::vector<PanelMenuEntry>& entries, const newui::Point& screen);
    };

}
