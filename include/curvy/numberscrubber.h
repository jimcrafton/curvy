#pragma once

#include <newui/controls.h>

#include <cstdint>
#include <functional>

namespace curvy {

    // "Scrubbing": dragging sideways on the caption of a number field changes the number, the way Illustrator and most design tools do.
    // The caption's cursor becomes a left-right arrow; while the button is down every pixel moved changes the value by one STEP, and
    // the value is kept on a multiple of that step so the numbers stay clean:
    //
    //     plain drag        step = the field's unit            (1 point, 1 degree ...)
    //     Shift             step = a tenth of the unit         (fine)
    //     Ctrl + Shift      step = a hundredth of the unit     (finer)
    //
    // The keys can be pressed or let go in the middle of a drag; it carries on from where the value is.
    //
    // The scrubber only turns mouse movement into numbers. What the numbers do - and how a whole drag becomes one undo step - is the
    // caller's business, through three callbacks (see Callbacks). It lives as long as the label's handlers do.
    // @reflect ignore=true
    class NumberScrubber {
    public:
        struct Callbacks {
            // The mouse went down: report the field's current value; return false when there is nothing to scrub (the drag is then ignored).
            std::function<bool(double& value)> begin;
            // The value changed (called only when it moves to a new value). Apply it live, without an undo step.
            std::function<void(double value)> change;
            // The button was let go. `changed` says whether `change` was called at all; this is where the drag becomes one undo step.
            std::function<void(bool changed)> end;
            // When set, asked at every move for the plain step, instead of the `unit` given to attach (a field whose unit depends on its row).
            std::function<double()> unit;
        };

        // Makes dragging `label` scrub, with `unit` as the plain step (see above) and the value kept within [minimum, maximum].
        static void attach(newui::Label& label, double unit, double minimum, double maximum, Callbacks callbacks);

        // The step for the modifier keys held (newui::kmShift / kmCtrl bits): unit, unit / 10, unit / 100.
        static double stepFor(double unit, std::uint32_t keys);
        // `value` rounded to a multiple of `step` (and cleaned of binary noise: 0.30000000000000004 -> 0.3).
        static double roundToStep(double value, double step);
    };

}
