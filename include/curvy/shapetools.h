#pragma once

#include <blend2d/blend2d.h>

#include <cmath>

#include <vector>

namespace curvy {

    // The geometry behind the drawing tools, as plain functions (no view, no document) so it can be unit-tested.

    // The corners of a regular polygon around `center` with its first corner `firstAngle` radians from the +x axis
    // (-pi/2 points it up; y grows downward). `sides` is clamped to 3 or more.
    std::vector<BLPoint> regularPolygonPoints(const BLPoint& center, double radius, int sides, double firstAngle);

    // A star: `points` outer corners on `outer`, alternating with inner corners on outer * innerRatio. Twice `points` vertices.
    std::vector<BLPoint> starPoints(const BLPoint& center, double outer, double innerRatio, int points, double firstAngle);

    // Ramer-Douglas-Peucker: the fewest of `points` (always keeping the ends) that stay within `tolerance` of the original line.
    std::vector<BLPoint> simplifyPolyline(const std::vector<BLPoint>& points, double tolerance);

    // A smooth open curve through every point (a Catmull-Rom spline written as cubic Beziers). Two points give a line.
    BLPath smoothPath(const std::vector<BLPoint>& points);

    // One anchor of a pen path with its two Bezier handles (absolute positions). An anchor without handles is a corner.
    struct PenAnchor {
        BLPoint point;
        BLPoint in;       // the handle toward the previous anchor
        BLPoint out;      // the handle toward the next anchor
        bool hasIn = false;
        bool hasOut = false;
    };

    // One continuous outline of a path as anchors with handles - the form the Direct Select tool edits.
    struct SubPath {
        std::vector<PenAnchor> anchors;
        bool closed = false;
    };
    // Splits a path into its outlines. Quadratic curves become cubics; a closing line that ends on the first point is
    // folded into it (so a rectangle has four anchors, not five).
    std::vector<SubPath> pathToSubPaths(const BLPath& path);
    BLPath subPathsToPath(const std::vector<SubPath>& subPaths);

    // The path through the anchors: a straight line between two corners, otherwise a cubic using the handles (a missing
    // handle sits on its anchor). `closed` joins the last anchor back to the first and closes the path.
    BLPath penPath(const std::vector<PenAnchor>& anchors, bool closed);

    // ---- Recognising regular shapes (live shape handles) ----

    // A regular polygon or a star, as the numbers that make it: `points` corners (or outer points of a star), the center, the
    // outer radius, the star inner radius (0 for a polygon) and the angle of the first outer corner. A shape drawn with the
    // Polygon / Star tools has exactly this form, and so does one loaded from a file - nothing but the outline is stored.
    struct RegularShape {
        bool star = false;
        int points = 0;
        BLPoint center;
        double outer = 0.0;
        double inner = 0.0;
        double firstAngle = 0.0;
        double ratio() const { return outer > 0.0 ? inner / outer : 0.0; }
    };
    // True (and fills `out`) when `corners` are the vertices of a regular polygon, or of a star whose outer and inner corners
    // alternate, going clockwise on screen from the first one. Anything squashed, turned unevenly or edited is not.
    bool analyzeRegularShape(const std::vector<BLPoint>& corners, RegularShape& out);
    // The closed outline for the shape (the first outer corner at firstAngle).
    BLPath regularShapePath(const RegularShape& shape);

    // A rounded rectangle: its box in the frame turned by `angle` (a point p of the outline is, in box space, R(-angle) * p)
    // and the corner radius.
    struct RoundRectShape {
        BLBox box;
        double radius = 0.0;
        double angle = 0.0;
    };
    // True when `path` is a rounded (or plain) rectangle in the frame turned by frameAngle: four or eight anchors, all on the
    // edges of their bounding box. The radius is how far the first corner arc starts from the box corner.
    bool analyzeRoundRect(const BLPath& path, double frameAngle, RoundRectShape& out);
    BLPath roundRectPath(const RoundRectShape& shape);

    // A circular arc: the part of a circle from angle `start` going `sweep` radians (positive = clockwise on screen, y grows
    // downward). Angles are measured from the +x axis. The arc tool makes these; like the other live shapes, one is recognised
    // from its outline alone.
    struct ArcShape {
        BLPoint center;
        double radius = 0.0;
        double start = 0.0;
        double sweep = 0.0;
        BLPoint startPoint() const { return BLPoint(center.x + radius * std::cos(start), center.y + radius * std::sin(start)); }
        BLPoint endPoint() const { return BLPoint(center.x + radius * std::cos(start + sweep), center.y + radius * std::sin(start + sweep)); }
    };
    // The open outline: cubic Beziers of at most a quarter turn each.
    BLPath arcPath(const ArcShape& arc);
    // True when `path` is one open run of cubic curves that all lie on one circle (a gentle error of a fraction of a percent
    // is allowed), turning one way. The sweep is at most one full turn.
    bool analyzeArc(const BLPath& path, ArcShape& out);

    // Fits cubic Beziers to a freehand polyline (Schneider's "automatically fitting digitized curves"): as few curve segments as
    // keep the curve within `tolerance` of every sample, with smooth joins - except at sharp corners (turns over about 60
    // degrees), where the line is cut and the two sides are fitted separately so the corner stays sharp. Returns the anchors with
    // their handles (an open path: `penPath(result, false)`); fewer than two distinct points give what there is.
    std::vector<PenAnchor> fitCurve(const std::vector<BLPoint>& points, double tolerance);

    // Lightly smooths a hand drawn polyline: each pass replaces every point but the two ends with (previous + 2 * itself + next) / 4.
    std::vector<BLPoint> smoothPolyline(const std::vector<BLPoint>& points, int passes);

}
