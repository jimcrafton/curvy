#pragma once

#include "curvy/noderef.h"
#include "curvy/sketchcolor.h"
#include "curvy/textoutline.h"

#include <blend2d/blend2d.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>
#include <string>
#include <vector>

namespace curvy {

    // One color stop of a gradient: offset 0..1 along the gradient line.
    // @reflect ignore=true
    struct GradientStop {
        double offset = 0.0;
        SketchColor color;

        bool operator==(const GradientStop& other) const { return offset == other.offset && color == other.color; }
    };

    // One colour point of a point gradient: where it sits as fractions of the object's bounding box (0,0 = top-left, 1,1 = bottom-right),
    // its colour, and how far it reaches as a fraction of the box's longer side.
    // @reflect ignore=true
    struct GradientPoint {
        double x = 0.5;
        double y = 0.5;
        SketchColor color;
        double radius = 0.4;

        bool operator==(const GradientPoint& other) const { return x == other.x && y == other.y && radius == other.radius && color == other.color; }
    };

    // A projective map of the plane (a 3x3 matrix on [x y 1], row major): what a perspective warp does to the points it moves.
    // @reflect ignore=true
    struct PaintWarp {
        double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

        bool isIdentity() const;
        BLPoint map(const BLPoint& p) const;
        // False when the map has no inverse.
        bool inverted(PaintWarp& out) const;
        // This map followed by `next`.
        PaintWarp then(const PaintWarp& next) const;
        static PaintWarp fromMatrix(const BLMatrix2D& matrix);
        // The map taking quad `from` (TL, TR, BR, BL) onto quad `to`; the identity when `from` is degenerate.
        static PaintWarp between(const BLPoint* from, const BLPoint* to);
        bool operator==(const PaintWarp& other) const;
    };

    struct SymbolDef;
    struct BrushDef;
    class SketchDocument;

    enum class GradientSpread { Pad, Repeat, Reflect };

    // A gradient described relative to the object it paints: linear gradients run along `angleDegrees`
    // (0 = left to right, 90 = top to bottom) across the object's bounding box; radial ones are centered on
    // it. Because nothing is stored in document coordinates, a gradient follows its object through moves,
    // resizes and rotations without any bookkeeping. Stops are kept sorted by offset.
    // @reflect ignore=true
    class SketchGradient {
    public:
        std::vector<GradientStop> stops;
        double angleDegrees = 0.0;
        GradientSpread spread = GradientSpread::Pad;

        // An explicit axis, set by the Gradient tool or read from a file: two points kept as fractions of the
        // object's bounding box (0,0 = top-left, 1,1 = bottom-right, may lie outside), so it too follows its
        // object. Linear: where the gradient starts (offset 0) and ends (offset 1). Radial: the center and a
        // point on the outer circle. Without it the axis is derived from angleDegrees and the box (linear:
        // edge to edge through the center; radial: centered, reaching the box's longer half-side).
        bool hasAxis = false;
        double axisX1 = 0.0, axisY1 = 0.5, axisX2 = 1.0, axisY2 = 0.5;
        // Radial only: the radius across the axis as a multiple of the radius along it (1 = a circle). The
        // ellipse is turned so its long/short sides line up with the axis direction.
        double axisAspect = 1.0;
        // How far the lines of equal color lean away from square to the axis, as the tangent of that lean (0 =
        // square; a gradient under a skewing transform has it). Linear: the iso-color lines run along
        // (skew * axis direction + across direction). Radial: the ellipse's across radius leans the same way.
        double axisSkew = 0.0;
        // Stroke paints only: the colors run along the stroke's path (by its length) instead of across the
        // object's bounding box. The axis, angle and radial/linear kind then do not matter.
        bool alongStroke = false;

        // Point gradients only (SketchPaint::Kind::Points): the colour points inside the object. The colour at any place is the average of
        // the points' colours, each weighted by how near that place is to the point in units of its radius.
        std::vector<GradientPoint> points;
        // `color` at the top left and fading to white at the bottom right, as two points - what a solid fill becomes.
        static SketchGradient pointsFromColor(const SketchColor& color);
        // The colour the points give at the box fractions (x, y) of an object whose box is w x h.
        SketchColor pointColorAt(double x, double y, double w, double h) const;
        // The points drawn into an image of width x height pixels covering `box` (premultiplied ARGB).
        BLImage pointsImage(int width, int height, const BLBox& box) const;

        // The matrix that turns the plain gradient (a circle of radius |end - start| around `start`, or the
        // linear gradient from `start` to `end`) into the ellipse / leaning gradient described by aspect and
        // skew; identity when there is neither.
        BLMatrix2D axisMatrix(const BLPoint& start, const BLPoint& end, bool radial) const;

        // The effective axis over `box`, in document units.
        void axisPoints(const BLBox& box, bool radial, BLPoint& start, BLPoint& end) const;
        // Fixes the axis to these document points (and, for a linear gradient, the angle to match).
        void setAxis(const BLBox& box, const BLPoint& start, const BLPoint& end);
        // Turns the gradient line to `degrees` about its midpoint, keeping its length (a no-op on the axis
        // itself unless hasAxis); always updates angleDegrees.
        void setAngle(const BLBox& box, double degrees);

        // `color` fading to white - a reasonable starting gradient when a solid fill is switched to one.
        static SketchGradient fromColor(const SketchColor& color);

        // Sorts by offset (stable).
        void sortStops();
        // Interpolated color at offset t (clamped to the first/last stop); transparent black when empty.
        SketchColor colorAt(double t) const;
        // Mirrors every stop about the middle of the gradient.
        void reverse();

        bool operator==(const SketchGradient& other) const {
            return angleDegrees == other.angleDegrees && spread == other.spread && stops == other.stops && points == other.points && hasAxis == other.hasAxis &&
                alongStroke == other.alongStroke &&
                (!hasAxis || (axisX1 == other.axisX1 && axisY1 == other.axisY1 && axisX2 == other.axisX2 && axisY2 == other.axisY2 &&
                    axisAspect == other.axisAspect && axisSkew == other.axisSkew));
        }
        bool operator!=(const SketchGradient& other) const { return !(*this == other); }
    };

    class SketchDocument;

    // A library definition: artwork made once and used by reference - placed any number of times as Symbol objects (the symbol library), or
    // repeated to fill a shape as a Pattern paint (the pattern library). `content` is a
    // small document holding the artwork in its own coordinates; `box` is the area of it that an instance shows - an instance is a
    // quadrilateral, and the artwork is carried from `box` onto it, so moving, resizing, rotating and shearing the instance work like
    // they do on any other object. Definitions are never changed once made (redefining a symbol swaps in a new one).
    // @reflect ignore=true
    struct SymbolDef {
        std::size_t id = 0;
        std::string name;
        BLBox box = BLBox(0, 0, 1, 1);
        std::shared_ptr<SketchDocument> content;
        // The artwork drawn into a w x h pixel image (the box scaled to fit it), for patterns; the last few sizes are kept.
        const BLImage& tile(int width, int height) const;
        mutable std::vector<std::tuple<int, int, BLImage>> tiles;
    };

    // What fill and stroke hold: nothing, a solid color, a gradient or a repeating pattern.
    class SketchPaint {
    public:
        enum class Kind { None, Solid, Linear, Radial, Pattern, Points };

        SketchPaint() = default;
        static SketchPaint none() { return SketchPaint(); }
        static SketchPaint solid(const SketchColor& color);
        static SketchPaint linear(const SketchGradient& gradient);
        static SketchPaint radial(const SketchGradient& gradient);
        // A point gradient: colour points blending into each other across the object (gradient.points).
        static SketchPaint points(const SketchGradient& gradient);
        // The pattern's tile (a SymbolDef: its box is the tile, its artwork what is repeated) at `scale` times its size and turned by
        // `angleDegrees`, anchored at the document origin. patternRef is the same before the definition is known (loading a file): it is
        // bound by bindPattern once the library is in.
        static SketchPaint pattern(const std::shared_ptr<const SymbolDef>& def, double scale = 1.0, double angleDegrees = 0.0);
        static SketchPaint patternRef(std::size_t id, double scale, double angleDegrees);

        Kind kind() const { return kind_; }
        bool isNone() const { return kind_ == Kind::None; }
        bool isGradient() const { return kind_ == Kind::Linear || kind_ == Kind::Radial; }
        bool isPoints() const { return kind_ == Kind::Points; }
        bool isPattern() const { return kind_ == Kind::Pattern; }
        const std::shared_ptr<const SymbolDef>& patternDef() const { return pattern_; }
        std::size_t patternId() const { return patternId_; }
        double patternScale() const { return patternScale_; }
        double patternAngle() const { return patternAngle_; }
        void bindPattern(const std::shared_ptr<const SymbolDef>& def) { pattern_ = def; if (def) { patternId_ = def->id; } }
        // The solid color; for a gradient, its first stop (so color-only callers keep working).
        const SketchColor& color() const { return color_; }
        const SketchGradient& gradient() const { return gradient_; }

        bool operator==(const SketchPaint& other) const {
            if (kind_ != other.kind_) {
                return false;
            }
            if (kind_ == Kind::None) {
                return true;
            }
            if (kind_ == Kind::Pattern) {
                return patternId_ == other.patternId_ && pattern_ == other.pattern_ && patternScale_ == other.patternScale_ && patternAngle_ == other.patternAngle_;
            }
            return isGradient() || isPoints() ? gradient_ == other.gradient_ : color_ == other.color_;
        }
        bool operator!=(const SketchPaint& other) const { return !(*this == other); }

    private:
        Kind kind_ = Kind::None;
        SketchColor color_;
        SketchGradient gradient_;
        std::shared_ptr<const SymbolDef> pattern_;
        std::size_t patternId_ = 0;
        double patternScale_ = 1.0;
        double patternAngle_ = 0.0;
    };

    // How the ends and corners of a stroke look (the Stroke panel's Cap / Join / Miter).
    enum class StrokeCap { Butt, Round, Square };
    enum class StrokeJoin { Miter, Round, Bevel };
    // Where a closed outline's stroke sits: centred on it, or wholly inside / outside it (the Stroke panel's Align row).
    enum class StrokeAlign { Center, Inside, Outside };
    // What an open path's end carries (the Stroke panel's Arrows row): a chevron, a filled triangle or a dot.
    enum class ArrowStyle { None, Simple, Filled, Dot };
    // One point of a variable-width stroke (the Width tool): `t` is the distance along the path as a fraction
    // 0..1 (per subpath); `left`/`right` are the half-widths of the two sides as multiples of half the stroke
    // weight (1 = the plain stroke). Between points the width eases from one to the next; outside the first and
    // last point it stays at theirs.
    // @reflect ignore=true
    struct WidthPoint {
        double t = 0.0;
        double left = 1.0;
        double right = 1.0;
        bool operator==(const WidthPoint& o) const { return t == o.t && left == o.left && right == o.right; }
    };
    // The eased multiplier at `t` for one side (1 when there are no points). `points` must be sorted by t.
    double widthScaleAt(const std::vector<WidthPoint>& points, double t, bool leftSide);

    // @reflect ignore=true
    struct StrokeOptions {
        StrokeCap cap = StrokeCap::Butt;
        StrokeJoin join = StrokeJoin::Miter;
        // A miter longer than this many stroke widths is bevelled (Illustrator's default is 10).
        double miterLimit = 10.0;
        // Dash, gap, dash, gap... in points (SVG stroke-dasharray); empty = solid. Odd counts repeat, as in SVG.
        std::vector<double> dashes;
        // Variable width, sorted by t; empty = the plain stroke weight all along. Drawn as a filled outline, so
        // dashes and "gradient along stroke" do not apply while it is set.
        std::vector<WidthPoint> widths;
        // A brush that draws the stroke (see brush.h); then cap, join, dashes and widths do not apply. `brushRef` is the id while a file
        // is being read, before the library is in.
        std::shared_ptr<const BrushDef> brush;
        std::size_t brushRef = 0;
        // Inside / outside applies to closed outlines (an open path stays centred). Arrowheads go on the first and last end of the open
        // paths; `arrowScale` is a percentage of the default size (four stroke weights long). Both are drawn as one filled outline.
        StrokeAlign align = StrokeAlign::Center;
        ArrowStyle arrowStart = ArrowStyle::None, arrowEnd = ArrowStyle::None;
        double arrowScale = 100.0;

        // True when the stroke is drawn as a filled outline made by strokeDecoration() instead of a plain Blend2D stroke.
        bool decorated() const { return align != StrokeAlign::Center || arrowStart != ArrowStyle::None || arrowEnd != ArrowStyle::None; }

        bool operator==(const StrokeOptions& other) const {
            return cap == other.cap && join == other.join && miterLimit == other.miterLimit && dashes == other.dashes && widths == other.widths &&
                brush == other.brush && brushRef == other.brushRef && align == other.align && arrowStart == other.arrowStart &&
                arrowEnd == other.arrowEnd && arrowScale == other.arrowScale;
        }
        bool operator!=(const StrokeOptions& other) const { return !(*this == other); }
    };

    // How an object's paint combines with what is under it (the Appearance panel's Blend list; the order is the
    // list's order). Normal is plain source-over.
    enum class BlendMode { Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, HardLight, SoftLight, Difference, Exclusion };
    // The mode's name as the panel shows it ("Color Dodge") and as CSS mix-blend-mode spells it ("color-dodge").
    const char* blendModeName(BlendMode mode);
    const char* blendModeCss(BlendMode mode);
    // Parses a CSS mix-blend-mode value; unknown values (and "normal") give Normal.
    BlendMode blendModeFromCss(const std::string& value);

    // One entry of an object's appearance stack, painted bottom to top: a fill or a stroke with its own paint, opacity,
    // blend mode and visibility (a stroke also has its weight and options). Every object keeps at least one fill and one
    // stroke - the "primary" ones, which fill()/stroke()/strokeWidth()/strokeOptions() read and the old setters write;
    // either may be a paint of None.
    enum class AppearanceKind { Fill, Stroke, Effect };
    // Live effects, in the order of the Add Effect menu (new kinds are only ever added at the end: the numbers are in files). An
    // effect acts on everything below it in the stack: a drop shadow / outer glow is cast behind that composite, Gaussian Blur blurs
    // it, Inner Shadow is cast inside its silhouette, Round Corners / Offset Path / Roughen reshape the outline of the fills and
    // strokes below them.
    //   Offset Path: `radius` is how far the outline moves (negative = inward).
    //   Roughen: `radius` is the size of the wobble, `offsetX` the detail (the length of the pieces the outline is cut into) and
    //   `offsetY` the seed - the same seed always roughens the same way, so the shape does not shimmer when it moves.
    enum class EffectKind { DropShadow, OuterGlow, GaussianBlur, RoundCorners, InnerShadow, OffsetPath, Roughen };
    const char* effectName(EffectKind kind);
    // True for the effects that reshape the outline (the rest work on pixels).
    inline bool isGeometryEffect(EffectKind kind) { return kind == EffectKind::RoundCorners || kind == EffectKind::OffsetPath || kind == EffectKind::Roughen; }
    // True for the effects that cast a tinted shadow or glow (they have a color, and a blend mode / amount of their own).
    inline bool isTintedEffect(EffectKind kind) { return kind == EffectKind::DropShadow || kind == EffectKind::OuterGlow || kind == EffectKind::InnerShadow; }
    // @reflect ignore=true
    struct AppearanceItem {
        AppearanceKind kind = AppearanceKind::Fill;
        bool visible = true;
        double opacity = 1.0;
        // Normal means "follow the object's blend mode".
        BlendMode blendMode = BlendMode::Normal;
        SketchPaint paint;
        // Strokes only.
        double strokeWidth = 1.0;
        StrokeOptions strokeOptions;
        // Effects only (paint and the stroke fields are unused; `opacity` is the effect's amount and `blendMode` how a
        // shadow / glow is composited).
        EffectKind effect = EffectKind::DropShadow;
        SketchColor color = SketchColor::gray(0.0f);  // shadow / glow tint
        double softness = 6.0;                       // blur radius in document units (shadow, glow, blur)
        double offsetX = 4.0, offsetY = 4.0;         // shadow offset
        double radius = 10.0;                        // Round Corners

        static AppearanceItem makeFill(const SketchPaint& paint) {
            AppearanceItem item;
            item.kind = AppearanceKind::Fill;
            item.paint = paint;
            return item;
        }
        static AppearanceItem makeStroke(const SketchPaint& paint, double width = 1.0, const StrokeOptions& options = StrokeOptions()) {
            AppearanceItem item;
            item.kind = AppearanceKind::Stroke;
            item.paint = paint;
            item.strokeWidth = width;
            item.strokeOptions = options;
            return item;
        }
        // A new effect with the defaults of its kind.
        static AppearanceItem makeEffect(EffectKind effect) {
            AppearanceItem item;
            item.kind = AppearanceKind::Effect;
            item.effect = effect;
            item.paint = SketchPaint::none();
            switch (effect) {
                case EffectKind::DropShadow: item.opacity = 0.75; item.blendMode = BlendMode::Multiply; break;
                case EffectKind::OuterGlow: item.opacity = 0.75; item.color = SketchColor::fromHex("#ffe066"); item.softness = 8.0; break;
                case EffectKind::GaussianBlur: item.softness = 4.0; break;
                case EffectKind::RoundCorners: break;
                case EffectKind::InnerShadow: item.opacity = 0.75; item.blendMode = BlendMode::Multiply; item.softness = 5.0; item.offsetX = 3.0; item.offsetY = 3.0; break;
                case EffectKind::OffsetPath: item.radius = 6.0; break;
                case EffectKind::Roughen: item.radius = 2.0; item.offsetX = 8.0; item.offsetY = 1.0; break;
            }
            return item;
        }
        bool operator==(const AppearanceItem& o) const {
            return kind == o.kind && visible == o.visible && opacity == o.opacity && blendMode == o.blendMode && paint == o.paint &&
                strokeWidth == o.strokeWidth && strokeOptions == o.strokeOptions && effect == o.effect && color == o.color &&
                softness == o.softness && offsetX == o.offsetX && offsetY == o.offsetY && radius == o.radius;
        }
        bool operator!=(const AppearanceItem& o) const { return !(*this == o); }
    };
    // The item `stored` names when it is of the kind, else the first item of the kind, else a none-painted placeholder.
    const AppearanceItem& activeItemOf(const std::vector<AppearanceItem>& items, AppearanceKind kind, std::size_t stored);
    // The index activeItemOf() resolves to (items.size() when there is no item of the kind).
    std::size_t activeIndexOf(const std::vector<AppearanceItem>& items, AppearanceKind kind, std::size_t stored);

    // Draws `content` into an offscreen layer covering `userBounds` (document units) at the context's own device
    // resolution, then composites the layer with `opacity` and the blend mode - so everything in the layer blends as one
    // piece (overlapping fills and strokes do not double up). Used for objects and groups with opacity < 1 or a blend mode.
    void paintIsolated(BLContext& ctx, const BLBox& userBounds, double opacity, BlendMode mode, const std::function<void(BLContext&)>& content);
    // Like paintIsolated, with opacity 1 and Normal, but only what lies inside `mask` (document units, filled non-zero) is kept - a clipping mask.
    void paintClipped(BLContext& ctx, const BLBox& userBounds, const BLPath& mask, const std::function<void(BLContext&)>& content);

    // A picture in a document (imagedata.h): the file it came from and its decoded pixels. Shared by every Image object made from it.
    // @reflect ignore=true
    struct ImageData {
        std::string name;
        std::string bytes;       // the file as it was (PNG, JPEG, BMP or QOI), saved as it is
        std::string mime;        // "image/png" ...
        int width = 0, height = 0;
        BLImage image;           // PRGB32
        // The picture to draw when it is shown at `scale` times its own size (pixels on screen per pixel of the picture): a copy halved as
        // often as that stays sharp, so a 12-megapixel photo is not read through in full to draw a thumbnail. `extra` is how much bigger
        // than the returned image's own pixels each of them is (1 for the picture itself).
        const BLImage& levelFor(double scale, double& extra) const;
        mutable std::vector<BLImage> mips;
    };

    // Image: a picture drawn into a quadrilateral (like a Symbol, so it moves, scales, turns and shears like any object).
    enum class SketchObjectType { Line, Rect, RoundRect, Ellipse, Polygon, Polyline, Path, Text, Symbol, Image };

    // Text that follows a path, or is stretched between two (see TextWarper in textwarp.h). The paths are copies, kept in the text's own
    // flat space (the one SketchObject::textMatrix() places on the page), so moving, turning or scaling the text carries them along.
    // @reflect ignore=true
    struct TextWarp {
        enum class Kind { None, OnPath, Between };
        Kind kind = Kind::None;
        BLPath first;          // OnPath: the path the baseline follows; Between: the top path
        BLPath second;         // Between: the bottom path
        double start = 0.0;    // OnPath: where the text starts, as a percentage of the path length
        bool flip = false;     // OnPath: the other side of the path; Between: top and bottom swapped
        bool active() const { return kind != Kind::None; }
        bool operator==(const TextWarp& o) const {
            return kind == o.kind && start == o.start && flip == o.flip && first.equals(o.first) && second.equals(o.second);
        }
        bool operator!=(const TextWarp& o) const { return !(*this == o); }
    };



    // One drawable thing. Geometry is kept as a BLPath in document units (points, y down) so
    // rendering, bounds and hit-testing all share it; the typed factories build that path.
    // Mutating an object is the document's job (undoable commands arrive in S2) - these setters
    // are what those commands call.
    class SketchObject {
    public:
        static SketchObject* makeLine(double x1, double y1, double x2, double y2);
        static SketchObject* makeRect(double x, double y, double width, double height);
        static SketchObject* makeRoundRect(double x, double y, double width, double height, double radius);
        static SketchObject* makeEllipse(double cx, double cy, double rx, double ry);
        static SketchObject* makePolygon(const std::vector<BLPoint>& points);
        static SketchObject* makePolyline(const std::vector<BLPoint>& points);
        static SketchObject* makePath(const BLPath& path);
        // A Text object: the glyph outlines of `text` with the first baseline starting at `origin`. Its outline is rebuilt by
        // setText() / setFontSize() / setFontName(); moving, resizing and rotating the object are remembered (textMatrix()) so
        // editing the words later keeps the object where it was put.
        // An instance of a symbol: its outline is the definition's box carried by `placement` (a four point polygon, so the usual
        // transforms keep working); it paints the symbol's artwork instead of fills and strokes (only the opacity, blend mode and effects apply).
        static SketchObject* makeSymbolInstance(const std::shared_ptr<const SymbolDef>& def, const BLMatrix2D& placement);
        static SketchObject* makeText(const std::string& text, const std::string& fontName, double fontSize, const BLPoint& origin);
        // An object of the given type around an existing outline (what the SVG reader uses).
        static SketchObject* make(SketchObjectType type, const BLPath& path);

        // Builds up an object that is not in a document yet (import, a clone before it is added): the document makes its nodes
        // from the finished object on add(). Once an object is in a document its outline changes only through the document, which
        // also uses these to refresh the object from its nodes.
        struct Prototype {
            static void setOutline(SketchObject& object, const BLPath& path) { object.setGeometry(path); }
            static void transform(SketchObject& object, const BLMatrix2D& matrix) { object.transform(matrix); }
            static void warp(SketchObject& object, const std::function<BLPoint(const BLPoint&)>& map, const PaintWarp& page = PaintWarp()) { object.warp(map, page); }
            static void setPaintMatrix(SketchObject& object, const BLMatrix2D& matrix) { object.setPaintMatrix(matrix); }
            static void setPaintWarp(SketchObject& object, const PaintWarp& warp) { object.setPaintWarp(warp); }
            static void setSymbol(SketchObject& object, const std::shared_ptr<const SymbolDef>& def) { object.setSymbol(def); }
            static void setImage(SketchObject& object, const std::shared_ptr<const ImageData>& image) { object.setImage(image); }
            static void setTextMatrix(SketchObject& object, const BLMatrix2D& matrix) { object.setTextMatrix(matrix); }
            static void setText(SketchObject& object, const std::string& text) { object.setText(text); }
            static void setFontSize(SketchObject& object, double size) { object.setFontSize(size); }
            static void setTextState(SketchObject& object, const std::string& text, const std::string& fontName, double fontSize, const BLMatrix2D& matrix) { object.setTextState(text, fontName, fontSize, matrix); }
            static void setTextAll(SketchObject& object, const std::string& text, const std::string& fontName, double fontSize, const TextStyle& style) { object.setTextAll(text, fontName, fontSize, style); }
            static void setTextStyle(SketchObject& object, const TextStyle& style) { object.setTextStyle(style); }
            static void setTextWarp(SketchObject& object, const TextWarp& warp) { object.setTextWarp(warp); }
        };

        SketchObjectType type() const { return type_; }
        std::size_t id() const { return id_; }
        // Assigned by SketchDocument::add(); 0 means "not in a document".
        void setId(std::size_t id) { id_ = id; }

        // The graph node whose cooked polyline is this object's outline (0 when it is an ordinary object).
        std::uint32_t graphNode() const { return graphNode_; }
        void setGraphNode(std::uint32_t node) { graphNode_ = node; }
        // The scope (network) the node lives in; 0 is the document's root graph.
        std::uint32_t graphScope() const { return graphScope_; }
        NodeRef graphRef() const { return NodeRef(graphScope_, graphNode_); }
        void setGraphRef(const NodeRef& ref) { graphScope_ = ref.scope; graphNode_ = ref.id; }

        const std::string& name() const { return name_; }
        void setName(const std::string& name) { name_ = name; }

        // isVisible() / isLocked() are what the object IS now: its own flag together with those of the layers it is in (a hidden
        // or locked layer hides / locks everything in it, sublayers included). ownVisible() / ownLocked() are the object's own flags,
        // the ones that are saved and that Hide / Lock set. The document keeps the layer part up to date (setLayerState).
        bool isVisible() const { return visible_ && !layerHidden_; }
        bool ownVisible() const { return visible_; }
        void setVisible(bool value) { visible_ = value; }
        bool isLocked() const { return locked_ || layerLocked_; }
        bool ownLocked() const { return locked_; }
        void setLocked(bool value) { locked_ = value; }
        // The layer the object is in (an id of SketchDocument::layers(); 0 = not placed yet - the document puts it in the active layer).
        std::size_t layer() const { return layer_; }
        void setLayer(std::size_t layer) { layer_ = layer; }
        void setLayerState(bool hidden, bool locked) { layerHidden_ = hidden; layerLocked_ = locked; }

        // The appearance stack, bottom to top. One fill and one stroke are "active" (the Appearance panel's selected
        // row of that kind): fill()/stroke()/strokeWidth()/strokeOptions() and their setters - and so the Color and Stroke
        // panels and the gradient / width tools - work on those. setAppearance keeps at least one fill and one stroke
        // (a none-painted one is added when missing).
        const std::vector<AppearanceItem>& appearance() const { return items_; }
        void setAppearance(const std::vector<AppearanceItem>& items);
        std::size_t activeIndex(AppearanceKind kind) const;
        std::size_t activeFillIndex() const { return activeFill_; }
        std::size_t activeStrokeIndex() const { return activeStroke_; }
        std::size_t activeEffectIndex() const { return activeEffect_; }
        // Makes items()[index] the active one of its kind; ignored when index is out of range.
        void setActiveIndex(std::size_t index);
        void setActiveIndices(std::size_t fill, std::size_t stroke, std::size_t effect = 0) { activeFill_ = fill; activeStroke_ = stroke; activeEffect_ = effect; }
        const AppearanceItem& activeItem(AppearanceKind kind) const { return activeItemOf(items_, kind, storedActive(kind)); }
        const SketchPaint& fill() const { return activeItem(AppearanceKind::Fill).paint; }
        void setFill(const SketchPaint& paint) { primary(AppearanceKind::Fill).paint = paint; }
        const SketchPaint& stroke() const { return activeItem(AppearanceKind::Stroke).paint; }
        void setStroke(const SketchPaint& paint) { primary(AppearanceKind::Stroke).paint = paint; }
        double strokeWidth() const { return activeItem(AppearanceKind::Stroke).strokeWidth; }
        void setStrokeWidth(double width);
        const StrokeOptions& strokeOptions() const { return activeItem(AppearanceKind::Stroke).strokeOptions; }
        void setStrokeOptions(const StrokeOptions& options) { primary(AppearanceKind::Stroke).strokeOptions = options; }
        // The active item of the kind, for editing (opacity, blend mode, visibility); null for an effect when there is none.
        AppearanceItem* activeItemPtr(AppearanceKind kind);
        // How far the visible pixel effects (shadow, glow, blur) can reach past bounds(), and bounds() grown by that.
        double effectPad() const;
        BLBox paintBounds() const;

        // Whole-object opacity 0..1 (composited over fill+stroke together).
        double opacity() const { return opacity_; }
        void setOpacity(double opacity);
        // The groups this object is in, outermost first (ids from SketchDocument::newGroupId). Members of a group are
        // adjacent in the document's stacking order; the document keeps it that way. Empty = not grouped.
        const std::vector<std::size_t>& groups() const { return groups_; }
        void setGroups(const std::vector<std::size_t>& groups) { groups_ = groups; }
        // How the whole object (fill and stroke) is blended with what is below it.
        BlendMode blendMode() const { return blendMode_; }
        void setBlendMode(BlendMode mode) { blendMode_ = mode; }

        // Symbol instances only: the definition, and the matrix that carries its artwork (definition space) onto the instance's outline.
        bool isSymbol() const { return type_ == SketchObjectType::Symbol; }
        const std::shared_ptr<const SymbolDef>& symbol() const { return symbol_; }
        BLMatrix2D symbolMatrix() const;

        // Image objects only: the picture, and the matrix that carries its pixels (0,0 .. width,height) onto the object's outline.
        static SketchObject* makeImageObject(const std::shared_ptr<const ImageData>& image, const BLMatrix2D& placement);
        bool isImage() const { return type_ == SketchObjectType::Image; }
        const std::shared_ptr<const ImageData>& image() const { return image_; }
        BLMatrix2D imageMatrix() const;

        // Text objects only (type() == SketchObjectType::Text). The words ('\n' separates lines), the font name ("" = the default
        // font) and size, and the matrix that places the un-transformed outline (baseline at the origin) in the document.
        bool isText() const { return type_ == SketchObjectType::Text; }
        const std::string& text() const { return text_; }
        const std::string& fontName() const { return fontName_; }
        double fontSize() const { return fontSize_; }
        const BLMatrix2D& textMatrix() const { return textMatrix_; }
        // Weight, slant, alignment, tracking and leading (see TextStyle).
        const TextStyle& textStyle() const { return textStyle_; }
        // Text on a path / between two paths. None (the default) is plain text.
        const TextWarp& textWarp() const { return textWarp_; }
        // A point of the text's flat space (x along the first baseline, y down from it) as it is on the page now: through the warp, if any,
        // and the placement. What the editing caret uses.
        BLPoint mapTextPoint(double x, double y) const;
        // The left edge of the text as last set (the warp stretches from there).
        double warpFrameLeft() const { return warpFrame_.x0; }

        const BLPath& geometry() const { return path_; }
        // Counts the edits of the outline; the document's cache of what the nodes cook is valid for one value of it.
        std::uint64_t geometryRevision() const { return geometryRevision_; }
        // True while the object's nodes hold what it is: every edit of the outline clears it, and the document sets it once the nodes were
        // written (or the object was read back from them). A document sets it for a new object when it makes the nodes.
        bool boundToNodes() const { return bound_; }
        // False until the object has been bound once: a loaded or new object whose nodes, if it has any, are the file's.
        bool everBound() const { return everBound_; }
        void setBoundToNodes(bool bound) { bound_ = bound; everBound_ = everBound_ || bound; }
        // A new node chain is about to be made for the object.
        void forgetNodes() { bound_ = everBound_ = false; }
        // The object this one hangs from (0: none). Transforming the parent carries the child along; the child's own transform node holds
        // its placement relative to the parent's.
        std::size_t parent() const { return parent_; }
        void setParent(std::size_t id) { parent_ = id; }
        // The angle (radians) the object's selection frame is turned by - the sum of the rotations
        // applied to it. The outline itself is baked, so this only orients the handles.
        double frameAngle() const { return frameAngle_; }
        void setFrameAngle(double angle) { frameAngle_ = angle; }
        // Bounds of the outline measured in the rotated frame (points un-rotated by -frameAngle()
        // about the origin); equal to geometryBounds() while frameAngle() is 0.
        BLBox frameBounds() const;
        // Paints (gradients, point gradients, patterns) are laid out on frameBounds() and carried to the page by this matrix.
        // The sum of every transform applied to the outline (rotate, scale, shear, flip, move): paints are laid out on the outline as it
        // was before them (paintFrameBounds()) and carried to the page by paintToPage(). A warp is not a matrix, so it is not in it.
        const BLMatrix2D& paintToPage() const { return paintMatrix_; }
        // A perspective warp bends the outline but not the paints, which are not a point list: the paint sits at paintToPage() after this
        // map of the plane it was laid out on, so a gradient or pattern is drawn flat and resampled through it. Identity when never warped.
        const PaintWarp& paintWarp() const { return paintWarp_; }
        // The box the paints are laid out on: the outline as it was before every transform and warp.
        BLBox paintFrameBounds() const;
        // Axis-aligned bounds of the outline alone (no stroke) - what the selection frame wraps.
        BLBox geometryBounds() const;
        // A copy with id 0 (the document assigns one on add) and the same appearance.
        SketchObject* clone() const;
        // A new object of `type` around `path` (id 0) that wears this object's look: appearance, name, opacity, blend mode, visibility,
        // lock and groups. What the cutting tools and the Pathfinder commands make their pieces with. The frame angle starts at 0
        // (the outline is new); use a type other than Text, which keeps its words and placement.
        SketchObject* cloneWithGeometry(SketchObjectType type, const BLPath& path) const;
        // Closed shapes (everything except Line/Polyline) take a fill; open ones ignore it.
        bool isClosed() const;

        // Axis-aligned bounds of the geometry plus half the stroke width.
        BLBox bounds() const;
        // The same for an outline other than the object's own (the one its graph nodes make).
        BLBox boundsOf(const BLPath& outline) const;

        // The point and unit "left" normal at fraction t (0..1 of the length) along the first subpath, flattened;
        // false when the outline has no subpath. The sides of a WidthPoint are offsets along +normal (left) and -normal.
        bool pathSample(double t, BLPoint& point, BLPoint& normal) const;
        // The fraction along the first subpath nearest to `point` and how far away it is (document units).
        bool nearestOnPath(const BLPoint& point, double& t, double& distance) const;

        // True when point (document units) is on the fill or within tolerance of the stroke.
        // `outline` replaces the object's own path when given: the document passes the one its graph nodes make.
        bool hitTest(const BLPoint& point, double tolerance, const BLPath* outline = nullptr) const;

        // Fills and strokes in stacking order; with an object opacity below 1 or a blend mode the whole object is drawn
        // through paintIsolated().
        void paint(BLContext& ctx) const;

    private:
        // The outline and its paint frame change only through the document, which writes the same edit to the object's nodes.
        friend class SketchDocument;
        // Replaces the outline (undo restores the pre-edit one through this).
        void setGeometry(const BLPath& path) { path_ = path; touchGeometry(); }
        void touchGeometry() { ++geometryRevision_; bound_ = false; }
        // Bakes a transform into the outline - move/resize/rotate all go through here, so stroke width
        // stays put and handles are always axis-aligned on geometryBounds().
        void transform(const BLMatrix2D& matrix);
        void setPaintMatrix(const BLMatrix2D& matrix) { paintMatrix_ = matrix; }
        void setPaintWarp(const PaintWarp& warp) { paintWarp_ = warp; }
        void setSymbol(const std::shared_ptr<const SymbolDef>& def) { symbol_ = def; }
        void setImage(const std::shared_ptr<const ImageData>& image) { image_ = image; }
        // Puts the placement back (undo of a move / resize restores the outline and, through this, where edits of the words land).
        void setTextMatrix(const BLMatrix2D& matrix) { textMatrix_ = matrix; }
        // Each of these rebuilds the outline from the text state; setTextState also replaces the placement matrix.
        void setText(const std::string& text);
        void setFontName(const std::string& fontName);
        void setFontSize(double size);
        void setTextState(const std::string& text, const std::string& fontName, double fontSize, const BLMatrix2D& matrix);
        void setTextStyle(const TextStyle& style);
        void setTextWarp(const TextWarp& warp);
        // Everything that shapes the words at once (one rebuild) - what undo of a text change restores.
        void setTextAll(const std::string& text, const std::string& fontName, double fontSize, const TextStyle& style);
        // Moves every outline vertex (curve control points included) through `map` - how a perspective
        // warp is applied. Control points are mapped, not re-fitted, so strong distortion of a curve is approximate.
        // `page` is the projective map `map` stands for (page to page), which the paints follow.
        void warp(const std::function<BLPoint(const BLPoint&)>& map, const PaintWarp& page = PaintWarp());
        SketchObject(SketchObjectType type, const BLPath& path);
        // The appearance items only (their own opacity and blend mode, no object-level ones).
        void paintItems(BLContext& ctx) const;
        // Everything through an offscreen layer so the pixel effects can act on what is below them.
        void paintWithEffects(BLContext& ctx) const;
        void drawItem(BLContext& ctx, std::size_t index, const BLBox& box) const;
    public:
        // What an appearance item is drawn on: the outline after the geometry effects (round corners, offset path, roughen) that stand
        // above it in the stack.
        BLPath itemOutline(std::size_t index) const;
        // A stroke item as a filled outline (plain, dashed or with a width profile; a brush is not handled here). False when nothing is drawn.
        bool strokeOutline(std::size_t index, BLPath& out) const;
        // True when some visible effect works on pixels (shadow, glow, blur): it cannot be expressed as an outline.
        bool hasPixelEffects() const;
    private:
        void drawSymbol(BLContext& ctx) const;
        void drawImage(BLContext& ctx) const;
        // Text objects: path_ = the outlines of text_ placed by textMatrix_.
        void rebuildText();
        std::size_t storedActive(AppearanceKind kind) const { return kind == AppearanceKind::Fill ? activeFill_ : (kind == AppearanceKind::Stroke ? activeStroke_ : activeEffect_); }
        // The active item of the kind (the first one, added when missing: fills at the bottom, strokes on top).
        AppearanceItem& primary(AppearanceKind kind);

        SketchObjectType type_;
        BLPath path_;
        std::size_t id_ = 0;
        std::uint32_t graphNode_ = 0;
        std::uint32_t graphScope_ = 0;
        std::uint64_t geometryRevision_ = 1;
        bool bound_ = false;
        bool everBound_ = false;
        std::size_t parent_ = 0;
        std::string name_;
        bool visible_ = true;
        bool locked_ = false;
        std::size_t layer_ = 0;
        bool layerHidden_ = false;   // a layer it is in is hidden / locked (kept by the document)
        bool layerLocked_ = false;
        std::vector<AppearanceItem> items_ = {AppearanceItem::makeFill(SketchPaint::solid(SketchColor::gray(1.0f))),
            AppearanceItem::makeStroke(SketchPaint::solid(SketchColor::gray(0.0f)), 1.0)};
        double opacity_ = 1.0;
        BlendMode blendMode_ = BlendMode::Normal;
        std::vector<std::size_t> groups_;
        std::size_t activeFill_ = 0;    // indices into items_ (validated on use)
        std::size_t activeStroke_ = 1;
        std::size_t activeEffect_ = 0;
        double frameAngle_ = 0.0;
        BLMatrix2D paintMatrix_ = BLMatrix2D::make_identity();
        PaintWarp paintWarp_;
        std::string text_;
        std::string fontName_;
        double fontSize_ = 24.0;
        BLMatrix2D textMatrix_ = BLMatrix2D::make_identity();
        TextStyle textStyle_;
        TextWarp textWarp_;
        BLBox warpFrame_ = BLBox(0, 0, 1, 1);   // x0, y0 = top, x1, y1 = bottom of the text as last set, for the warp
        std::shared_ptr<const SymbolDef> symbol_;
        std::shared_ptr<const ImageData> image_;
    };

}
