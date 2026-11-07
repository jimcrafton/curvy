#pragma once

#include <string>

namespace curvy {

    class SketchDocument;

    // File > Export As EPS: the active artboard as an Encapsulated PostScript (level 2) file, with %%BoundingBox = the artboard in points.
    //
    // Paths, solid colors in the document's own color model (RGB, CMYK, gray via setrgbcolor / setcmykcolor / setgray), dashes, caps, joins,
    // clipping masks, groups, symbols and text (as glyph outlines) stay vector. Gradients are written as vector steps (stripes / rings, 96 per
    // gradient) clipped to the shape, so no PostScript level 3 shading is needed. Pictures are written as ASCIIHex RGB images.
    //
    // PostScript has no transparency, so: a color with opacity below 1 is mixed with the page color (white when the page is transparent);
    // group opacity and blend modes are not applied; and objects with shadows, glows or blurs, and pattern fills, are drawn as pictures
    // of themselves (which are opaque, on the page color).
    std::string epsFromDocument(const SketchDocument& document);

    // Writes epsFromDocument to `path`; false when the file cannot be written.
    bool writeEps(const SketchDocument& document, const std::string& path);

}
