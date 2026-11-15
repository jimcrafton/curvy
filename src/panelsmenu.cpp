#include "curvy/panelsmenu.h"

#include <newui/menus.h>

namespace curvy {

    using namespace newui;

    std::vector<PanelMenuEntry> PanelsMenu::forPanels(DockLayout& layout, bool dockShown, std::function<void()> openDock, std::function<void()> toggleDock) {
        std::vector<PanelMenuEntry> entries;
        for (const PanelInfo& info : layout.panels()) {
            PanelMenuEntry entry;
            entry.text = info.title;
            entry.checked = !info.userHidden;
            entry.run = [&layout, id = info.id, show = info.userHidden, openDock]() {
                layout.setUserHidden(id, !show);
                const PanelInfo* now = layout.find(id);
                if (show && now != nullptr && !now->floating && openDock) {
                    openDock();
                }
            };
            entries.push_back(entry);
        }
        PanelMenuEntry line;
        line.separator = true;
        entries.push_back(line);
        PanelMenuEntry toggle;
        toggle.text = dockShown ? "Hide panel dock" : "Show panel dock";
        toggle.run = std::move(toggleDock);
        entries.push_back(toggle);
        return entries;
    }

    std::vector<PanelMenuEntry> PanelsMenu::forPanel(DockLayout& layout, Panel& panel, const Point& floatAt) {
        std::vector<PanelMenuEntry> entries;
        const PanelInfo* info = layout.find(panel.id());
        if (info == nullptr) {
            return entries;
        }
        if (panel.collapsible()) {
            PanelMenuEntry collapse;
            collapse.text = panel.collapsed() ? "Expand" : "Collapse";
            collapse.run = [&panel]() { panel.toggleCollapsed(); };
            entries.push_back(collapse);
        }
        if (info->canTearOff) {
            PanelMenuEntry place;
            if (info->floating) {
                place.text = "Dock";
                place.run = [&layout, id = panel.id()]() { layout.dock(id); };
            } else {
                place.text = "Float";
                place.run = [&layout, id = panel.id(), floatAt]() { layout.tearOff(id, floatAt.x, floatAt.y); };
            }
            entries.push_back(place);
        }
        PanelMenuEntry hide;
        hide.text = "Hide";
        hide.run = [&layout, id = panel.id()]() { layout.setUserHidden(id, true); };
        entries.push_back(hide);
        return entries;
    }

    void PanelsMenu::popup(HWND owner, const std::vector<PanelMenuEntry>& entries, const Point& screen) {
        if (owner == nullptr) {
            return;
        }
        MenuItem menu;
        for (const PanelMenuEntry& entry : entries) {
            if (entry.separator) {
                menu.addChild(MenuItem::Separator());
                continue;
            }
            MenuItem* item = menu.addChild(std::make_unique<MenuItem>(entry.text));
            item->setChecked(entry.checked);
            item->onClick += [run = entry.run](MenuItem&) {
                if (run) {
                    run();
                }
                return SyncReturn::Handled;
            };
        }
        ContextMenu context;
        context.show(owner, menu, int(screen.x), int(screen.y));
    }

}
