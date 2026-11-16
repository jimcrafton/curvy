#pragma once

#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/subview.h>

#include <string>

namespace curvy {

    // The base of every dockable panel (docs/mockups/panel.html, Resources/panel.newui): the chrome and an empty scrollable body. The chrome is a title bar
    // with a collapse caret, a grip, the title, a menu button and a close button (icons in Resources/icons/panel-*.svg), all always visible as in the mockup. A derived
    // panel (DocumentPanel) puts its content into body(). The panel knows nothing of docks: it announces what the user did and the owner (the dock, or a
    // ToolFrame) decides what that means, so Close hides a docked panel and docks a floating one back.
    //
    // Points in the drag events are screen coordinates, so they stay meaningful while the panel itself moves.
    // @reflect ignore=true
    class Panel : public newui::SubView {
    public:
        static constexpr float kTitleHeight = 20.0f;
        static constexpr float kBorder = 1.0f;
        static constexpr float kDefaultBodyHeight = 120.0f;
        // How far the pointer must move after a press on the title before it counts as a drag.
        static constexpr float kDragThreshold = 3.0f;

        // Throws std::runtime_error when Resources/panel.newui does not load.
        Panel(std::string id, std::string title);

        const std::string& id() const { return id_; }
        const std::string& title() const { return title_; }
        void setTitle(const std::string& title);

        bool collapsed() const { return collapsed_; }
        // Only the title bar shows when collapsed.
        void setCollapsed(bool collapsed);
        void toggleCollapsed() { setCollapsed(!collapsed_); }

        // False for a panel that stays where it is: no grip, and a press on the title does not start a drag.
        bool movable() const { return movable_; }
        void setMovable(bool movable);

        // False while the panel floats in a window of its own: the caret is disabled and the panel stays expanded.
        bool collapsible() const { return collapsible_; }
        void setCollapsible(bool collapsible);

        // The active panel (the one the user last pressed in) draws a subtle accent outline; the owner decides which one is active.
        bool active() const { return active_; }
        void setActive(bool active);
        // A press anywhere in the panel.
        newui::Delegate<Panel> onActivateRequested;
        bool resizing() const { return resizing_; }

        // True while docked: a strip under the body can be dragged to change bodyHeight(). Hidden when collapsed.
        bool resizable() const { return resizable_; }
        void setResizable(bool resizable);
        bool resizeHandleVisible() const { return resizer_->isVisible(); }
        static constexpr float kMinBodyHeight = 40.0f;
        // What a TabControl's strip takes at the top of a panel's body, for a panel that works out the height its tabs need.
        static constexpr float kTabStripHeight = 36.0f;
        static constexpr float kResizeHeight = 6.0f;

        // The scrollable area a derived panel fills; call body().setContentSize() when the content's height changes.
        newui::ScrollView& body() { return *body_; }
        // How tall the body wants to be when expanded; the panel's desired height follows (title bar, body and borders, or the title bar alone when collapsed).
        float bodyHeight() const { return bodyHeight_; }
        void setBodyHeight(float height);
        // How tall the panel wants its body (what its content needs). It becomes the panel's desired size, which the dock stacks, unless the user has
        // dragged the panel's resize strip: then their height stays. Call it whenever the content's natural height changes.
        void setPreferredBodyHeight(float height);
        // True once the user resized the body by hand (or a saved height was restored): only then is the height worth saving.
        bool userResized() const { return userResized_; }
        // Applies a height the user chose in an earlier session.
        void restoreBodyHeight(float height);
        float desiredHeight() const;

        // What is on show, for tests.
        const std::string& caretIcon() const { return caret_->icon(); }
        std::string titleText() const { return titleLabel_->text(); }
        newui::SubView& titleBar() { return *titleBar_; }
        newui::ToolbarButton& caret() { return *caret_; }
        newui::ToolbarButton& grip() { return *grip_; }
        newui::ToolbarButton& menuButton() { return *menu_; }
        newui::ToolbarButton& closeButton() { return *close_; }

        // Fired after the collapsed state changed.
        newui::Delegate<Panel> onCollapsedChanged;
        // The close button was pressed: the owner hides the panel (docked) or docks it back (floating).
        newui::Delegate<Panel> onCloseRequested;
        // The menu button was pressed; `at` is where to open the menu (screen coordinates, under the button).
        newui::Delegate<Panel, const newui::Point&> onMenuRequested;
        // A press on the title moved past kDragThreshold; then every move; then the release (or the pointer lifted without moving: never fired).
        newui::Delegate<Panel, const newui::Point&> onTitleDragStarted;
        newui::Delegate<Panel, const newui::Point&> onTitleDragged;
        newui::Delegate<Panel, const newui::Point&> onTitleDragEnded;

        // The gestures as the mouse handlers run them, so a test can drive them without a window (screen points).
        void pressTitle(const newui::Point& screen);
        void moveTitle(const newui::Point& screen, bool buttonDown);
        void releaseTitle(const newui::Point& screen);
        bool dragging() const { return dragging_; }

        // Fired while the user drags the resize strip (the owner stacks its panels again).
        newui::Delegate<Panel> onBodyHeightChanged;
        // The user let go of the strip: the height to keep.
        newui::Delegate<Panel> onResizeFinished;
        void pressResize(const newui::Point& screen);
        void moveResize(const newui::Point& screen, bool buttonDown);
        void releaseResize();

    private:
        void updateResizer();
        void updateLook();
        void bindDragSource(newui::View& view);
        void layoutContent();
        void updateSize();

        std::string id_;
        std::string title_;
        bool collapsed_ = false;
        bool movable_ = true;
        bool collapsible_ = true;
        bool resizable_ = true;
        bool resizing_ = false;
        bool active_ = false;
        float resizeStartY_ = 0.0f;
        float resizeStartHeight_ = 0.0f;
        newui::SubView* resizer_ = nullptr;
        float bodyHeight_ = kDefaultBodyHeight;
        bool userResized_ = false;

        newui::SubView* content_ = nullptr;
        newui::SubView* box_ = nullptr;
        newui::SubView* titleBar_ = nullptr;
        newui::ToolbarButton* caret_ = nullptr;
        newui::ToolbarButton* grip_ = nullptr;
        newui::Label* titleLabel_ = nullptr;
        newui::ToolbarButton* menu_ = nullptr;
        newui::ToolbarButton* close_ = nullptr;
        newui::ScrollView* body_ = nullptr;

        bool pressed_ = false;
        bool dragging_ = false;
        newui::Point pressPoint_;
    };

}
