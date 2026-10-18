#pragma once

#include "curvy/shapetools.h"

#include <blend2d/blend2d.h>

#include <cstddef>
#include <vector>

namespace curvy {

    // Geometry behind the cutting tools and the Pathfinder commands, as plain functions (no view, no document) so it can be unit-tested.
    //
    // Shapes are worked on as polygons: a curve is first flattened into short straight pieces (a few hundredths of a point from the
    // curve), so the result of a boolean operation, a knife cut or an erase is made of straight edges. Everything uses the non-zero
    // fill rule, which is how the objects are painted.

    // One closed polygon (the last point is not repeated) and a set of them - the outlines of one shape or of a result.
    using Loop = std::vector<BLPoint>;
    using Loops = std::vector<Loop>;

    // The area of a polygon; positive when its points run one way round, negative the other (outlines this file produces run so
    // that the filled side is on the positive side, which makes outer outlines positive and holes negative).
    double signedArea(const Loop& loop);

    // `path` as polygons: every subpath is closed (as a fill would). `tolerance` is how far the flattened edges may stray from a curve.
    Loops flattenToLoops(const BLPath& path, double tolerance = 0.05);

    // `path` as polylines, one per subpath, left open or closed as they are (`closed[i]` tells which).
    std::vector<std::vector<BLPoint>> flattenToLines(const BLPath& path, std::vector<bool>& closed, double tolerance = 0.05);

    // The closed polygons as a path (one subpath each).
    BLPath loopsToPath(const Loops& loops);

    // True when `point` is inside the shape made by `loops` (non-zero rule).
    bool pointInLoops(const BLPoint& point, const Loops& loops);

    enum class BoolOp { Union, Intersect, Subtract, Exclude };

    // Combines two shapes: a united with b, only what is in both, a with b cut out of it (a - b), or what is in exactly one of them.
    // The result's outlines run so that the filled side is always the same one: outer outlines have a positive signedArea and holes a
    // negative one. Overlaps inside a or b themselves (a self-crossing outline) are fine. May return no loops (an empty result).
    Loops combineLoops(const Loops& a, const Loops& b, BoolOp op);

    // The separate pieces of a shape: each outer outline together with the holes inside it. `loops` must come from combineLoops.
    std::vector<Loops> splitIntoRegions(const Loops& loops);

    // One piece of the plane cut up by several shapes: a connected area (an outer outline with its holes) and which of the shapes cover
    // it (indices into the list given to arrangeCells). Every point of the shapes is in exactly one cell.
    struct ShapeCell {
        Loops region;
        std::vector<std::size_t> members;
    };

    // The cells that `shapes` (in any order) make together - what the Shape Builder lets the user pick from. Slivers of no area are dropped.
    std::vector<ShapeCell> arrangeCells(const std::vector<Loops>& shapes);

    // The polygon covering a pen stroke `width` wide along `line` (round caps and joins; a single point gives a circle).
    Loops strokeToLoops(const std::vector<BLPoint>& line, double width);

    // The parts of the polyline `line` outside the shape `region` - what an eraser leaves of a line. `closed` treats the polyline as a
    // loop. Returns nothing at all when no part of the line is inside the region (the line is untouched); `touched` says whether any was.
    std::vector<std::vector<BLPoint>> clipLineOutside(const std::vector<BLPoint>& line, bool closed, const Loops& region, bool& touched);

    // What a knife stroke along `cut` does to `region`: the shape is divided along the line (which is carried straight on past its two
    // ends) into the pieces on its one side and the pieces on the other, each piece an outer outline with its holes. Fewer than two
    // pieces (the line misses the shape, or only touches it) gives none.
    std::vector<Loops> cutAlongLine(const Loops& region, const std::vector<BLPoint>& cut);

    // ---- Scissors: cutting a path (anchors and handles) at a point ----

    // The place on `sub` nearest to `point`: the segment (from anchor `segment` to the next one, wrapping on a closed path), how far
    // along it (0..1) and how far away it is.
    bool nearestOnSubPath(const SubPath& sub, const BLPoint& point, std::size_t& segment, double& t, double& distance);

    // Cuts `sub` at segment / t, keeping the curves (the segment is split where it is cut, not flattened). A closed outline becomes ONE
    // open path that starts and ends at the cut; an open one becomes two. False when nothing would change (cutting an open path at
    // one of its two ends).
    bool splitSubPath(const SubPath& sub, std::size_t segment, double t, std::vector<SubPath>& pieces);

}
