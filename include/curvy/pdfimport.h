#pragma once

#include <string>

namespace curvy {

    class SketchDocument;

    // File > Import PDF / Illustrator: reads the vector artwork of a PDF file - and of an Adobe Illustrator (.ai) file, which since CS is a
    // PDF with extra private data - into a document, one artboard per page (laid side by side, with a gap).
    //
    // Read: pages (media box, /Rotate, inherited attributes), object streams and cross-reference-less files (objects are found by scanning,
    // so damaged files and incremental updates work), Flate / ASCIIHex / ASCII85 filters with PNG predictors, content streams with paths
    // (m l c v y h re), painting (f F f* S s B B* b b* n), the graphics state (q Q cm w J j M d gs with LW LC LJ ML D ca CA), colors in
    // DeviceGray / RGB / CMYK, CalGray / CalRGB, ICCBased and a guess for Separation / DeviceN / Indexed, axial and radial shading patterns
    // (as gradients), form XObjects (nested, with their matrix) and images (Flate or raw 8-bit gray / RGB / CMYK, JPEG, with a soft mask).
    //
    // Not read: text (a PDF holds glyphs by font, not outlines; the words are skipped), clipping paths, blend modes and soft masks of the
    // graphics state, tiling patterns, encrypted files, and the JPEG 2000 / CCITT / JBIG2
    // image filters. What cannot be read is skipped, not an error.
    //
    // True when `data` is a PDF file image. False - and the document untouched - when it is not a PDF, or has no page the reader can use.
    bool isPdfDocument(const std::string& data);
    bool documentFromPdf(const std::string& data, SketchDocument& document);

}
