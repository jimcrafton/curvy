#include "curvy/panel.h"

#include <newui/bundle.h>
#include <newui/mouse_constants.h>
#include <newui/viewstyle.h>

#include "curvy/theme.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    namespace {
        template <typename T>
        T* need(SubView& root, const char* name) {
            auto* view = dynamic_cast<T*>(root.findView(name));
            if (view == nullptr) {
                throw std::runtime_error(std::string("Panel: Resources/panel.newui has no view named \"") + name + "\"");
            }
            return view;
        }

        constexpr const char* kCaretOpen = "icons/panel-caret-open.svg";
        constexpr const char* kCaretClosed = "icons/panel-caret-closed.svg";
    }

    Panel::Panel(std::string id, std::string title) : id_(std::move(id)), title_(std::move(title)) {
        setName(id_);
        setVisible(true);

        View* loaded = Bundle::instance().loadView("panel");
        content_ = dynamic_cast<SubView*>(loaded);
        if (content_ == nullptr) {
            delete loaded;
            throw std::runtime_error("Panel: Resources/panel.newui did not load");
        }
        addChild(content_);
        box_ = need<SubView>(*content_, "panelBox");
        titleBar_ = need<SubView>(*content_, "panelTitleBar");
        caret_ = need<ToolbarButton>(*content_, "panelCaret");
        grip_ = need<ToolbarButton>(*content_, "panelGrip");
        titleLabel_ = need<Label>(*content_, "panelTitle");
        menu_ = need<ToolbarButton>(*content_, "panelMenu");
        close_ = need<ToolbarButton>(*content_, "panelClose");
        body_ = need<ScrollView>(*content_, "panelBody");
        caret_->setIcon(kCaretOpen);
        caret_->setIconSize(10.0f);
        grip_->setIcon("icons/panel-grip.svg");
        menu_->setIcon("icons/panel-menu.svg");
        close_->setIcon("icons/panel-close.svg");
        for (ToolbarButton* button : {grip_, menu_, close_}) {
            button->setIconSize(14.0f);
        }
        // The mockup is drawn at 268 x 200; the box and its title bar take the panel's width and the panel's height from layoutContent.
        box_->setDesiredSize(Size(0.0f, 0.0f));
        titleBar_->setDesiredSize(Size(0.0f, kTitleHeight));
        titleLabel_->setText(title_);

        caret_->onClick += [this](Control&) {
            toggleCollapsed();
            return SyncReturn::Handled;
        };
        menu_->onClick += [this](Control&) {
            onMenuRequested(*this, menu_->localToScreen(Point(0.0f, menu_->bounds().height())));
            return SyncReturn::Handled;
        };
        close_->onClick += [this](Control&) {
            onCloseRequested(*this);
            return SyncReturn::Handled;
        };
        bindDragSource(*titleBar_);
        bindDragSource(*grip_);
        bindDragSource(*titleLabel_);

        resizer_ = new SubView();
        resizer_->setCursor(Cursor(CursorKind::SizeNS));
        addChild(resizer_);
        updateResizer();
        updateLook();
        resizer_->onMouseDown += [this, r = resizer_](View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
            if ((buttons & mbmLeftButton) == 0) {
                return SyncReturn(SyncReturn::Ignored);
            }
            pressResize(r->localToScreen(pt));
            return SyncReturn(SyncReturn::Handled);
        };
        resizer_->onMouseMove += [this, r = resizer_](View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
            moveResize(r->localToScreen(pt), (buttons & mbmLeftButton) != 0);
            return SyncReturn(SyncReturn::Ignored);
        };
        resizer_->onMouseUp += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
            releaseResize();
            return SyncReturn(SyncReturn::Ignored);
        };

        onSizeChanged += [this](View&, const Size&) {
            layoutContent();
            return SyncReturn::Ignored;
        };

        for (View* view : {static_cast<View*>(this), static_cast<View*>(box_), static_cast<View*>(body_)}) {
            view->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                onActivateRequested(*this);
                return SyncReturn(SyncReturn::Ignored);
            };
        }

        setMovable(true);
        setBounds(Rect(0.0f, 0.0f, 268.0f, desiredHeight()));
        updateSize();
    }

    void Panel::bindDragSource(View& view) {
        view.onMouseDown += [this, &view](View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
            if ((buttons & mbmLeftButton) == 0) {
                return SyncReturn(SyncReturn::Ignored);
            }
            pressTitle(view.localToScreen(pt));
            return SyncReturn(SyncReturn::Handled);
        };
        view.onMouseMove += [this, &view](View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
            moveTitle(view.localToScreen(pt), (buttons & mbmLeftButton) != 0);
            return SyncReturn(SyncReturn::Ignored);
        };
        view.onMouseUp += [this, &view](View&, const Point& pt, std::uint32_t, std::uint32_t) {
            releaseTitle(view.localToScreen(pt));
            return SyncReturn(SyncReturn::Ignored);
        };
    }

    void Panel::setTitle(const std::string& title) {
        title_ = title;
        titleLabel_->setText(title_);
    }

    void Panel::setCollapsible(bool collapsible) {
        collapsible_ = collapsible;
        caret_->setEnabled(collapsible_);
        setCollapsed(false);
    }

    void Panel::setActive(bool active) {
        if (active_ != active) {
            active_ = active;
            updateLook();
        }
    }

    void Panel::updateLook() {
        // A third of the way from the normal border to the accent: noticeable, not loud.
        const std::uint32_t accent = Theme::current().accent;
        const auto mix = [&](int shift) {
            const std::uint32_t base = (0x3au), a = (accent >> shift) & 0xFFu;
            return base + (a - base) / 3;
        };
        const std::uint32_t outline = 0xff000000u | (mix(16) << 16) | (mix(8) << 8) | mix(0);
        box_->style().setBorderFill(Color(active_ ? outline : 0xff3a3a3au));
        resizer_->style().setBackgroundColor(BLRgba32(resizing_ ? Theme::argb(accent, 90) : 0xff2e2e2eu));
        style().markDirty();
    }

    void Panel::setResizable(bool resizable) {
        resizable_ = resizable;
        resizing_ = false;
        updateResizer();
        updateLook();
    }

    void Panel::updateResizer() {
        resizer_->setVisible(resizable_ && !collapsed_);
    }

    void Panel::pressResize(const Point& screen) {
        onActivateRequested(*this);
        resizing_ = true;
        updateLook();
        resizeStartY_ = screen.y;
        resizeStartHeight_ = bodyHeight_;
    }

    void Panel::moveResize(const Point& screen, bool buttonDown) {
        if (!resizing_) {
            return;
        }
        if (!buttonDown) {
            releaseResize();
            return;
        }
        setBodyHeight((std::max)(kMinBodyHeight, resizeStartHeight_ + screen.y - resizeStartY_));
        onBodyHeightChanged(*this);
    }

    void Panel::releaseResize() {
        const bool was = resizing_;
        resizing_ = false;
        updateLook();
        if (was) {
            onResizeFinished(*this);
        }
    }

    void Panel::setCollapsed(bool collapsed) {
        if (collapsed_ == collapsed || (collapsed && !collapsible_)) {
            return;
        }
        collapsed_ = collapsed;
        updateResizer();
        body_->setVisible(!collapsed_);
        caret_->setIcon(collapsed_ ? kCaretClosed : kCaretOpen);
        updateSize();
        onCollapsedChanged(*this);
    }

    void Panel::setMovable(bool movable) {
        movable_ = movable;
        grip_->setVisible(movable_);
        if (!movable_) {
            pressed_ = false;
            dragging_ = false;
        }
    }

    void Panel::setBodyHeight(float height) {
        bodyHeight_ = (std::max)(0.0f, height);
        updateSize();
    }

    float Panel::desiredHeight() const {
        return 2.0f * kBorder + kTitleHeight + (collapsed_ ? 0.0f : bodyHeight_);
    }

    void Panel::updateSize() {
        setDesiredSize(Size(0.0f, desiredHeight()));
        if (View* owner = parent()) {
            owner->updateLayout();
        }
        style().markDirty();
    }

    void Panel::layoutContent() {
        const Rect own = bounds();
        content_->setBounds(Rect(0.0f, 0.0f, own.width(), own.height()));
        box_->setBounds(Rect(0.0f, 0.0f, own.width(), own.height()));
        resizer_->setBounds(Rect(kBorder, own.height() - kBorder - kResizeHeight, (std::max)(0.0f, own.width() - 2.0f * kBorder), kResizeHeight));
    }

    void Panel::pressTitle(const Point& screen) {
        if (!movable_) {
            return;
        }
        onActivateRequested(*this);
        pressed_ = true;
        dragging_ = false;
        pressPoint_ = screen;
    }

    void Panel::moveTitle(const Point& screen, bool buttonDown) {
        if (!pressed_) {
            return;
        }
        if (!buttonDown) {   // the release happened where nobody told us
            releaseTitle(screen);
            return;
        }
        if (!dragging_) {
            if (std::hypot(screen.x - pressPoint_.x, screen.y - pressPoint_.y) < kDragThreshold) {
                return;
            }
            dragging_ = true;
            onTitleDragStarted(*this, screen);
        }
        onTitleDragged(*this, screen);
    }

    void Panel::releaseTitle(const Point& screen) {
        const bool was = dragging_;
        pressed_ = false;
        dragging_ = false;
        if (was) {
            onTitleDragEnded(*this, screen);
        }
    }

}
