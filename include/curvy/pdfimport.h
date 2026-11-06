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
    // Text in a font the file embeds (TrueType, OpenType, CFF; see fontprogram.h) becomes that font's own outlines, one path per run (see
    // PdfImportOptions). Other text becomes live Text objects: characters from ToUnicode or the encoding, advances from the font's widths,
    // and the installed font of the family named by BaseFont (bold / italic from its name and descriptor). Composite fonts without
    // ToUnicode and Type 3 fonts have no readable characters there and are skipped; embedded Type 1 (PostScript) programs are not read.
    //
    // Encrypted files open when their user password is empty (the standard security handler with RC4 or AES-128, revisions 2 to 4);
    // AES-256 files and files that need a password are refused.
    //
    // Not read: blend modes, tiling patterns, and the JPEG 2000 / CCITT / JBIG2
    // image filters. What cannot be read is skipped, not an error.
    //
    // True when `data` is a PDF file image. False - and the document untouched - when it is not a PDF, or has no page the reader can use.
    bool isPdfDocument(const std::string& data);

    struct PdfImportOptions {
        // Text in a font the file embeds (TrueType, OpenType, CFF) becomes the font's own outlines, one path per run: the letterforms are
        // exact and need no installed font, but the words can no longer be edited. Text in other fonts, or all text when this is false,
        // becomes live Text objects in an installed font of the same family.
        bool textAsOutlines = true;
    };
    // `problem`, when given, receives a reason the user could act on ("needs a password to open"); it is left alone for other failures.
    bool documentFromPdf(const std::string& data, SketchDocument& document, const PdfImportOptions& options = PdfImportOptions(), std::string* problem = nullptr);

}
