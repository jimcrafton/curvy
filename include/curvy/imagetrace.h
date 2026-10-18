#pragma once

#include <blend2d/blend2d.h>

#include <cstdint>
#include <string>
#include <vector>

namespace curvy {

    // Image Trace: turns a raster picture into filled vector shapes. The colors of the picture are reduced to a small palette
    // (median cut), then for each palette color - largest area first - the outline of every pixel that is that color or a color after
    // it in the order is traced and fitted with curves. The layers are stacked in that order, so what lies below always reaches under
    // what is on top of it (no hairline gaps between neighbours), and the shapes need no holes for the colors they cover.
    // Pixels that are mostly transparent are left out.

    struct TraceOptions {
        int colors = 8;            // size of the palette, 2..32
        int maxSize = 400;         // the picture is first shrunk (by whole factors) until neither side is more than this many pixels
        double minArea = 6.0;      // outlines smaller than this many pixels (after shrinking) are dropped as specks
        double fitTolerance = 0.7; // how far (in pixels) the fitted curves may stray from the outline: less = more points, closer to the picture
        bool ignoreWhite = false;  // near-white pixels count as background and are left out
    };

    // One layer of the result: its color (0xRRGGBB) and its outlines, in pixel units of the (shrunk) picture, y down.
    struct TracedShape {
        std::uint32_t rgb = 0;
        BLPath path;
    };

    struct TraceResult {
        std::vector<TracedShape> shapes;   // bottom layer first
        int width = 0;                     // the size of the picture that was traced
        int height = 0;
    };

    // `pixels` are width * height 0xAARRGGBB values (straight, not premultiplied), row by row from the top.
    TraceResult traceRgba(const std::vector<std::uint32_t>& pixels, int width, int height, const TraceOptions& options = TraceOptions());

    // Reads a PNG, BMP or JPEG file as straight 0xAARRGGBB pixels. False when it cannot be read.
    bool loadImagePixels(const std::string& path, std::vector<std::uint32_t>& pixels, int& width, int& height);

}
