#pragma once

#include <newui/delegate.h>
#include <newui/subview.h>

namespace curvy {

    // A hue ring with a saturation/brightness square inside (docs/ui-design.md, "Colour, gradient and stroke editing"). Press and drag on the ring to
    // turn the hue, in the square to set saturation (left to right) and brightness (bottom to top); a drag keeps going when the pointer leaves
    // the part it started on. All three values are 0..1. A pen works the same as a mouse: there is no hover state.
    //
    // The view only edits the numbers and tells its owner; it knows nothing of documents or colour spaces (StylePanel turns the HSB it
    // reports into the colour being edited).
    // @reflect ignore=true
    class ColorWheel : public newui::SubView {
    public:
        ColorWheel();

        double hue() const { return hue_; }
        double saturation() const { return saturation_; }
        double brightness() const { return brightness_; }
        // Shows a colour without telling anyone. A hue of a grey or black has no meaning, so one the caller flags as undefined keeps the hue shown.
        void setHsb(double hue, double saturation, double brightness, bool hueDefined = true);
        bool isDragging() const { return drag_ != Drag::None; }

        // Fired on every change the user makes, between onDragStarted and onDragFinished.
        newui::Delegate<ColorWheel> onChanged;
        newui::Delegate<ColorWheel> onDragStarted;
        newui::Delegate<ColorWheel> onDragFinished;

        void paint(BLContext& ctx) override;

    private:
        enum class Drag { None, Hue, Square };
        struct Geometry {
            double cx, cy, outer, inner;   // ring radii around the centre
            double left, top, side;        // the square
        };
        bool geometry(Geometry& out) const;
        void updateFrom(const newui::Point& pt);
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);

        double hue_ = 0.0;
        double saturation_ = 0.0;
        double brightness_ = 1.0;
        Drag drag_ = Drag::None;
    };

}
