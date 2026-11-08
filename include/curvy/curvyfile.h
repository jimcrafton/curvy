#pragma once

#include <string>
#include <vector>

namespace curvy {

    class SketchDocument;

    // curvy's native file format (.curvy): one JSON5 text file, see docs/native-format.md.
    //
    //   { format: "curvy", version: 1,
    //     document: { page, guides, layers, artboards, swatches, graphicStyles, groups },
    //     graph:    the jitnodes network, as data,
    //     objects:  [ { type, name, visible, locked, layer, groups, look, node, scope, ... } ] }
    //
    // Everything the document holds is written: page, guides, layers, artboards, swatches, graphic styles, groups and clips, the graph, the
    // objects with their looks (paths, text, symbol instances, pictures), and under "resources" the symbols, patterns and brushes (each
    // with its artwork as a nested document of the same shape) and the pictures (base64).

    // False when the document holds something this version of the writer leaves out (nothing, at present).
    bool curvyFileSupports(const SketchDocument& document);

    // The document as .curvy text. Numbers are written exactly (the shortest decimal that reads back to the same value).
    std::string curvyFromDocument(const SketchDocument& document);

    // Replaces the document's contents with the file in `text` (undo history and selection are cleared). Returns false, with the reason in
    // `error` when it is given, and leaves the document untouched when the text is not a well-formed .curvy file. Nothing in it is
    // trusted: counts, ids and numbers are checked before the document is touched.
    bool documentFromCurvy(const std::string& text, SketchDocument& document, std::string* error = nullptr);

    // True when `text` starts like a .curvy file (after white space and comments, an opening brace and the key "format").
    bool isCurvyDocument(const std::string& text);

    // Autosave: a copy of the document in `folder` as <stem>.curvy, or <stem>.svg when .curvy cannot hold it. Returns the path written
    // (empty when nothing could be written), and removes the other format's copy of the same stem so a recovery is offered once.
    std::string writeRecoveryCopy(SketchDocument& document, const std::string& folder, const std::string& stem);

    // The recovery copies in `folder`, newest first: .curvy files, and the .svg files older builds wrote.
    std::vector<std::string> recoveryCopies(const std::string& folder);

}
