#include "curvy/floatingpanels.h"

#include <stdexcept>

namespace curvy {

    using namespace newui;

    FloatingPanels::FloatingPanels(HWND owner, DockLayout& layout, std::function<PanelDock*(DockSide)> dockFor)
        : owner_(owner), layout_(layout), dockFor_(std::move(dockFor)) {}

    ToolFrame* FloatingPanels::frame(const std::string& id) const {
        const auto found = entries_.find(id);
        return found == entries_.end() ? nullptr : found->second.frame.get();
    }

    bool FloatingPanels::showing(const std::string& id) const {
        const ToolFrame* f = frame(id);
        return f != nullptr && f->isVisible();
    }

    bool FloatingPanels::screenPosition(const std::string& id, float& x, float& y) const {
        const ToolFrame* f = frame(id);
        RECT rect = {};
        if (f == nullptr || !f->isVisible() || f->frameHandle() == nullptr || !::GetWindowRect(f->frameHandle(), &rect)) {
            return false;
        }
        x = float(rect.left);
        y = float(rect.top);
        return true;
    }

    FloatingPanels::Entry& FloatingPanels::ensure(const PanelInfo& info, PanelDock& dock) {
        Entry& entry = entries_[info.id];
        if (entry.frame != nullptr) {
            return entry;
        }
        Panel* panel = dock.panel(info.id);
        if (panel == nullptr) {
            throw std::runtime_error("FloatingPanels: the dock has no panel \"" + info.id + "\"");
        }
        entry.panel = panel;
        entry.frame = std::make_unique<PanelFrame>();
        PanelFrame& frame = *entry.frame;
        frame.setTitle(info.title);
        frame.setBounds(Rect(info.floatX, info.floatY, kWidth, panel->desiredHeight() + kFrameChrome));
        frame.setMinimumSize(Size(200.0f, 60.0f));
        if (owner_ == nullptr || !frame.initialize(owner_)) {
            entries_.erase(info.id);
            throw std::runtime_error("FloatingPanels: the window for \"" + info.id + "\" could not be created");
        }
        frame.rootView().onSizeChanged += [panel](View&, const Size& size) {
            panel->setBounds(Rect(0.0f, 0.0f, size.width, size.height));
            return SyncReturn::Handled;
        };
        const std::string id = info.id;
        frame.onClosed += [this, id](ToolFrame&) {
            layout_.dock(id);
            return SyncReturn::Handled;
        };
        frame.onMoving += [this, id](PanelFrame&, const Point& at) {
            hover(id, at);
            return SyncReturn::Handled;
        };
        frame.onMoveEnded += [this, id](PanelFrame&, const Point& at) {
            drop(id, at);
            return SyncReturn::Handled;
        };
        return entry;
    }

    void FloatingPanels::hover(const std::string& id, const Point& screen) {
        const PanelInfo* info = layout_.find(id);
        const auto entry = entries_.find(id);
        PanelDock* dock = info == nullptr ? nullptr : dockFor_(info->side);
        if (dock != nullptr && entry != entries_.end()) {
            dock->showDropTarget(*entry->second.panel, screen);
        }
    }

    void FloatingPanels::drop(const std::string& id, const Point& screen) {
        const PanelInfo* info = layout_.find(id);
        const auto entry = entries_.find(id);
        PanelDock* dock = info == nullptr ? nullptr : dockFor_(info->side);
        if (dock == nullptr || entry == entries_.end()) {
            return;
        }
        dock->hideDropTarget();
        if (info->floating && dock->containsScreen(screen)) {
            layout_.dock(id, dock->dropIndexAt(*entry->second.panel, screen));
        }
    }

    void FloatingPanels::sync() {
        for (const PanelInfo& info : layout_.panels()) {
            PanelDock* dock = dockFor_(info.side);
            if (dock == nullptr) {
                continue;
            }
            const bool floats = info.floating && layout_.shown(info.id);
            const auto existing = entries_.find(info.id);
            if (!floats) {
                if (existing == entries_.end() || existing->second.frame == nullptr) {
                    continue;
                }
                Entry& entry = existing->second;
                if (entry.panel->parent() != dock) {
                    if (info.floating) {   // floating but put away: the panel waits in its frame, hidden
                        entry.frame->hide();
                        continue;
                    }
                    entry.panel->setParent(dock);
                    entry.panel->setMovable(true);
                    entry.panel->setCollapsible(true);
                    entry.panel->setResizable(true);
                }
                entry.frame->hide();
                continue;
            }
            Entry& entry = ensure(info, *dock);
            if (entry.panel->parent() != &entry.frame->rootView()) {
                entry.panel->setParent(&entry.frame->rootView());
                entry.panel->setMovable(false);   // the window's caption moves it
                entry.panel->setCollapsible(false);
                entry.panel->setResizable(false);   // the window's edge sizes it
                entry.frame->moveTo(Rect(info.floatX, info.floatY, kWidth, entry.panel->desiredHeight() + kFrameChrome));
                const Size size = entry.frame->rootView().bounds().size();
                entry.panel->setBounds(Rect(0.0f, 0.0f, size.width, size.height));
            }
            entry.panel->setVisible(true);
            if (!entry.frame->isVisible()) {
                entry.frame->show();
            }
        }
    }

}
