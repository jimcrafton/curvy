#pragma once

#include "curvy/sketchdocument.h"
#include "curvy/swatchpalettes.h"
#include "curvy/swatchstrip.h"

#include <functional>
#include <string>

namespace curvy {

    // Connects a SwatchStrip to the document's palettes: shows them (with the automatic Recent palette first), and turns the strip's requests into
    // document edits, each one undo step. The strip shows the document's active palette, or Recent when that is picked. The colour the "+" tile saves
    // and "Replace with current colour" uses is the one given to setCurrent. A refused edit is reported through onMessage and the strip is read again.
    // @reflect ignore=true
    class SwatchPaletteController {
    public:
        SwatchPaletteController(SketchDocument& document, SwatchStrip& strip);
        ~SwatchPaletteController();
        SwatchPaletteController(const SwatchPaletteController&) = delete;
        SwatchPaletteController& operator=(const SwatchPaletteController&) = delete;

        // Reads the palettes, the active one and the swatches into the strip again. The document calls this for edits, undo and redo; the owner calls
        // it when the strip is shown (New and Open change the palettes without telling anyone).
        void refresh();
        void setCurrent(const Swatch& color);
        bool showingRecent() const { return recent_; }
        // The swatches the strip shows now.
        const std::vector<Swatch>& shown() const;

        // A swatch was chosen: the owner applies its colour.
        std::function<void(const Swatch& swatch)> onUse;
        // Something to tell the user (a refused edit, a swatch saved).
        std::function<void(const std::string& message)> onMessage;

    private:
        void guarded(const std::function<void()>& edit);
        void say(const std::string& message);

        SketchDocument& document_;
        SwatchStrip& strip_;
        Swatch current_;
        bool recent_ = false;
        bool refreshing_ = false;
    };

}
