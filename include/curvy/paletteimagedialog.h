#pragma once

#include "curvy/paletteimagemodel.h"

#include <newui/frame.h>

namespace curvy {

    // The "New palette from image" dialog (Resources/paletteFromImage.newui, from docs/mockups/paletteFromImage.html): the picture, a Choose image button,
    // how many colours (4 to 16), the order, the two filters, the colours found as they change, and the palette's name. Runs modally over `owner`.
    // @reflect ignore=true
    class PaletteImageDialog {
    public:
        // True when the user pressed Create palette (the model then holds the choices and the colours); false on Cancel or when the dialog file cannot be
        // loaded. The model already holds an image; Choose image replaces it.
        static bool run(newui::Frame& owner, PaletteImageModel& model);
    };

}
