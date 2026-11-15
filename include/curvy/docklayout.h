#pragma once

#include "curvy/dockcontroller.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // The state of one panel (docs/mockups/dockPanels.html): which dock it belongs to, whether it can leave it, and what it is doing now.
    // @reflect ignore=true
    struct PanelInfo {
        std::string id;                    // unique; the name of the panel's view
        std::string title;
        DockSide side = DockSide::Right;   // the dock it lives in, and comes back to
        bool canTearOff = true;            // false: it stays in its dock and cannot be reordered (the toolbar and the timeline)
        bool collapsed = false;            // only its title area shows
        bool userHidden = false;           // put away from the Panels menu or the panel's own Hide
        bool contextVisible = true;        // the selection says it applies (the Text panel with text selected); the owner sets it
        bool floating = false;             // in a ToolFrame, not in its dock
        float floatX = 0.0f, floatY = 0.0f;   // where the frame is (the owner's coordinates), while floating
    };

    // A rectangle in the owner's coordinates (the dock's area).
    // @reflect ignore=true
    struct DockBox {
        float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;

        bool contains(float x, float y) const { return x >= left && x <= right && y >= top && y <= bottom; }
        // How far the point is outside the box (0 inside), the larger of the horizontal and vertical distances.
        float outsideBy(float x, float y) const;
    };

    // The panels of the workspace and where each one is: docked in one of the four docks (in order), floating, or put away. Pure state with no views,
    // so the rules can be tested without a window; the dock view reads it and the user's drags change it. A panel is shown when it is neither
    // userHidden nor out of context. Not undoable and not part of the document. Every change that alters something calls onChanged.
    // @reflect ignore=true
    class DockLayout {
    public:
        // How far outside its dock a title must be dragged before the panel is torn off.
        static constexpr float kTearOffDistance = 40.0f;
        static constexpr std::size_t kEnd = ~std::size_t(0);   // not numeric_limits::max(): windows.h defines max

        // Throws std::runtime_error for an empty or taken id. A new panel goes at the end of its dock.
        void add(const PanelInfo& info);
        const std::vector<PanelInfo>& panels() const { return panels_; }
        // Null when there is no such panel.
        const PanelInfo* find(const std::string& id) const;
        bool shown(const std::string& id) const;

        // The ids of the shown panels docked in `side`, top to bottom.
        std::vector<std::string> docked(DockSide side) const;
        // The ids of the shown floating panels.
        std::vector<std::string> floating() const;

        // Moves a docked panel inside its dock to `index` among the other shown panels of the dock (kEnd or past the last: the bottom). A hidden panel
        // keeps its place in the order. Throws for an unknown panel, a floating one, or one that cannot be torn off (it is fixed in place).
        void move(const std::string& id, std::size_t index);
        // Makes the panel floating at (x, y). Throws when it cannot be torn off or is unknown.
        void tearOff(const std::string& id, float x, float y);
        void moveFloating(const std::string& id, float x, float y);
        // Puts a floating panel back into its dock at `index` (as move()). Closing a floating frame is dock(id) with no index: it goes to the bottom.
        void dock(const std::string& id, std::size_t index = kEnd);

        void setUserHidden(const std::string& id, bool hidden);
        void setContextVisible(const std::string& id, bool visible);
        void setCollapsed(const std::string& id, bool collapsed);
        // The panel the user last worked in (empty: none). Not saved with the layout.
        const std::string& activeId() const { return activeId_; }
        void setActive(const std::string& id);

        // Whether dragging this panel's title to (x, y) should tear it off: it can leave and the point is kTearOffDistance or more outside the dock.
        bool shouldTearOff(const std::string& id, const DockBox& dock, float x, float y) const;
        // Where a dragged panel would land: `middles` are the vertical centres of the other shown panels of the dock, top to bottom; the result is how
        // many of them are above `y`, which is the index to give move() or dock().
        static std::size_t dropIndex(const std::vector<float>& middles, float y);

        // Called after a change that altered something.
        std::function<void()> onChanged;

    private:
        PanelInfo& at(const std::string& id);
        void changed() {
            if (onChanged) {
                onChanged();
            }
        }
        // Takes the panel out of the list and puts it back so it is the `index`th shown docked panel of its dock.
        void place(const std::string& id, std::size_t index);

        std::vector<PanelInfo> panels_;
        std::string activeId_;
    };

}
