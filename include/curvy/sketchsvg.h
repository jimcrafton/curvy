#pragma once

#include "curvy/sketchdocument.h"

#include <string>

namespace curvy {

    // SVG as sketch's file format.
    //
    // The writer emits one <path> per object, in back-to-front order, with the outline baked (a rotated
    // rectangle is just a path), plus data-* attributes that carry what plain SVG cannot: the object's type
    // and name, its frame angle, the original color space (a CMYK swatch is written as its RGB display color
    // and also as data-fill-color="cmyk c m y k", so it comes back as CMYK), and the page ppi. Gradients are
    // written as <linearGradient>/<radialGradient> in userSpaceOnUse units, so any SVG viewer shows what sketch
    // shows. Locked and hidden objects are kept (data-locked, display="none").
    //
    // The reader understands sketch's own output exactly and, for files from elsewhere, the common subset:
    // path (all commands, arcs included), rect, circle, ellipse, line, polyline, polygon, <g>, transform,
    // fill/stroke/opacity as attributes or style="", named/#rgb/#rrggbb/rgb() colors, and gradients
    // (objectBoundingBox or userSpaceOnUse, href'd stops). Text objects are read back from their own
    // data-text attributes (other <text> elements, images, clipping, filters and CSS stylesheets are skipped).
    std::string svgFromDocument(const SketchDocument& document);

    // Replaces the document's contents with the SVG in `text` (page size, ppi and objects; undo history and
    // selection are cleared). Returns false - and leaves the document untouched - when `text` is not well-formed
    // XML with an <svg> root.
    bool documentFromSvg(const std::string& text, SketchDocument& document);

}
