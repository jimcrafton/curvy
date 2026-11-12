#pragma once

#include "curvy/colorwheel.h"
#include "curvy/swatchstrip.h"

#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/segmentedcontrol.h>
#include <newui/subview.h>

#include <array>
#include <cstddef>
#include <string>

namespace curvy {

    // The floating card the graph panel's colour swatch opens: the hue/brightness wheel, RGB / HSB / CMYK / Gray buttons that pick what the
    // component sliders mean, an alpha slider (only for a colour that has an alpha), a hex field and, when the owner asks, the palettes and swatches.
    // Every colour control is a view of one colour kept as red, green, blue and alpha in 0..1; the card knows nothing of documents.
    //
    // An edit made with the mouse or the hex field is announced as onDragStarted, onChanged (as often as it moves), onDragFinished, so the owner can apply it live
    // and make the whole gesture one undo step. setColor shows a colour without announcing anything.
    // @reflect ignore=true
    class ColorPickerCard : public newui::SubView {
    public:
        enum class Mode { Rgb, Hsb, Cmyk, Gray };

        ColorPickerCard();

        void setColor(double red, double green, double blue, double alpha, bool hasAlpha);
        double red() const { return r_; }
        double green() const { return g_; }
        double blue() const { return b_; }
        double alpha() const { return a_; }
        bool hasAlpha() const { return hasAlpha_; }

        Mode mode() const { return mode_; }
        void setMode(Mode mode);

        // The #RRGGBB (#RRGGBBAA with an alpha) field at the bottom. Shown by default; an owner that already shows the hex elsewhere (the graph
        // panel's colour row) hides it. Hiding changes only the layout: hexText and setHex keep working.
        bool hexVisible() const { return hexVisible_; }
        void setHexVisible(bool visible);
        std::string hexText() const;
        // Types into the hex field and applies it as one gesture, as Enter does; false (and the field is put back) when the text is not a colour.
        // Six digits leave the alpha alone.
        bool setHex(const std::string& text);

        // Shows `red, green, blue` (and `alpha` when the colour has one) as a gesture of its own: onDragStarted, onChanged, onDragFinished. What a
        // swatch click does.
        void applyColor(double red, double green, double blue, double alpha);

        // The palettes and swatches under the sliders (SwatchStrip). Hidden by default; the owner shows them and connects the strip to a document
        // (SwatchPaletteController). The card gets taller or shorter with it.
        bool swatchesVisible() const { return swatchesVisible_; }
        void setSwatchesVisible(bool visible);
        SwatchStrip& swatchStrip() { return *strip_; }

        // What is on show, for tests.
        ColorWheel& wheel() { return *wheel_; }
        std::size_t sliderCount() const;
        newui::Slider& slider(std::size_t index) { return *rows_[index].slider; }
        newui::Slider& alphaSlider() { return *alphaRow_.slider; }

        newui::Delegate<ColorPickerCard> onDragStarted;
        newui::Delegate<ColorPickerCard> onChanged;
        newui::Delegate<ColorPickerCard> onDragFinished;

    private:
        struct Row {
            newui::Label* caption = nullptr;
            newui::Slider* slider = nullptr;
            newui::Label* value = nullptr;
        };

        Row makeRow();
        void showRow(const Row& row, bool shown, float y, const char* caption, float low, float high);
        void layoutCard();
        void refreshAll();
        void refreshHex();
        void slidersFromColor();
        void colorFromSliders();
        void sliderMoved();
        void gestureEnded();
        void beginGesture();

        double r_ = 0.0, g_ = 0.0, b_ = 0.0, a_ = 1.0;
        double hue_ = 0.0;   // kept apart from the colour: a grey has no hue, and the wheel and the H slider should not jump to red
        bool hasAlpha_ = false;
        Mode mode_ = Mode::Rgb;
        bool updating_ = false;
        bool gesture_ = false;
        bool hexVisible_ = true;
        bool swatchesVisible_ = false;

        SwatchStrip* strip_ = nullptr;
        ColorWheel* wheel_ = nullptr;
        newui::SegmentedControl* modes_ = nullptr;
        std::array<Row, 4> rows_;
        Row alphaRow_;
        newui::TextField* hex_ = nullptr;
    };

}
