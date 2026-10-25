#include "curvy/numberscrubber.h"

#include <newui/cursor.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace curvy {

    double NumberScrubber::stepFor(double unit, std::uint32_t keys) {
        const bool shift = (keys & newui::kmShift) != 0, ctrl = (keys & newui::kmCtrl) != 0;
        if (shift && ctrl) {
            return unit / 100.0;
        }
        if (shift) {
            return unit / 10.0;
        }
        return unit;
    }

    double NumberScrubber::roundToStep(double value, double step) {
        if (!(step > 0.0)) {
            return value;
        }
        double snapped = std::round(value / step) * step;
        return std::round(snapped * 1e6) / 1e6;
    }

    bool NumberScrubber::press(float x) {
        double value = 0.0;
        if (!onBegin.syncCallFirst(*this, value).handled()) {
            return false;
        }
        active_ = true;
        changed_ = false;
        lastX_ = x;
        raw_ = shown_ = value;
        return true;
    }

    void NumberScrubber::move(float x, std::uint32_t keys, double unit, double minimum, double maximum) {
        onUnit.syncCallFirst(*this, unit);
        double step = stepFor(unit, keys);
        raw_ += double(x - lastX_) * step;
        lastX_ = x;
        raw_ = std::max(minimum, std::min(maximum, raw_));
        double value = std::max(minimum, std::min(maximum, roundToStep(raw_, step)));
        if (value != shown_) {
            shown_ = value;
            changed_ = true;
            onChange(*this, value);
        }
    }

    void NumberScrubber::release() {
        active_ = false;
        onEnd(*this, changed_);
    }

    std::shared_ptr<NumberScrubber> NumberScrubber::attach(newui::View& view, double unit, double minimum, double maximum) {
        auto scrubber = std::make_shared<NumberScrubber>();
        view.setCursor(newui::Cursor(newui::CursorKind::SizeWE));

        view.onMouseDown += [scrubber](newui::View&, const newui::Point& at, std::uint32_t buttons, std::uint32_t) {
            if ((buttons & newui::mbmLeftButton) == 0 || !scrubber->press(at.x)) {
                return newui::SyncReturn::Ignored;
            }
            return newui::SyncReturn::Handled;
        };
        view.onMouseMove += [scrubber, unit, minimum, maximum](newui::View&, const newui::Point& at, std::uint32_t, std::uint32_t keys) {
            if (!scrubber->active()) {
                return newui::SyncReturn::Ignored;
            }
            scrubber->move(at.x, keys, unit, minimum, maximum);
            return newui::SyncReturn::Handled;
        };
        view.onMouseUp += [scrubber](newui::View&, const newui::Point&, std::uint32_t, std::uint32_t) {
            if (!scrubber->active()) {
                return newui::SyncReturn::Ignored;
            }
            scrubber->release();
            return newui::SyncReturn::Handled;
        };
        return scrubber;
    }

}
