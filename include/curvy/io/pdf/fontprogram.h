#pragma once

#include <blend2d/blend2d.h>

#include <memory>
#include <string>

namespace curvy {

    // The glyph outlines of an embedded font program, read from the bytes of a font file: TrueType (glyf, composites included), OpenType
    // with CFF outlines, bare CFF (what PDF calls Type1C and CIDFontType0C), and Type 1 (PostScript, eexec-encrypted, seac and flex
    // included). Outlines are in em units (1 = the font size), y up. Hints, kerning and substitution tables are not read.
    class FontProgram {
    public:
        virtual ~FontProgram() = default;

        // Null when the data is not a font this can read.
        static std::shared_ptr<FontProgram> load(const std::string& data);

        virtual unsigned glyphCount() const = 0;
        virtual bool outline(unsigned glyph, BLPath& out) const = 0;

        // 0 (the .notdef glyph) when the font has none for it.
        virtual unsigned glyphForUnicode(unsigned codePoint) const = 0;
        // Through the font's own encoding: a symbolic cmap of a TrueType font, the built-in encoding of a CFF font.
        virtual unsigned glyphForCode(unsigned code) const = 0;
        // A character id of a CID-keyed font; the glyph index itself otherwise.
        virtual unsigned glyphForCid(unsigned cid) const = 0;
        // By the glyph's own name ("ff", "eacute"); only fonts that carry names (CFF) answer, the others return 0.
        virtual unsigned glyphForName(const std::string&) const { return 0; }
    };

    // The Unicode value of a glyph name of the Adobe glyph list ("eacute", "uni00E9", "u1F600"); 0 when unknown.
    unsigned glyphNameToUnicode(const std::string& name);

}
