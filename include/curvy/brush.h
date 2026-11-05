#pragma once

#include "curvy/sketchobject.h"

#include <memory>
#include <string>
#include <vector>

namespace curvy {

    // What a brush does to a stroke. Calligraphic: the stroke is swept by a slanted oval nib (width = the stroke weight). Art: artwork
    // is stretched along the path (its x axis runs along it, its height across it). Scatter: copies of the artwork stand along the path.
    // For the two artwork kinds a stroke weight of 1 draws the artwork at its own size; 2 draws it twice as big.
    // Graph: a jitnodes asset turns the path into dots or polygons (graphbrush.h); the weight is the stroke's width, as for a nib.
    enum class BrushKind { Calligraphic, Scatter, Art, Graph };
    const char* brushKindName(BrushKind kind);

    // How the artwork of an art brush is fitted to the path. Stretch: the whole artwork is stretched over the path. Guided: two guides
    // split it into a rigid head, a stretching middle and a rigid tail (an arrowhead stays an arrowhead). Repeat: whole copies, each
    // stretched a little so that they fill the path exactly.
    enum class BrushFit { Stretch, Guided, Repeat };
    // Which way the artwork of an art brush runs along the path: its x axis (left to right) or its y axis (top to bottom).
    enum class BrushDirection { LeftToRight, TopToBottom };
    // What happens at a sharp corner of the path. Bend: the artwork is bent as is (it tears on the outside of the corner and overlaps on
    // the inside). Smooth: the corner is rounded first. Miter: the outline stays continuous and meets in a sharp point (bevelled where
    // the point would be longer than the miter limit). Break: the path is cut at corners and each piece is fitted on its own.
    enum class BrushCorner { Bend, Smooth, Miter, Break };

    // One piece of the artwork of an art or scatter brush: an outline with the first fill and stroke of the object it came from.
    // @reflect ignore=true
    struct BrushPart {
        BLPath path;
        bool closed = false;
        SketchPaint fill;
        SketchPaint stroke;
        double strokeWidth = 0.0;
    };

    // The pen pressure at each point of a freehand stroke as a width profile for a brush stroke (see WidthPoint), the pressure itself being the
    // multiplier; empty when the pressure hardly changes (a mouse) or the two lists do not match.
    std::vector<WidthPoint> pressureProfile(const std::vector<BLPoint>& points, const std::vector<float>& pressure);

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
        // Art: how the artwork is fitted, where the head ends and the tail starts (as fractions 0..1 of the artwork width, for Guided),
        // and how corners are treated; the radius of a smoothed corner is a percentage of the artwork height.
        BrushFit fit = BrushFit::Stretch;
        double headGuide = 0.2;
        double tailGuide = 0.8;
        BrushCorner corner = BrushCorner::Smooth;
        double cornerRadius = 100.0;
        // Miter: the longest point of a corner, as a multiple of the half-width (like an SVG miter limit). Break: the turn (degrees)
        // above which the path is cut.
        double miterLimit = 4.0;
        double breakAngle = 60.0;
        // Art: which axis of the artwork runs along the path, and mirroring of the artwork along and across the path.
        BrushDirection direction = BrushDirection::LeftToRight;
        bool flipAlong = false;
        bool flipAcross = false;
        // The symbol of the document library this brush's artwork was taken from (0 = none: the brush keeps its own copy). While it is set,
        // redefining that symbol redefines the artwork of the brush, and so every stroke drawn with it. Not part of params().
        std::size_t sourceSymbol = 0;
        // The group the brush picker lists it under (empty: the name of its kind) and whether it is in the picker's Favorites. Neither
        // changes how the brush draws, so neither is part of params() or sameLook().
        std::string category;
        bool favorite = false;
        // Graph: the node type of the asset that paints it (see graphbrush.h).
        std::string graphType;
        std::string categoryName() const { return category.empty() ? brushKindName(kind) : category; }
        // Artwork of the two artwork kinds; its box is the unit that is stretched or repeated.
        std::shared_ptr<const SymbolDef> art;

        // Replaces the artwork (and forgets the outlines built from the old one).
        void setArt(std::shared_ptr<const SymbolDef> artwork) {
            art = std::move(artwork);
            parts_.clear();
            partsBuilt_ = false;
        }
        bool hasArt() const { return kind == BrushKind::Scatter || kind == BrushKind::Art; }
        // The weight a new stroke gets: artwork is drawn at its own size (1); a nib or a graph brush needs a real width.
        double startingWeight(double documentDefault) const {
            const double least = kind == BrushKind::Calligraphic ? 2.0 : 16.0;
            return kind == BrushKind::Scatter || kind == BrushKind::Art ? 1.0 : (documentDefault > least ? documentDefault : least);
        }
        // The numbers above in a fixed order (angle, roundness, spacing, scatter, sizeVariation, rotation, followPath, then fit, headGuide,
        // tailGuide, corner, cornerRadius, miterLimit, breakAngle, direction, flipAlong, flipAcross), for saving; the setter keeps what is missing and brings every value into its range.
        static constexpr std::size_t kBaseParams = 7;
        std::vector<double> params() const;
        void setParams(const std::vector<double>& values);
        bool sameLook(const BrushDef& other) const;

        // How far from the path the brush can reach, for a stroke of weight `strokeWidth`.
        // `widths` (a stroke's width profile, see WidthPoint) scales the brush along the path: the artwork's height, the nib, the copies. Pen
        // pressure is stored this way. Empty = the plain weight all along.
        double reach(double strokeWidth, const std::vector<WidthPoint>& widths = {}) const;
        // The swept outline of a calligraphic brush (fill it with the stroke paint).
        BLPath nibOutline(const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths = {}) const;
        // The artwork bent along `path` (one copy per subpath).
        std::vector<BrushPart> bendAlong(const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths = {}) const;
        // The same for any artwork: `artParts` inside `box` (the unit that is stretched), bent by this brush's fit, corner, direction and flips.
        // This is what the deform-along-path graph node runs.
        std::vector<BrushPart> bendParts(const std::vector<BrushPart>& artParts, const BLBox& box, const BLPath& path, double strokeWidth,
            const std::vector<WidthPoint>& widths = {}) const;
        // Where the copies of a scatter brush stand: each matrix carries the artwork coordinates onto the page.
        std::vector<BLMatrix2D> placements(const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths = {}) const;
        // The artwork as outlines (built the first time it is asked for).
        const std::vector<BrushPart>& parts() const;

    private:
        mutable std::vector<BrushPart> parts_;
        mutable bool partsBuilt_ = false;
    };

}
