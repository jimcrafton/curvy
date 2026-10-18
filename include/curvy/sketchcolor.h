#pragma once

#include <blend2d/blend2d.h>

#include <cstdint>
#include <string>

namespace curvy {

    enum class ColorSpace { RGB, CMYK, Gray, HSB };

    // A color as the user authored it: the components stay in their own space (so a CMYK swatch
    // is still CMYK after a save/load round trip and in a PDF), and toRgba32() converts for display.
    // Components are all 0..1 (HSB: hue is 0..1 of a full turn). CMYK -> RGB is the naive
    // (1-c)(1-k) formula - no ICC profiles, a documented limit (PLAN.md risk 4).
    class SketchColor {
    public:
        SketchColor() = default;

        static SketchColor rgb(float r, float g, float b, float alpha = 1.0f);
        static SketchColor cmyk(float c, float m, float y, float k, float alpha = 1.0f);
        static SketchColor gray(float g, float alpha = 1.0f);
        static SketchColor hsb(float h, float s, float b, float alpha = 1.0f);

        // "#rrggbb" or "#rrggbbaa" (leading '#' optional); throws std::runtime_error when malformed.
        static SketchColor fromHex(const std::string& hex);

        ColorSpace space() const { return space_; }
        // Component i (0..3) in space(); unused components are 0.
        float component(std::size_t index) const;
        float alpha() const { return alpha_; }
        void setAlpha(float alpha);

        // The same color expressed in `space` (alpha kept). Conversions go through RGB with the naive CMYK
        // formula (no ICC profiles), so RGB -> CMYK -> RGB is exact but CMYK -> RGB -> CMYK normalizes
        // to full black generation (the K a CMYK color had is not remembered). Gray is luminance
        // (0.299/0.587/0.114); converting gray to anything else keeps it neutral.
        SketchColor convertedTo(ColorSpace space) const;
        // The color's red/green/blue, 0..1.
        void toRgb(float& r, float& g, float& b) const;

        // Display color, alpha included.
        BLRgba32 toRgba32() const;
        // Display color as "#rrggbb" (alpha excluded - SVG carries it as fill-opacity).
        std::string toHex() const;

        bool operator==(const SketchColor& other) const;
        bool operator!=(const SketchColor& other) const { return !(*this == other); }

    private:
        ColorSpace space_ = ColorSpace::RGB;
        float c_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float alpha_ = 1.0f;
    };

}
