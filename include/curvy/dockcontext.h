#pragma once

#include "curvy/canvasview.h"
#include "curvy/sketchdocument.h"

#include <string>

namespace curvy {

    // Which property boxes the dock shows for what is being worked on: the selection and the tool. Boxes with nothing to edit are left out
    // instead of greyed, so the dock is as long as the job.
    //
    //   nothing selected, a drawing tool armed   Colour, Stroke, Opacity - they edit the defaults of the next shape ("New shapes")
    //   nothing selected, any other tool         nothing, with the note "Nothing selected"
    //   a group (exactly)                        Transform, Opacity
    //   text only                                Transform, Colour, Text, Opacity
    //   any other selection                      Transform, Colour, Stroke, Opacity (and Text when it includes text)
    // @reflect ignore=true
    struct DockBoxes {
        bool transform = false;
        bool colour = false;
        bool stroke = false;
        bool opacity = false;
        bool text = false;
        std::string caption;   // shown above the boxes; empty hides it

        bool operator==(const DockBoxes& o) const {
            return transform == o.transform && colour == o.colour && stroke == o.stroke && opacity == o.opacity && text == o.text && caption == o.caption;
        }
        bool any() const { return transform || colour || stroke || opacity || text; }
    };

    // @reflect ignore=true
    class DockContext {
    public:
        static DockBoxes boxesFor(const SketchDocument& document, SketchTool tool);
        // The tools that make a new shape (so their defaults are worth editing before the shape exists).
        static bool isDrawingTool(SketchTool tool);
    };

}
