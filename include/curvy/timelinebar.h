#pragma once

#include "curvy/canvasview.h"
#include "curvy/sketchdocument.h"
#include "curvy/workspacemodel.h"

#include <newui/controls.h>
#include <newui/frame.h>
#include <newui/runloop.h>

#include <chrono>
#include <memory>

namespace curvy {

    // The transport strip along the bottom of the canvas (mockup docs/mockups/timelineBar.html): First, Previous, Play/Stop, Next, Last, the frame being
    // shown, a scrubber over 0..lastFrame() and the frame rate. It owns the playhead: the document's time is frame() / fps(), so every move calls
    // SketchDocument::setTime and the expressions that read `time` or `frame` follow. Play runs on a timer, by the wall clock, and loops.
    // @reflect ignore=true
    class TimelineBar {
    public:
        TimelineBar(newui::Frame& frame, CanvasView& canvas, WorkspaceModel& model, SketchDocument& document);
        ~TimelineBar();
        TimelineBar(const TimelineBar&) = delete;
        TimelineBar& operator=(const TimelineBar&) = delete;

        newui::SubView* bar() const { return bar_; }
        bool isShown() const { return bar_ != nullptr && bar_->isVisible(); }

        int frame() const;
        int lastFrame() const { return lastFrame_; }
        double fps() const { return document_.network().fps(); }
        // The last frame the scrubber and Play reach (at least 1).
        void setLastFrame(int frame);
        // Moves the playhead (clamped to 0..lastFrame()) and with it the document's time.
        void setFrame(int frame);
        void step(int frames) { setFrame(frame() + frames); }

        bool isPlaying() const { return timer_ != newui::RunLoop::kInvalidTimerHandle; }
        void play();
        void stop();
        void toggle() { isPlaying() ? stop() : play(); }
        // Plays `seconds` of the animation from where the playhead is, wrapping from the last frame to the first. The timer calls this with the time
        // that passed; the checks call it directly.
        void advance(double seconds);

        void restyle();
        void layout();
        // Shows or hides the strip with the model's timeline flag.
        void sync();

    private:
        void readControls();
        void writeControls();

        newui::Frame& frame_;
        CanvasView& canvas_;
        WorkspaceModel& model_;
        SketchDocument& document_;
        newui::SubView* bar_ = nullptr;
        newui::Button* play_ = nullptr;
        newui::TextField* field_ = nullptr;
        newui::Slider* scrub_ = nullptr;
        newui::Label* end_ = nullptr;
        newui::Label* rate_ = nullptr;
        int lastFrame_ = 48;
        bool updating_ = false;
        std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);   // the views and the model outlive this object and still call its handlers while closing
        newui::RunLoop::TimerHandle timer_ = newui::RunLoop::kInvalidTimerHandle;
        std::chrono::steady_clock::time_point lastTick_;
    };

}
