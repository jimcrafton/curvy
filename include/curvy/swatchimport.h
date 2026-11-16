#pragma once

#include "curvy/swatchpalettes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace curvy {

    // What a swatch file held: one palette per group (an ASE file may have several; an ACO file has one), named after the group, or `fallbackName`
    // for colours that were not in a group. `error` is empty on success; when it is not, `palettes` is empty. Colours are plain (not linked).
    // @reflect ignore=true
    struct SwatchImportResult {
        std::vector<Palette> palettes;
        std::string error;
        std::size_t colorCount() const;
    };

    // Reads Adobe swatch files into palettes. Both are big-endian binary formats. RGB, CMYK, Gray, Lab (and, in ACO, HSB) colours are turned into RGB.
    // CMYK and Lab are the plain textbook conversions (no colour profile), so a swatch can differ a little from what Photoshop shows.
    // @reflect ignore=true
    struct SwatchImport {
        // An .aco colour swatch file, version 1 or 2 (version 2 carries the names and is preferred when present).
        static SwatchImportResult fromAco(const std::vector<std::uint8_t>& bytes, const std::string& fallbackName);
        // An .ase Adobe Swatch Exchange file.
        static SwatchImportResult fromAse(const std::vector<std::uint8_t>& bytes, const std::string& fallbackName);
        // By the file's extension (.aco or .ase, any case); other names come back with an error.
        static SwatchImportResult fromFile(const std::string& path);

        // The conversions, in 0..1 (exposed for tests). Lab: L 0..100, a and b about -128..127, D50 white.
        static void cmykToRgb(float c, float m, float y, float k, float& r, float& g, float& b);
        static void hsbToRgb(float hueDegrees, float saturation, float brightness, float& r, float& g, float& b);
        static void labToRgb(float l, float a, float b, float& r, float& g, float& blue);
    };

}
