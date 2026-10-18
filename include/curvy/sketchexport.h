#pragma once

#include <blend2d/blend2d.h>

#include <string>

namespace curvy {

    class SketchDocument;

    // Drawing the document to a raster image and saving it. Pure functions of the model (no window, no dialog), so they can be tested.

    // The page of `document` drawn on a new image `scale` pixels per point wide (a 612 x 792 pt page at scale 2 is 1224 x 1584 pixels):
    // the page color (transparent when the page has none), then every visible object. False when the image would be empty or too large.
    bool renderPageImage(const SketchDocument& document, double scale, BLImage& out);

    // Renders the page and writes it to `path`; the format follows the extension (.png, .bmp, .qoi; Blend2D cannot write JPEG). A format with no
    // transparency (bmp) gets a white page where the document has none. False when the file cannot be written.
    bool exportPageImage(const SketchDocument& document, const std::string& path, double scale);

    // The same for one artboard (an index into document.artboards()): the image is that artboard's rectangle of the canvas, whatever
    // lies outside it is cut off. renderPageImage / exportPageImage are these for the active artboard.
    bool renderArtboardImage(const SketchDocument& document, std::size_t artboard, double scale, BLImage& out);
    bool exportArtboardImage(const SketchDocument& document, std::size_t artboard, const std::string& path, double scale);

    // The file name for artboard number `number` (1 based) of an export to `path`: "poster.png" -> "poster_2_Artboard-2.png".
    std::string artboardFilePath(const std::string& path, const std::string& artboardName, std::size_t number);

    // The pixels per point a document asks for: its resolution in pixels per inch over 72.
    double exportScaleFor(const SketchDocument& document);

}
