#pragma once

#include <map>
#include <string>

namespace curvy {

    // Where a cursor image's "point" is, in the image's pixels: Resources/cursors/<name>.svg is drawn at `size` x `size`
    // and the pointer's position is (x, y) inside it (the red dots of the mockup's cursor gallery).
    struct CursorHotspot {
        int x = 0;
        int y = 0;
        int size = 32;
    };

    // Reads Resources/cursors/cursors.json - { "pen": { "x": 6, "y": 27, "size": 32 }, ... } - into name -> hotspot. Tolerant:
    // an entry it cannot read is skipped, and anything that is not an object of numbers is ignored.
    std::map<std::string, CursorHotspot> parseCursorHotspots(const std::string& json);

}
