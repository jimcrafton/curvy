#include "curvy/dockcontroller.h"

#include <newui/rootview.h>

#include <algorithm>
#include <cmath>

namespace curvy {

    using namespace newui;

    DockController::DockController(Frame& frame, Settings& settings, std::function<void()> changed) : settings_(settings), changed_(std::move(changed)) {
        for (int i = 0; i < Settings::dockCount; ++i) {
            const std::string prefix = Settings::dockName(i);
            parts_[i].splitter = dynamic_cast<Splitter*>(frame.rootView().findView(prefix + "Splitter"));
            parts_[i].area = dynamic_cast<SubView*>(frame.rootView().findView(prefix + "Area"));
            settings_.docks[i].size = std::max(settings_.docks[i].size, Settings::minDockSize);
            bind(DockSide(i));
            apply(DockSide(i));
        }
    }

    void DockController::bind(DockSide side) {
        Splitter* splitter = parts_[index(side)].splitter;
        if (splitter == nullptr) {
            return;
        }
        splitter->onMouseDblClick += [this, side](View&, const Point&, std::uint32_t, std::uint32_t) {
            toggle(side);
            return SyncReturn::Handled;
        };
        splitter->onMouseMove += [this, side](View&, const Point&, std::uint32_t, std::uint32_t) {
            guardDrag(side);
            return SyncReturn::Ignored;
        };
        splitter->onMouseUp += [this, side](View&, const Point&, std::uint32_t, std::uint32_t) {
            dragged(side);
            return SyncReturn::Ignored;
        };
    }

    void DockController::apply(DockSide side) {
        const Part& part = parts_[index(side)];
        const Settings::DockArea& state = settings_.docks[index(side)];
        if (part.splitter != nullptr) {
            // The minimum keeps a dragged divider off the window edge and the canvas from being squeezed out; a hidden area is the one case below it.
            part.splitter->setMinPaneSize(state.collapsed ? 0.0f : Settings::minDockSize);
            part.splitter->setSplitPosition(state.collapsed ? 0.0f : state.size);
        }
        if (part.area != nullptr) {
            part.area->setVisible(!state.collapsed);
        }
    }

    void DockController::refresh() {
        for (int i = 0; i < Settings::dockCount; ++i) {
            settings_.docks[i].size = std::max(settings_.docks[i].size, Settings::minDockSize);
            apply(DockSide(i));
        }
    }

    void DockController::setCollapsed(DockSide side, bool collapsed) {
        Settings::DockArea& state = settings_.docks[index(side)];
        if (state.collapsed == collapsed) {
            return;
        }
        state.collapsed = collapsed;
        apply(side);
        changed_();
    }

    void DockController::setSize(DockSide side, float size) {
        Settings::DockArea& state = settings_.docks[index(side)];
        state.size = std::max(size, Settings::minDockSize);
        state.collapsed = false;
        apply(side);
        changed_();
    }

    // A drag step that would leave the area smaller than the minimum (or negative), or the canvas side too small, is ignored: the divider
    // stays where it last was valid.
    void DockController::guardDrag(DockSide side) {
        Settings::DockArea& state = settings_.docks[index(side)];
        Splitter* splitter = parts_[index(side)].splitter;
        if (splitter == nullptr) {
            return;
        }
        if (state.collapsed) {   // dragging a hidden area's splitter opens it
            if (splitter->splitPosition() > 1.0f) {
                state.collapsed = false;
                state.size = std::max(splitter->splitPosition(), Settings::minDockSize);
                apply(side);
                changed_();
            }
            return;
        }
        const Size client = splitter->getClientBounds().size();
        const float extent = (splitter->orientation() == Orientation::Horizontal) ? client.width : client.height;
        const float position = splitter->splitPosition();
        const float most = extent - splitter->dividerThickness() - Settings::minDockSize;
        const bool valid = std::isfinite(position) && position >= Settings::minDockSize && position <= std::max(most, Settings::minDockSize);
        if (valid) {
            state.size = position;
        } else {
            splitter->setSplitPosition(state.size);
        }
    }

    // The user moved the divider: whatever the splitter now says is the area's size.
    void DockController::dragged(DockSide side) {
        Settings::DockArea& state = settings_.docks[index(side)];
        Splitter* splitter = parts_[index(side)].splitter;
        if (splitter == nullptr || state.collapsed) {
            return;
        }
        state.size = std::max(splitter->splitPosition(), Settings::minDockSize);
        changed_();
    }

}
