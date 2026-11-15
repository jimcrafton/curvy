#include "curvy/paneldock.h"

#include "curvy/theme.h"

#include <algorithm>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    PanelDock::PanelDock(DockLayout& layout, DockSide side) : layout_(layout), side_(side) {
        setVisible(true);
        dropLine_ = new SubView();
        dropLine_->setVisible(false);
        dropLine_->style().setBackgroundColor(BLRgba32(Theme::argb(Theme::current().accent)));
        addChild(dropLine_);
        outline_ = new SubView();
        outline_->setVisible(false);
        outline_->style().setBorderWidth(2.0f);
        outline_->style().setBorderFill(Color(Theme::argb(Theme::current().accent)));
        addChild(outline_);
        hint_ = new SubView();
        hint_->setVisible(false);
        hint_->style().setBackgroundColor(BLRgba32(Theme::argb(Theme::current().accent, 150)));
        addChild(hint_);
        onMouseWheel += [this](View&, const Point&, float delta) {
            return scrollBy(delta / 120.0f) ? SyncReturn::Handled : SyncReturn::Ignored;
        };
        onSizeChanged += [this](View&, const Size&) {
            layoutPanels();
            return SyncReturn::Ignored;
        };
    }

    PanelDock::~PanelDock() {
        if (hintTimer_ != RunLoop::kInvalidTimerHandle) {
            RunLoop::current().cancelDelayed(hintTimer_);
        }
    }

    bool PanelDock::scrollBy(float notches) {
        const float most = (std::max)(0.0f, contentHeight_ - bounds().height());
        const float before = scroll_;
        scroll_ = (std::max)(0.0f, (std::min)(most, scroll_ - notches * kWheelStep));
        if (most <= 0.0f) {
            return false;
        }
        layoutPanels();
        updateHint();
        return scroll_ != before || notches != 0.0f;
    }

    void PanelDock::updateHint() {
        const float height = bounds().height();
        if (contentHeight_ <= height || height <= 0.0f) {
            hint_->setVisible(false);
            return;
        }
        const float length = (std::max)(16.0f, height * height / contentHeight_);
        const float top = (height - length) * scroll_ / (contentHeight_ - height);
        hint_->setBounds(Rect(bounds().width() - kHintWidth - 1.0f, top, kHintWidth, length));
        hint_->setVisible(true);
        RunLoop& loop = RunLoop::current();
        if (hintTimer_ != RunLoop::kInvalidTimerHandle) {
            loop.cancelDelayed(hintTimer_);
        }
        try {
            hintTimer_ = loop.postDelayed(std::chrono::milliseconds(kHintMs), [this]() {
                hintTimer_ = RunLoop::kInvalidTimerHandle;
                hint_->setVisible(false);
                style().markDirty();
                return true;
            });
        } catch (const std::exception&) {
            // postDelayed throws before the loop runs (a window under test): the hint then simply stays until the next scroll
        }
    }

    Panel* PanelDock::panel(const std::string& id) const {
        const auto found = panels_.find(id);
        return found == panels_.end() ? nullptr : found->second;
    }

    void PanelDock::addPanel(Panel* panel) {
        if (layout_.find(panel->id()) == nullptr) {
            const std::string id = panel->id();
            delete panel;
            throw std::runtime_error("PanelDock: the layout has no panel \"" + id + "\"");
        }
        panels_[panel->id()] = panel;
        addChild(panel);
        reorderChild(outline_, ~std::size_t(0));   // clamped to the end: the outline and the line draw over the panels
        reorderChild(dropLine_, ~std::size_t(0));
        reorderChild(hint_, ~std::size_t(0));

        panel->onCollapsedChanged += [this](Panel& p) {
            layout_.setCollapsed(p.id(), p.collapsed());
            return SyncReturn::Handled;
        };
        panel->onActivateRequested += [this](Panel& p) {
            layout_.setActive(p.id());
            return SyncReturn::Handled;
        };
        panel->onResizeFinished += [this](Panel& p) {
            onPanelResized(*this, p);
            return SyncReturn::Handled;
        };
        panel->onBodyHeightChanged += [this](Panel&) {
            layoutPanels();
            return SyncReturn::Handled;
        };
        panel->onCloseRequested +=[this](Panel& p) {
            const PanelInfo* info = layout_.find(p.id());
            if (info != nullptr && info->floating) {
                layout_.dock(p.id());
            } else {
                layout_.setUserHidden(p.id(), true);
            }
            return SyncReturn::Handled;
        };
        panel->onMenuRequested += [this](Panel& p, const Point& at) {
            onPanelMenu(*this, p, at);
            return SyncReturn::Handled;
        };
        panel->onTitleDragged += [this](Panel& p, const Point& screen) {
            if (wouldTearOff(p, screen)) {
                dropLine_->setVisible(false);
                return SyncReturn::Handled;
            }
            showDropLine(p, screen);
            return SyncReturn::Handled;
        };
        panel->onTitleDragEnded += [this](Panel& p, const Point& screen) {
            dropLine_->setVisible(false);
            if (wouldTearOff(p, screen)) {
                // The window opens with its caption under the pointer.
                layout_.tearOff(p.id(), screen.x - kTearOffGrabX, screen.y - kTearOffGrabY);
            } else {
                layout_.move(p.id(), dropIndexAt(p, screen));
            }
            return SyncReturn::Handled;
        };
        sync();
    }

    void PanelDock::showDropLine(const Panel& dragged, const Point& screen) {
        const float y = (std::max)(0.0f, (std::min)(bounds().height(), screenToLocal(screen).y));
        std::vector<float> middles;
        float top = kSpacing - scroll_;
        for (const std::string& id : layout_.docked(side_)) {
            Panel* other = this->panel(id);
            if (other == nullptr || other == &dragged) {
                continue;
            }
            const float h = other->desiredHeight();
            middles.push_back(top + h * 0.5f);
            top += h + kSpacing;
        }
        const std::size_t index = DockLayout::dropIndex(middles, y);
        float lineY = kSpacing * 0.5f - scroll_;
        std::size_t i = 0;
        for (const std::string& id : layout_.docked(side_)) {
            Panel* other = this->panel(id);
            if (other == nullptr || other == &dragged) {
                continue;
            }
            if (i++ == index) {
                break;
            }
            lineY = other->bounds().top() + other->bounds().height() + kSpacing * 0.5f;
        }
        dropLine_->setBounds(Rect(0.0f, lineY - kDropLineHeight * 0.5f, bounds().width(), kDropLineHeight));
        dropLine_->setVisible(true);
    }

    bool PanelDock::containsScreen(const Point& screen) const {
        if (!isVisible() || bounds().width() <= 0.0f || bounds().height() <= 0.0f) {
            return false;
        }
        const Point local = screenToLocal(screen);
        return local.x >= 0.0f && local.y >= 0.0f && local.x <= bounds().width() && local.y <= bounds().height();
    }

    void PanelDock::showDropTarget(const Panel& floating, const Point& screen) {
        if (!containsScreen(screen)) {
            hideDropTarget();
            return;
        }
        outline_->setBounds(Rect(0.0f, 0.0f, bounds().width(), bounds().height()));
        outline_->setVisible(true);
        showDropLine(floating, screen);
    }

    void PanelDock::hideDropTarget() {
        outline_->setVisible(false);
        dropLine_->setVisible(false);
    }

    bool PanelDock::wouldTearOff(const Panel& dragged, const Point& screen) const {
        const Point origin = localToScreen(Point(0.0f, 0.0f));
        DockBox box;
        box.left = origin.x;
        box.top = origin.y;
        box.right = origin.x + bounds().width();
        box.bottom = origin.y + bounds().height();
        return layout_.shouldTearOff(dragged.id(), box, screen.x, screen.y);
    }

    std::size_t PanelDock::dropIndexAt(const Panel& dragged, const Point& screen) const {
        const float y = screenToLocal(screen).y;
        std::vector<float> middles;
        for (const std::string& id : layout_.docked(side_)) {
            const Panel* other = panel(id);
            if (other == nullptr || other == &dragged) {
                continue;
            }
            const Rect r = other->bounds();
            middles.push_back(r.top() + r.height() * 0.5f);
        }
        return DockLayout::dropIndex(middles, y);
    }

    void PanelDock::sync() {
        layoutPanels();
    }

    void PanelDock::layoutPanels() {
        for (auto& entry : panels_) {
            entry.second->setActive(entry.first == layout_.activeId());
        }
        const std::vector<std::string> order = layout_.docked(side_);
        for (auto& entry : panels_) {
            if (entry.second->parent() == this) {   // a floating panel is in its frame, not ours
                entry.second->setVisible(std::find(order.begin(), order.end(), entry.first) != order.end());
            }
        }
        float stack = kSpacing;
        for (const std::string& id : order) {
            const Panel* p = panel(id);
            if (p != nullptr && p->parent() == this) {
                stack += p->desiredHeight() + kSpacing;
            }
        }
        contentHeight_ = stack;
        scroll_ = (std::max)(0.0f, (std::min)(scroll_, contentHeight_ - bounds().height()));   // the dock grew or a panel shrank
        float top = kSpacing - scroll_;
        const float width = bounds().width();
        for (const std::string& id : order) {
            Panel* p = panel(id);
            if (p == nullptr || p->parent() != this) {
                continue;
            }
            const float height = p->desiredHeight();
            p->setBounds(Rect(0.0f, top, width, height));
            top += height + kSpacing;
        }
    }

}
