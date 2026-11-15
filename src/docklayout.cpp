#include "curvy/docklayout.h"

#include <algorithm>
#include <stdexcept>

namespace curvy {

    float DockBox::outsideBy(float x, float y) const {
        return std::max({0.0f, left - x, x - right, top - y, y - bottom});
    }

    void DockLayout::add(const PanelInfo& info) {
        if (info.id.empty()) {
            throw std::runtime_error("a panel needs an id");
        }
        if (find(info.id) != nullptr) {
            throw std::runtime_error("there is already a panel \"" + info.id + "\"");
        }
        panels_.push_back(info);
        changed();
    }

    const PanelInfo* DockLayout::find(const std::string& id) const {
        const auto it = std::find_if(panels_.begin(), panels_.end(), [&](const PanelInfo& p) { return p.id == id; });
        return it == panels_.end() ? nullptr : &*it;
    }

    PanelInfo& DockLayout::at(const std::string& id) {
        const auto it = std::find_if(panels_.begin(), panels_.end(), [&](const PanelInfo& p) { return p.id == id; });
        if (it == panels_.end()) {
            throw std::runtime_error("there is no panel \"" + id + "\"");
        }
        return *it;
    }

    bool DockLayout::shown(const std::string& id) const {
        const PanelInfo* panel = find(id);
        return panel != nullptr && !panel->userHidden && panel->contextVisible;
    }

    std::vector<std::string> DockLayout::docked(DockSide side) const {
        std::vector<std::string> ids;
        for (const PanelInfo& p : panels_) {
            if (p.side == side && !p.floating && !p.userHidden && p.contextVisible) {
                ids.push_back(p.id);
            }
        }
        return ids;
    }

    std::vector<std::string> DockLayout::floating() const {
        std::vector<std::string> ids;
        for (const PanelInfo& p : panels_) {
            if (p.floating && !p.userHidden && p.contextVisible) {
                ids.push_back(p.id);
            }
        }
        return ids;
    }

    void DockLayout::place(const std::string& id, std::size_t index) {
        const auto from = std::find_if(panels_.begin(), panels_.end(), [&](const PanelInfo& p) { return p.id == id; });
        PanelInfo moved = *from;
        panels_.erase(from);
        moved.floating = false;
        // The shown docked panels of this dock, now without the moved one; the panel goes before the `index`th of them, or after the last.
        std::vector<std::size_t> shownAt;
        for (std::size_t i = 0; i < panels_.size(); ++i) {
            const PanelInfo& p = panels_[i];
            if (p.side == moved.side && !p.floating && !p.userHidden && p.contextVisible) {
                shownAt.push_back(i);
            }
        }
        std::size_t position = panels_.size();
        if (!shownAt.empty()) {
            position = index < shownAt.size() ? shownAt[index] : shownAt.back() + 1;
        } else {
            // Nothing shown in this dock: go after the dock's last panel, so the order of the hidden ones is kept.
            for (std::size_t i = 0; i < panels_.size(); ++i) {
                if (panels_[i].side == moved.side) {
                    position = i + 1;
                }
            }
        }
        panels_.insert(panels_.begin() + std::ptrdiff_t(position), moved);
    }

    void DockLayout::move(const std::string& id, std::size_t index) {
        PanelInfo& panel = at(id);
        if (panel.floating) {
            throw std::runtime_error("panel \"" + id + "\" is floating; dock it first");
        }
        if (!panel.canTearOff) {
            throw std::runtime_error("panel \"" + id + "\" is fixed in its dock");
        }
        const std::vector<PanelInfo> before = panels_;
        place(id, index);
        const auto same = [](const std::vector<PanelInfo>& a, const std::vector<PanelInfo>& b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const PanelInfo& x, const PanelInfo& y) { return x.id == y.id; });
        };
        if (!same(before, panels_)) {
            changed();
        }
    }

    void DockLayout::tearOff(const std::string& id, float x, float y) {
        PanelInfo& panel = at(id);
        if (!panel.canTearOff) {
            throw std::runtime_error("panel \"" + id + "\" cannot be torn off");
        }
        panel.floating = true;
        panel.floatX = x;
        panel.floatY = y;
        changed();
    }

    void DockLayout::moveFloating(const std::string& id, float x, float y) {
        PanelInfo& panel = at(id);
        if (!panel.floating) {
            throw std::runtime_error("panel \"" + id + "\" is not floating");
        }
        if (panel.floatX != x || panel.floatY != y) {
            panel.floatX = x;
            panel.floatY = y;
            changed();
        }
    }

    void DockLayout::dock(const std::string& id, std::size_t index) {
        PanelInfo& panel = at(id);
        if (!panel.floating) {
            throw std::runtime_error("panel \"" + id + "\" is already docked");
        }
        panel.floating = false;
        place(id, index);
        changed();
    }

    void DockLayout::setUserHidden(const std::string& id, bool hidden) {
        PanelInfo& panel = at(id);
        if (panel.userHidden != hidden) {
            panel.userHidden = hidden;
            changed();
        }
    }

    void DockLayout::setContextVisible(const std::string& id, bool visible) {
        PanelInfo& panel = at(id);
        if (panel.contextVisible != visible) {
            panel.contextVisible = visible;
            changed();
        }
    }

    void DockLayout::setCollapsed(const std::string& id, bool collapsed) {
        PanelInfo& panel = at(id);
        if (panel.collapsed != collapsed) {
            panel.collapsed = collapsed;
            changed();
        }
    }

    void DockLayout::setActive(const std::string& id) {
        if (!id.empty()) {
            at(id);   // throws on an unknown id
        }
        if (activeId_ != id) {
            activeId_ = id;
            changed();
        }
    }

    bool DockLayout::shouldTearOff(const std::string& id, const DockBox& dock, float x, float y) const {
        const PanelInfo* panel = find(id);
        return panel != nullptr && panel->canTearOff && !panel->floating && dock.outsideBy(x, y) >= kTearOffDistance;
    }

    std::size_t DockLayout::dropIndex(const std::vector<float>& middles, float y) {
        return std::size_t(std::count_if(middles.begin(), middles.end(), [y](float middle) { return middle < y; }));
    }

}
