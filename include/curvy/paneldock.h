#pragma once

#include "curvy/docklayout.h"
#include "curvy/panel.h"

#include <newui/delegate.h>
#include <newui/runloop.h>
#include <newui/subview.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace curvy {

    // The view of one dock (docs/mockups/dockPanels.html): stacks the shown panels the DockLayout puts in its side, top to bottom, and turns what the
    // user does to a panel into DockLayout changes: the caret collapses it, Close hides it, dragging its title re-orders it (an amber line shows where
    // it would land). It never decides anything itself; every change goes through the layout and comes back through DockLayout::onChanged -> sync().
    // The dock owns the panel views (they are its children); the layout owns the state.
    // @reflect ignore=true
    class PanelDock : public newui::SubView {
    public:
        static constexpr float kSpacing = 6.0f;
        static constexpr float kDropLineHeight = 2.0f;
        // Where the pointer sits in a torn-off window: the panel's title bar.
        static constexpr float kTearOffGrabX = 60.0f;
        static constexpr float kTearOffGrabY = 30.0f;

        PanelDock(DockLayout& layout, DockSide side);
        ~PanelDock() override;

        // Takes ownership of the view (through addChild). The panel must already be in the layout, under its id.
        void addPanel(Panel* panel);
        Panel* panel(const std::string& id) const;

        // Stacks the panels again from the layout (collapsed, hidden, order).
        void sync();

        // Where the drop line is while a title is dragged (y in this view's coordinates), or a negative number when it is not showing.
        float dropLineY() const { return dropLine_->isVisible() ? dropLine_->bounds().top() : -1.0f; }
        // Where the dragged panel would land: the index among the other shown panels of this dock.
        std::size_t dropIndexAt(const Panel& dragged, const newui::Point& screen) const;

        // The wheel scrolls the stack when the panels are taller than the dock (there is no scrollbar: a thin bar shows where you are for kHintMs).
        static constexpr float kWheelStep = 40.0f;
        static constexpr int kHintMs = 700;
        static constexpr float kHintWidth = 3.0f;
        float scrollOffset() const { return scroll_; }
        float contentHeight() const { return contentHeight_; }
        // `notches` as the wheel reports them (positive = away from the user = content moves down). Returns whether the dock scrolled.
        bool scrollBy(float notches);
        bool positionHintShowing() const { return hint_->isVisible(); }

        // A floating panel's window is being dragged: when the pointer is over this dock, outlines it and shows the line where the panel would land.
        void showDropTarget(const Panel& floating, const newui::Point& screen);
        void hideDropTarget();
        bool containsScreen(const newui::Point& screen) const;
        bool dropTargetShowing() const { return outline_->isVisible(); }

        // A Close on a docked panel puts it away (it comes back from the Panels menu).
        // A menu request is passed on for the owner to open (the menu has no entries until the Panels menu exists).
        newui::Delegate<PanelDock, Panel&, const newui::Point&> onPanelMenu;
        // The user finished dragging a panel's resize strip.
        newui::Delegate<PanelDock, Panel&> onPanelResized;

    private:
        void layoutPanels();
        void showDropLine(const Panel& dragged, const newui::Point& screen);
        bool wouldTearOff(const Panel& dragged, const newui::Point& screen) const;

        DockLayout& layout_;
        DockSide side_;
        std::unordered_map<std::string, Panel*> panels_;
        newui::SubView* dropLine_ = nullptr;
        newui::SubView* outline_ = nullptr;
        newui::SubView* hint_ = nullptr;
        float scroll_ = 0.0f;
        float contentHeight_ = 0.0f;
        newui::RunLoop::TimerHandle hintTimer_ = newui::RunLoop::kInvalidTimerHandle;
        void updateHint();
    };

}
