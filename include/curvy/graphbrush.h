#pragma once

#include "curvy/brush.h"
#include "curvy/sketchobject.h"

#include <blend2d/blend2d.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace curvy {

    // A brush made of a jitnodes asset (docs/graph-brushes.md): input 0 is the guide (the stroke path with the pen pressure in `w`), the
    // promoted floats are the settings and the output is dots or polygons. An asset is a brush when it promotes both of these floats.
    inline constexpr const char* kBrushReferenceWidthParam = "referenceWidth";
    inline constexpr const char* kBrushGuideLengthParam = "guideLength";
    // Optional: the distance along the whole stroke at which the guide starts. A long stroke is cooked in chunks, and an asset whose look
    // depends on the distance travelled (running dry, a colour drift) adds this to its own measure so the chunks join up.
    inline constexpr const char* kBrushGuideStartParam = "guideStart";
    // Optional: the length of the whole stroke (its subpath), in guide units. A brush with a paint load (dryness, colour drift) stretches the load
    // over the stroke when the stroke is longer than the load, so a long stroke is painted to its end instead of drying out halfway.
    inline constexpr const char* kBrushStrokeLengthParam = "strokeLength";
    // Optional: the box of the scaled guide, for a brush that needs room around the stroke (a gas grid). The asset derives its own bounds from it.
    inline constexpr const char* kBrushGuideMinXParam = "guideMinX";
    inline constexpr const char* kBrushGuideMinYParam = "guideMinY";
    inline constexpr const char* kBrushGuideMaxXParam = "guideMaxX";
    inline constexpr const char* kBrushGuideMaxYParam = "guideMaxY";
    // Optional: marks a simulation brush. The asset is cooked at this time (seconds of simulation after the stroke is done) and, because a
    // simulation cannot be cut into pieces, a subpath is one chunk.
    inline constexpr const char* kBrushSettleTimeParam = "settleTime";
    // Optional, and a setting like any other (the asset reads it through an expression too). 1: the asset outputs shading (white = full colour, darker
    // = shade) and the host multiplies it by the stroke's paint; 0: the asset paints in its own colours and the stroke's colour is ignored.
    inline constexpr const char* kBrushTintableParam = "tintable";
    // Optional, and an ordinary setting (the asset only has to promote it, it need not read it). The fraction of the stroke, counted from its end,
    // over which the host thins the guide's pressure to a tenth, for a stroke without pen pressure; 0 is off. A stroke with a width profile is left alone.
    inline constexpr const char* kBrushTailTaperParam = "tailTaper";
    // The names above that the host sets; every other promoted float is a setting the user can change.
    bool isHostBrushParam(const std::string& name);

    // A promoted float of the asset that the user may change (not one of the host-set names above).
    // @reflect ignore=true
    struct GraphBrushSetting {
        std::string name;
        std::string label;        // "dryStart" -> "Dry start"
        double defaultValue = 0.0;
    };
    // Changed settings by name; see StrokeOptions::graphSettings.
    using GraphBrushSettings = std::vector<std::pair<std::string, double>>;

    // @reflect ignore=true
    struct GraphBrushInfo {
        std::vector<GraphBrushSetting> settings;
        std::string type;        // the asset's node type
        std::string path;        // the file it was read from
        double referenceWidth = 1.0;   // the stroke width its dots were tuned for
        bool hasTintable = false;      // has tintable
        double tintableDefault = 0.0;
        bool simulation = false;       // has settleTime
        double settleTime = 0.0;
    };

    // Registers every `*.asset.json` file of `folder` (sorted by name) as a node type and returns the ones that are brushes. A file that
    // does not load is skipped and its reason added to `problems` (when given). Calling it again re-reads the folder; instances already in a
    // graph keep the definition they were built from until jitnodes' updateAssetInstances. A missing folder gives an empty list.
    std::vector<GraphBrushInfo> loadBrushAssets(const std::string& folder, std::vector<std::string>* problems = nullptr);

    // Makes usable the graph brushes a document brings along: an asset type that is registered (a graph's embedded definitions are registered
    // when it is read) but is not in the brush folder is added as a brush, once per type. Not in graphBrushes() (that is the folder), but found by
    // findGraphBrush, so a document made with an asset you do not have still cooks its strokes. Called when a document's graph is read.
    void registerEmbeddedBrushes();

    // The brushes found by the latest loadBrushAssets, and the one of a given type (null when it is not a brush).
    const std::vector<GraphBrushInfo>& graphBrushes();
    const GraphBrushInfo* findGraphBrush(const std::string& type);

    // What a graph brush paints in page coordinates: round dots (drawn in order, later over earlier) and filled polygons.
    // @reflect ignore=true
    struct GraphDot {
        double x = 0.0;
        double y = 0.0;
        double radius = 0.0;
        std::uint32_t color = 0;   // BLRgba32 value
        float softness = 0.0f;     // 0 is a hard edge; 1 fades from the centre (the jitnodes renderSplats profile)
    };
    // @reflect ignore=true
    struct GraphPolygon {
        BLPath path;
        std::uint32_t color = 0;
    };
    // What one cook made, in coordinates relative to the start of the stroke (GraphBrushCook::origin), so moving a stroke needs no new cook.
    // @reflect ignore=true
    struct GraphBrushResult {
        std::vector<GraphDot> dots;
        std::vector<GraphPolygon> polygons;
        BLBox bounds = BLBox(0, 0, 0, 0);
        // The shapes drawn once into an image at one scale (a stroke has tens of thousands of dots, too many to fill on every repaint); redrawn
        // when the view scale outgrows it. Only the UI thread paints, so this is not locked.
        struct Raster {
            BLImage image;
            BLBox box = BLBox(0, 0, 0, 0);   // what the image covers, in the same coordinates as the shapes
            double scale = 0.0;              // pixels per unit
        };
        mutable Raster raster;
    };

    // The shapes plus where the stroke starts on the page.
    // @reflect ignore=true
    struct GraphBrushCook {
        std::shared_ptr<const GraphBrushResult> shapes;
        BLPoint origin = BLPoint(0, 0);
        explicit operator bool() const { return shapes != nullptr; }
    };

    // Runs the asset `type` on a stroke: every subpath goes in as the guide, moved to the origin and divided by k = strokeWidth / referenceWidth (so
    // the brush keeps the proportions it was tuned for at any weight), with the pen pressure (`widths`, 1 without) as `w`; what comes out is scaled
    // back by k. Results are cached by the shape of the path (not where it is), weight, pressure and asset revision, so painting or moving a stroke
    // again costs nothing. Empty when the asset is not loaded or nothing could be cooked.
    // `settings` overrides promoted floats of the asset (a name the asset does not have is ignored); they are part of the cache key.
    GraphBrushCook cookGraphBrush(const std::string& type, const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths,
        const GraphBrushSettings& settings = {});

    // Called (from a worker thread) whenever a background cook has finished; the host posts a repaint to its UI thread. Until one is set,
    // cookGraphBrushLater cooks on the calling thread.
    // The handler is also called when a stroke is queued and when each of its chunks is done, so a status display can follow graphBrushProgress().
    void setGraphBrushReadyHandler(std::function<void()> handler);

    // How far the background cooks have got: chunks done of chunks queued since the queue was last empty; strokes is how many strokes are waiting.
    struct GraphBrushProgress {
        std::size_t done = 0;
        std::size_t total = 0;
        std::size_t strokes = 0;
        double fraction = 0.0;   // 0..1 over all queued chunks, counting how far the running ones have got (a simulation brush moves smoothly)
        bool busy() const { return strokes > 0; }
    };
    GraphBrushProgress graphBrushProgress();

    // Stops the queued chunks and tells the running ones to stop at their next node or simulation step. The strokes concerned stay hairlines
    // until they are edited (any edit is a new cook); nothing partial is kept.
    void cancelGraphBrushCooks();

    // The same as cookGraphBrush for painting on the UI thread: a cached cook is returned at once, otherwise the chunks of the stroke are
    // queued for the worker threads, the result is empty with `pending` set, and the ready handler is called when it can be drawn.
    // Long strokes are cooked in chunks of about 24 guide units (in parallel), so nothing blocks the editor while they are made.
    GraphBrushCook cookGraphBrushLater(const std::string& type, const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths,
        bool* pending = nullptr, const GraphBrushSettings& settings = {});

    // While one of these lives on a thread, cookGraphBrushLater on it waits for the cook instead of queuing it: for painting that is saved
    // (image export) rather than shown.
    class GraphBrushBlockingScope {
    public:
        GraphBrushBlockingScope();
        ~GraphBrushBlockingScope();
        GraphBrushBlockingScope(const GraphBrushBlockingScope&) = delete;
        GraphBrushBlockingScope& operator=(const GraphBrushBlockingScope&) = delete;
    };

    // Draws a cook on the context at the scale it is drawn at: from the cached image, which is made again when the scale has grown past it.
    void paintGraphBrush(BLContext& ctx, const GraphBrushCook& cook);

    // Whether a brush of this asset, with these settings, is tinted by the stroke's paint (the asset has `tintable` and it is 1 or more).
    bool graphBrushTinted(const std::string& type, const GraphBrushSettings& settings);

    // A tinted cook: the shading is drawn, the paint set by `applyPaint` (false: nothing to paint with) is multiplied over it, and what is outside
    // the shapes is cleared again. Needs a context that holds only this stroke (an isolated layer).
    void paintGraphBrushTinted(BLContext& ctx, const GraphBrushCook& cook, const std::function<bool(BLContext&)>& applyPaint);

    // The same result as outlines with their own fills, in page coordinates, for Expand and the PDF writer (a dot is a circle).
    std::vector<BrushPart> graphBrushParts(const GraphBrushCook& cook);
    // The same for a stroke with `paint`: a tinted brush takes a solid paint's colour and opacity (a gradient or pattern is not applied here).
    std::vector<BrushPart> graphBrushParts(const GraphBrushCook& cook, const StrokeOptions& options, const SketchPaint& paint);

}
