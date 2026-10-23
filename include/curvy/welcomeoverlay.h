#pragma once

#include <newui/overlay.h>
#include <newui/shapes.h>

#include <cstdint>
#include <string>
#include <vector>

namespace curvy {

    // The first-run card (docs/ui-design.md "First run"): a dimmed window with a centred card holding a keyboard drawn from vector shapes
    // (newui::shapes) whose Space bar glows, and a few lines on how Space opens the ring menu. Colours come from Theme::current() when it is built.
    class WelcomeOverlay : public newui::Overlay {
    public:
        WelcomeOverlay();

        void viewSized(const newui::Rect& bounds) override;

    private:
        struct Key {
            newui::shapes::RoundRect* lip = nullptr;
            newui::shapes::RoundRect* face = nullptr;
            newui::shapes::Text* label = nullptr;
            float row = 0.0f, column = 0.0f, span = 1.0f;   // in key pitches, column counted from the keyboard's left edge
        };

        newui::shapes::Text* addText(const std::string& text, float size, bool bold, std::uint32_t rgb);
        void addKey(const std::string& label, float row, float column, float span, bool space);
        void place(newui::shapes::Text& text, float x, float y, bool centred, float boxWidth = 0.0f) const;

        newui::shapes::RoundRect* card_ = nullptr;
        newui::shapes::RoundRect* plate_ = nullptr;
        newui::shapes::Text* title_ = nullptr;
        newui::shapes::Text* lead_ = nullptr;
        newui::shapes::Text* note_ = nullptr;
        newui::shapes::RoundRect* ring_ = nullptr;   // amber focus ring round the Space bar
        std::vector<Key> keys_;
        std::vector<newui::shapes::Text*> lines_;
    };

}
