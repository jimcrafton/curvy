#include "curvy/timelinebar.h"

#include "curvy/theme.h"

#include <newui/application.h>
#include <newui/bundle.h>
#include <newui/utils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kBarHeight = 36.0f;

        template <typename T>
        T* find(View& root, const char* name) {
            return dynamic_cast<T*>(root.findView(name));
        }

        Color tint(std::uint32_t rgb, std::uint32_t alpha = 255) { return Color(Theme::argb(rgb, alpha), true); }

        std::string rateText(double fps) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g fps", fps);
            return buffer;
        }
    }

    TimelineBar::TimelineBar(Frame& frame, CanvasView& canvas, WorkspaceModel& model, SketchDocument& document)
        : frame_(frame), canvas_(canvas), model_(model), document_(document) {
        bar_ = dynamic_cast<SubView*>(Bundle::instance().loadView("timelineBar"));
        if (bar_ == nullptr) {
            return;
        }
        bar_->setVisible(false);
        canvas_.addChild(bar_);
        play_ = find<Button>(*bar_, "timelinePlay");
        field_ = find<TextField>(*bar_, "timelineFrame");
        scrub_ = find<Slider>(*bar_, "timelineScrub");
        end_ = find<Label>(*bar_, "timelineEnd");
        rate_ = find<Label>(*bar_, "timelineFps");

        auto bind = [this](const char* name, std::function<void()> action) {
            if (auto* button = find<Button>(*bar_, name)) {
                connections_.add(button->onClick, [action](Control&) {
                    action();
                    return SyncReturn::Handled;
                });
            }
        };
        bind("timelineFirst", [this] { setFrame(0); });
        bind("timelinePrev", [this] { step(-1); });
        bind("timelinePlay", [this] { toggle(); });
        bind("timelineNext", [this] { step(1); });
        bind("timelineLast", [this] { setFrame(lastFrame_); });
        if (scrub_ != nullptr) {
            scrub_->setInteger(true);
            connections_.add(scrub_->onValueChanged, [this](Slider&) {
                if (!updating_) {
                    setFrame(int(std::lround(scrub_->value())));
                }
                return SyncReturn::Handled;
            });
        }
        if (field_ != nullptr) {
            connections_.add(field_->onReturnPressed, [this](TextField&) {
                readControls();
                return SyncReturn::Handled;
            });
            connections_.add(field_->onLostFocus, [this](View&) {
                readControls();
                return SyncReturn::Handled;
            });
        }
        connections_.add(model_.onChanged, [this](Model&) {
            sync();
            return SyncReturn::Handled;
        });
        connections_.add(canvas_.onSizeChanged, [this](View&, const Size&) {
            layout();
            return SyncReturn::Handled;
        });
        setLastFrame(lastFrame_);
        restyle();
        sync();
    }

    TimelineBar::~TimelineBar() = default;

    int TimelineBar::frame() const {
        return int(std::lround(document_.time() * fps()));
    }

    void TimelineBar::setLastFrame(int frame) {
        lastFrame_ = std::max(1, frame);
        if (scrub_ != nullptr) {
            scrub_->setRange(0.0f, float(lastFrame_));
        }
        writeControls();
    }

    void TimelineBar::setFrame(int frame) {
        document_.setTime(double(std::clamp(frame, 0, lastFrame_)) / fps());
        writeControls();
    }

    void TimelineBar::play() {
        if (isPlaying() || bar_ == nullptr) {
            return;
        }
        if (frame() >= lastFrame_) {
            setFrame(0);
        }
        lastTick_ = std::chrono::steady_clock::now();
        timer_ = Application::instance().runLoop().postDelayed(std::chrono::milliseconds(16), [this] {
            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - lastTick_).count();
            lastTick_ = now;
            advance(seconds);
            return false;   // keep going until stop()
        });
        writeControls();
    }

    void TimelineBar::stop() {
        if (!isPlaying()) {
            return;
        }
        Application::instance().runLoop().cancelDelayed(timer_);
        timer_ = RunLoop::kInvalidTimerHandle;
        writeControls();
    }

    void TimelineBar::advance(double seconds) {
        const double length = double(lastFrame_) / fps();
        double time = document_.time() + seconds;
        if (time > length) {
            time = std::fmod(time, length);
        }
        document_.setTime(time);
        writeControls();
    }

    void TimelineBar::readControls() {
        if (updating_ || field_ == nullptr) {
            return;
        }
        const std::string text = wideToUtf8(field_->text());
        char* end = nullptr;
        const double number = std::strtod(text.c_str(), &end);
        if (end != text.c_str()) {
            setFrame(int(std::lround(number)));
        } else {
            writeControls();
        }
    }

    void TimelineBar::writeControls() {
        if (bar_ == nullptr) {
            return;
        }
        updating_ = true;
        if (play_ != nullptr) {
            play_->setText(isPlaying() ? "Stop" : "Play");
        }
        if (field_ != nullptr) {
            const std::wstring text = utf8ToWide(std::to_string(frame()));
            if (field_->text() != text) {
                field_->setText(text);
            }
        }
        if (scrub_ != nullptr && int(std::lround(scrub_->value())) != frame()) {
            scrub_->setValue(float(frame()));
        }
        if (end_ != nullptr) {
            end_->setText("/ " + std::to_string(lastFrame_));
        }
        if (rate_ != nullptr) {
            rate_->setText(rateText(fps()));
        }
        updating_ = false;
    }

    void TimelineBar::layout() {
        if (bar_ == nullptr) {
            return;
        }
        const Rect client = canvas_.getClientBounds();
        const float left = canvas_.leftInset();
        bar_->setBounds(Rect(left, std::max(0.0f, client.height() - kBarHeight), std::max(0.0f, client.width() - left), kBarHeight));
        if (View* box = bar_->findView("timelineBarBox")) {
            box->setBounds(Rect(0.0f, 0.0f, bar_->bounds().size().width, kBarHeight));
        }
        canvas_.style().markDirty();
    }

    void TimelineBar::sync() {
        if (bar_ == nullptr) {
            return;
        }
        const bool wanted = model_.timelineVisible();
        if (wanted != bar_->isVisible()) {
            bar_->setVisible(wanted);
            if (!wanted) {
                stop();
            } else {
                writeControls();
            }
            layout();
        }
    }

    void TimelineBar::restyle() {
        if (bar_ == nullptr) {
            return;
        }
        const Theme& theme = Theme::current();
        if (View* box = bar_->findView("timelineBarBox")) {
            box->style().setBackgroundColor(tint(theme.bgSurface, 0x99));
        }
        for (const char* name : {"timelineFrameCaption", "timelineEnd", "timelineFps"}) {
            if (auto* label = find<Label>(*bar_, name)) {
                label->setTextColor(BLRgba32(Theme::argb(theme.textMuted)));
            }
        }
        if (field_ != nullptr) {
            field_->style().setBackgroundColor(tint(theme.bgApp));
            field_->setTextColor(BLRgba32(Theme::argb(theme.textPrimary)));
        }
    }

}
