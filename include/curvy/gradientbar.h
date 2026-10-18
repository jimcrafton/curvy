#pragma once

#include <newui/color.h>
#include <newui/delegate.h>
#include <newui/subview.h>

#include <cstddef>
#include <string>
#include <vector>

namespace curvy {

    // The stop bar of the gradient editor (the "GradientEditor" region of the mockup's Color
    // panel): a horizontal gradient preview with one draggable marker per stop beneath it.
    //
    // Stops are plain "offset color" strings (offset 0..1, then any newui::Color::fromString() text) so a
    // .newui file can set them. Click a marker to select it, drag it to move it, click on the bar to add a
    // stop there; the program reads and writes them as numbers through parsedStops()/setStops().
    // @reflect category=basic
    class GradientBar : public newui::SubView {
    public:
        // @reflect ignore=true
        struct Stop {
            double offset = 0.0;
            newui::Color color;
        };

        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        GradientBar();
        virtual ~GradientBar() {}

        // Each entry is "<offset> <color>", e.g. "0.55 #e8452cff".
        // @reflect collection add=addStop remove=removeStop
        const std::vector<std::string>& stops() const { return stops_; }
        void addStop(const std::string& stop);
        void removeStop(const std::string& stop);

        // The same stops as numbers, sorted by offset. A programmatic setStops() does not fire onStopsEdited.
        // @reflect ignore=true
        std::vector<Stop> parsedStops() const;
        // @reflect ignore=true
        void setStops(const std::vector<Stop>& stops);

        // Index into stops() of the stop being edited; stops().size() (the default) means none.
        std::size_t selectedStop() const { return selectedStop_; }
        void setSelectedStop(std::size_t index);

        // Fired when the user adds a stop or drags one (the stops changed), and when a different stop is selected.
        newui::Delegate<GradientBar> onStopsEdited;
        newui::Delegate<GradientBar> onStopSelected;

        void paint(BLContext& ctx) override;

    private:
        static constexpr float kMarkerSize = 10.0f;

        struct Metrics {
            double barLeft, barTop, barWidth, barHeight, markerTop;
        };
        bool metrics(Metrics& out) const;

        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);

        std::vector<std::string> stops_;
        std::size_t selectedStop_ = 0;
        bool dragging_ = false;
    };

}
