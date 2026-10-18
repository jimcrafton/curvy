#pragma once

#include "curvy/sketchobject.h"

#include <memory>
#include <string>
#include <vector>

namespace curvy {

    // What a brush does to a stroke. Calligraphic: the stroke is swept by a slanted oval nib (width = the stroke weight). Art: artwork
    // is stretched along the path (its x axis runs along it, its height across it). Scatter: copies of the artwork stand along the path.
    // For the two artwork kinds a stroke weight of 1 draws the artwork at its own size; 2 draws it twice as big.
    enum class BrushKind { Calligraphic, Scatter, Art };
    const char* brushKindName(BrushKind kind);

    // One piece of the artwork of an art or scatter brush: an outline with the first fill and stroke of the object it came from.
    // @reflect ignore=true
    struct BrushPart {
        BLPath path;
        bool closed = false;
        SketchPaint fill;
        SketchPaint stroke;
        double strokeWidth = 0.0;
    };

    // A brush of the document library. Like symbols and patterns, brushes are never changed once made (editing one swaps in a new
    // definition), so objects can share them.
    // @reflect ignore=true
    struct BrushDef {
        std::size_t id = 0;
        std::string name;
        BrushKind kind = BrushKind::Calligraphic;
        // Calligraphic: the nib angle in degrees (counter-clockwise from horizontal) and how round it is (its thickness as a percentage of
        // its length; 100 = a round pen).
        double angle = 45.0;
        double roundness = 25.0;
        // Scatter: the distance between copies as a percentage of the artwork width (100 = side by side), how far they stray from the
        // path (percent of the artwork height), how much their size varies (percent) and how far they turn at random (degrees either
        // way); whether they turn with the path.
        double spacing = 100.0;
        double scatter = 0.0;
        double sizeVariation = 0.0;
        double rotation = 0.0;
        bool followPath = true;
        // Artwork of the two artwork kinds; its box is the unit that is stretched or repeated.
        std::shared_ptr<const SymbolDef> art;

        bool hasArt() const { return kind != BrushKind::Calligraphic; }
        // The numbers above in a fixed order (angle, roundness, spacing, scatter, sizeVariation, rotation, followPath), for saving; the
        // setter keeps what is missing and brings every value into its range.
        std::vector<double> params() const;
        void setParams(const std::vector<double>& values);
        bool sameLook(const BrushDef& other) const;

        // How far from the path the brush can reach, for a stroke of weight `strokeWidth`.
        double reach(double strokeWidth) const;
        // The swept outline of a calligraphic brush (fill it with the stroke paint).
        BLPath nibOutline(const BLPath& path, double strokeWidth) const;
        // The artwork bent along `path` (one copy per subpath).
        std::vector<BrushPart> bendAlong(const BLPath& path, double strokeWidth) const;
        // Where the copies of a scatter brush stand: each matrix carries the artwork coordinates onto the page.
        std::vector<BLMatrix2D> placements(const BLPath& path, double strokeWidth) const;
        // The artwork as outlines (built the first time it is asked for).
        const std::vector<BrushPart>& parts() const;

    private:
        mutable std::vector<BrushPart> parts_;
        mutable bool partsBuilt_ = false;
    };

}
