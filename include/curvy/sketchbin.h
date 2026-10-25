#pragma once

#include <string>

namespace curvy {

    class SketchDocument;

    // sketch's native binary file format (.skb): the whole document - page, objects, appearance, text, graphic styles, groups - as
    // little-endian binary instead of XML, for drawings with very many shapes.
    //
    //   file   = "SKB1" (4 bytes), version u16, flags u16, body
    //            flags bit 0: the body is gzip-compressed (the writer's default; the reader accepts both)
    //   body   = chunks until the end: tag (4 ASCII letters), length u64, `length` bytes
    //   chunks = PAGE (page size, ppi, color mode, background), STRS (every distinct string once: names, text, fonts),
    //            STYL (the distinct appearances, each written once and shared by the objects that wear it), GRPS (group opacity /
    //            blend), GSTY (the Graphic Styles panel), OBJS (the objects, in stacking order: type, flags, name, style index,
    //            groups, frame angle, text state, then the outline as one byte per path command and two f64 per vertex).
    //   A chunk with an unknown tag is skipped, so newer files stay readable where they only add chunks.
    // Numbers: version 2 stores outlines as float32 steps (see writePath in sketchbin.cpp), so geometry comes back to within float
    // precision (exactly, for values a float holds); colors are f32, other numbers f64.

    // True when `data` starts like a .skb file.
    bool isBinaryDocument(const std::string& data);

    // The document as a .skb file image. `compress` gzips the body (the files are several times smaller and the extra time is small).
    // `version` is the layout written: 2 (the current one, outlines as 32-bit float steps) or 1 (outlines as doubles; the older files, kept
    // so that reading them can be tested). Both are read.
    std::string binaryFromDocument(const SketchDocument& document, bool compress = true, unsigned version = 3);

    // Replaces the document's contents with the file image in `data` (undo history and selection are cleared). Returns false - and
    // leaves the document untouched - when the data is not a well-formed .skb file (wrong magic, cut short, a count or index that does
    // not fit). Nothing in `data` is trusted: every length is checked against what is left before anything is allocated.
    bool documentFromBinary(const std::string& data, SketchDocument& document);

}
