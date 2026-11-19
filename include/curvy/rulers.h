#pragma once

#include "curvy/units.h"

#include <vector>

namespace curvy {

    // The arithmetic of the rulers: where the ticks fall for a zoom and a unit. No drawing and no state, so tests and scripts reach it too.
    // Positions are points from the page's top left corner (the ruler origin).
    // @reflect ignore=true
    class Rulers {
    public:
        // Width of the strip along the top and left of the canvas, in screen pixels.
        static constexpr float kThickness = 18.0f;

        enum class Region { None, Top, Left, Corner };

        struct Tick {
            double points = 0.0;   // where it lies
            double value = 0.0;    // what it reads in the document's unit
            bool major = false;    // major ticks are longer and carry the label
        };

        // The distance between labelled ticks, in the unit: 1, 2 or 5 times a power of ten, the smallest that is at least `minPixels` apart on screen.
        // `pixelsPerUnit` is how long one unit is on screen (the zoom times the unit's points).
        static double majorStep(double pixelsPerUnit, double minPixels = 64.0);
        // How many minor steps make one major step: 10 for 1x10^n, 4 for 2x10^n, 5 for 5x10^n.
        static int subdivisions(double majorStep);

        // The ticks between two positions (points, `from` <= `to`), in order. Minor ticks closer than `minMinorPixels` on screen are left out. The ticks
        // are counted from `originPoints` (where the ruler reads zero): `points` is still the place on the page, `value` is what the ruler reads there.
        static std::vector<Tick> ticks(double fromPoints, double toPoints, double zoom, LengthUnit unit, double ppi, double minMinorPixels = 5.0,
                                       double originPoints = 0.0);

        // Which strip a point (pixels from the canvas's top left) lies in, for a canvas of this size; None when the rulers are hidden.
        static Region regionAt(float x, float y, bool visible);
    };

}
