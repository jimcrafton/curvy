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

    // @reflect ignore=true
    struct GraphBrushInfo {
        std::string type;        // the asset's node type
        std::string path;        // the file it was read from
        double referenceWidth = 1.0;   // the stroke width its dots were tuned for
        bool simulation = false;       // has settleTime
        double settleTime = 0.0;
    };

    // Registers every `*.asset.json` file of `folder` (sorted by name) as a node type and returns the ones that are brushes. A file that
    // does not load is skipped and its reason added to `problems` (when given). Calling it again re-reads the folder; instances already in a
    // graph keep the definition they were built from until jitnodes' updateAssetInstances. A missing folder gives an empty list.
    std::vector<GraphBrushInfo> loadBrushAssets(const std::string& folder, std::vector<std::string>* problems = nullptr);

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
    GraphBrushCook cookGraphBrush(const std::string& type, const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths);

    // Called (from a worker thread) whenever a background cook has finished; the host posts a repaint to its UI thread. Until one is set,
    // cookGraphBrushLater cooks on the calling thread.
    void setGraphBrushReadyHandler(std::function<void()> handler);

    // The same as cookGraphBrush for painting on the UI thread: a cached cook is returned at once, otherwise the chunks of the stroke are
    // queued for the worker threads, the result is empty with `pending` set, and the ready handler is called when it can be drawn.
    // Long strokes are cooked in chunks of about 24 guide units (in parallel), so nothing blocks the editor while they are made.
    GraphBrushCook cookGraphBrushLater(const std::string& type, const BLPath& path, double strokeWidth, const std::vector<WidthPoint>& widths,
        bool* pending = nullptr);

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

    // The same result as outlines with their own fills, in page coordinates, for Expand and the PDF writer (a dot is a circle).
    std::vector<BrushPart> graphBrushParts(const GraphBrushCook& cook);

}
