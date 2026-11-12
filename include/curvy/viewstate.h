#pragma once

#include "curvy/noderef.h"

#include <optional>
#include <vector>

namespace curvy {

    // Where the user was looking when the document was saved. The workspace fills it before a save and applies it after an open; the file keeps
    // it in the optional "view" section. It is not an edit: nothing here is undoable or marks the document modified.

    // @reflect ignore=true
    struct CanvasViewState {
        float zoom = 1.0f;
        float panX = 0.0f;
        float panY = 0.0f;
    };

    // @reflect ignore=true
    struct GraphViewState {
        bool open = false;                       // the graph view covered the canvas
        std::vector<jitnodes::NodeId> level;     // the subnets entered, outermost first; empty is the root
        bool placed = false;                     // zoom and pan were set (by hand or a fit); false lets the view fit itself
        double zoom = 1.0;
        double panX = 0.0;
        double panY = 0.0;
        std::vector<jitnodes::NodeId> selected;  // in the shown network, the one the panel shows last
    };

    // @reflect ignore=true
    struct ViewState {
        std::optional<CanvasViewState> canvas;   // none: the page is centred and fitted
        std::optional<GraphViewState> graph;
    };

}
