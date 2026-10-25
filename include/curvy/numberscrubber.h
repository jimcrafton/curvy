#pragma once

#include <newui/controls.h>
#include <newui/delegate.h>

#include <cstdint>
#include <memory>

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
    // caller's business, through the delegates below. attach() hangs a scrubber on a view and returns it (the view's handlers keep it alive);
    // a view that scrubs several captions itself (a painted grid of rows) owns one and calls press / move / release.
    // @reflect ignore=true
    class NumberScrubber {
    public:
        // The mouse went down: set `value` to the field's current value and return Handled to start the drag; Ignored when there is nothing to scrub.
        newui::Delegate<NumberScrubber, double&> onBegin;
        // The value changed (only when it moves to a new value). Apply it live, without an undo step.
        newui::Delegate<NumberScrubber, double> onChange;
        // The button was let go. `changed` says whether onChange fired at all; this is where the drag becomes one undo step.
        newui::Delegate<NumberScrubber, bool> onEnd;
        // Optional, asked at every move: a handler that sets `unit` and returns Handled replaces the unit given to attach (a field whose unit
        // depends on its row).
        newui::Delegate<NumberScrubber, double&> onUnit;

        bool active() const { return active_; }
        // False when nobody started the drag in onBegin.
        bool press(float x);
        void move(float x, std::uint32_t keys, double unit, double minimum, double maximum);
        void release();

        // Makes dragging `view` (a caption label) scrub, with `unit` as the plain step (see above) and the value kept within [minimum, maximum].
        static std::shared_ptr<NumberScrubber> attach(newui::View& view, double unit, double minimum, double maximum);

        // The step for the modifier keys held (newui::kmShift / kmCtrl bits): unit, unit / 10, unit / 100.
        static double stepFor(double unit, std::uint32_t keys);
        // `value` rounded to a multiple of `step` (and cleaned of binary noise: 0.30000000000000004 -> 0.3).
        static double roundToStep(double value, double step);

    private:
        bool active_ = false;
        bool changed_ = false;
        float lastX_ = 0.0f;
        double raw_ = 0.0;       // the value the mouse has led to, before snapping
        double shown_ = 0.0;     // the last value reported
    };

}
