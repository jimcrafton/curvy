#pragma once

#include "curvy/swatchpalettes.h"

#include <cstdint>
#include <vector>

namespace curvy {

    // @reflect ignore=true
    struct PaletteExtractOptions {
        enum class Order { MostUsed, Hue, Lightness };

        int count = 8;                           // how many swatches to look for, 1..64
        bool mergeNearDuplicates = true;         // join swatches whose colours are closer than a small distance
        bool ignoreNearWhiteAndBlack = true;     // leave out paper and shadow
        Order order = Order::MostUsed;
    };

    // Finds the main colours of an image by median cut: the pixels (a grid of at most about 128 by 128 of them, transparent ones left out) are split at
    // the median of the colour channel with the widest spread until there are `count` boxes, and each box's average colour becomes a swatch. The swatches
    // are plain (not linked) and unnamed.
    // @reflect ignore=true
    struct PaletteExtract {
        // `rgba` is width * height pixels of 8-bit red, green, blue and alpha, rows without padding. Fewer swatches than asked for come back when the
        // image has fewer distinct colours (or merging joined some); none when there is nothing usable.
        static std::vector<Swatch> fromPixels(const std::uint8_t* rgba, int width, int height, const PaletteExtractOptions& options = {});
    };

}
