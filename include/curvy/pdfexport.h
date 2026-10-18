#pragma once

#include <string>

namespace curvy {

    class SketchDocument;

    // File > Export As PDF: the document as a PDF 1.4 file, one page per artboard (the page is the artboard, in points).
    //
    // The artwork stays vector: paths, solid colors in the document's own color model (RGB, CMYK, gray), gradients (axial / radial shading
    // patterns with the stops as a stitching function), pattern fills (tiling patterns of the tile image), dashes, caps, joins, clipping
    // masks (a clip path), group and object opacity and blend modes (transparency groups with an extended graphics state), symbols (drawn
    // from their artwork), pictures (image XObjects, with a soft mask for transparency) and text (as its glyph outlines, so no font is
    // needed). Brush strokes are written as the outlines they are made of, width-profile strokes as filled outlines.
    //
    // What a PDF cannot say as vectors - shadows, glows and blurs, which work on pixels - is drawn into a picture (twice the point size)
    // and placed where it belongs. A gradient that repeats or reflects past its ends is written padded (PDF shadings only extend).
    std::string pdfFromDocument(const SketchDocument& document);

    // Writes pdfFromDocument to `path`; false when the file cannot be written.
    bool writePdf(const SketchDocument& document, const std::string& path);

}
