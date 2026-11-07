#pragma once

#include <newui/overlay.h>
#include <newui/shapes.h>

#include <string>

namespace curvy {

    // Shown over the whole window while a file is opened or saved on the worker thread: a dimmed window and a centred card with the
    // task's name, a ring that fills with the percentage done - or, while the worker cannot tell, an arc that turns - and the hint that
    // Esc cancels. update() is called on every tick of the controller's timer.
    class LoadingOverlay : public newui::Overlay {
    public:
        LoadingOverlay();

        void setTitle(const std::string& title);
        // 0..1 for a ring that fills, negative for the turning arc.
        void update(float fraction);
        // After the task was asked to stop and has not ended yet.
        void setCancelling();

        void viewSized(const newui::Rect& bounds) override;

    private:
        void layout();

        newui::shapes::RoundRect* card_ = nullptr;
        newui::shapes::Circle* track_ = nullptr;
        newui::shapes::Arc* arc_ = nullptr;
        newui::shapes::Text* title_ = nullptr;
        newui::shapes::Text* percent_ = nullptr;
        newui::shapes::Text* hint_ = nullptr;
        newui::Rect bounds_;
        float turn_ = 0.0f;   // radians the indeterminate arc has turned
        float fraction_ = -1.0f;
        bool cancelling_ = false;
    };

}
