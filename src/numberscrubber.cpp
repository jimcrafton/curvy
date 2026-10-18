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

    void NumberScrubber::attach(newui::Label& label, double unit, double minimum, double maximum, Callbacks callbacks) {
        struct State {
            bool active = false;
            bool changed = false;
            float lastX = 0.0f;
            double raw = 0.0;       // the value the mouse has led to, before snapping
            double shown = 0.0;     // the last value reported
        };
        auto state = std::make_shared<State>();
        label.setCursor(newui::Cursor(newui::CursorKind::SizeWE));

        label.onMouseDown += [state, callbacks](newui::View&, const newui::Point& at, std::uint32_t buttons, std::uint32_t) {
            if ((buttons & newui::mbmLeftButton) == 0) {
                return newui::SyncReturn::Ignored;
            }
            double value = 0.0;
            if (!callbacks.begin || !callbacks.begin(value)) {
                return newui::SyncReturn::Ignored;
            }
            state->active = true;
            state->changed = false;
            state->lastX = at.x;
            state->raw = state->shown = value;
            return newui::SyncReturn::Handled;
        };
        label.onMouseMove += [state, callbacks, unit, minimum, maximum](newui::View&, const newui::Point& at, std::uint32_t, std::uint32_t keys) {
            if (!state->active) {
                return newui::SyncReturn::Ignored;
            }
            double step = NumberScrubber::stepFor(unit, keys);
            state->raw += double(at.x - state->lastX) * step;
            state->lastX = at.x;
            state->raw = std::max(minimum, std::min(maximum, state->raw));
            double value = std::max(minimum, std::min(maximum, NumberScrubber::roundToStep(state->raw, step)));
            if (value != state->shown) {
                state->shown = value;
                state->changed = true;
                if (callbacks.change) {
                    callbacks.change(value);
                }
            }
            return newui::SyncReturn::Handled;
        };
        label.onMouseUp += [state, callbacks](newui::View&, const newui::Point&, std::uint32_t, std::uint32_t) {
            if (!state->active) {
                return newui::SyncReturn::Ignored;
            }
            state->active = false;
            if (callbacks.end) {
                callbacks.end(state->changed);
            }
            return newui::SyncReturn::Handled;
        };
    }

}
