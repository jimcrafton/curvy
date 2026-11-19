#pragma once

namespace curvy {

    // One ruler guide: a line across the whole canvas. `position` is in points from the page's top left corner (an x for a vertical guide, a y
    // for a horizontal one), whatever the document's display unit is. Document state, not a graph node.
    // @reflect ignore=true
    struct Guide {
        bool vertical = false;
        double position = 0.0;
        bool operator==(const Guide& other) const { return vertical == other.vertical && position == other.position; }
        bool operator!=(const Guide& other) const { return !(*this == other); }
    };


    // Where the rulers read zero, in points from the page's top left corner (the page corner by default). Guides stay where they are on the page; only
    // the numbers on the rulers move with it. Document state, saved with the document.
    // @reflect ignore=true
    struct RulerOrigin {
        double x = 0.0;
        double y = 0.0;
        bool operator==(const RulerOrigin& other) const { return x == other.x && y == other.y; }
        bool operator!=(const RulerOrigin& other) const { return !(*this == other); }
    };

}
