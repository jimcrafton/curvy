#pragma once

#include "curvy/guides.h"
#include "curvy/rulers.h"
#include "curvy/rulerview.h"
#include "curvy/shapepaths.h"
#include "curvy/sketchdocument.h"

#include <newui/subview.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace curvy {

    // The drawing surface. It paints the pasteboard, the document's page and the document's cooked artwork at its zoom and pan (the wheel pans,
    // Ctrl+wheel zooms at the pointer); the hooks let the workspace see presses and keys first.
    // @reflect category=basic
    class CanvasView : public newui::SubView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        CanvasView();
        ~CanvasView() override = default;

        // The document whose page is painted; null paints the pasteboard only. Not owned. The owner registers the canvas with the
        // document (Model::addView) so edits repaint it.
        // @reflect ignore=true
        void setDocument(SketchDocument* document);
        // @reflect ignore=true
        const SketchDocument* document() const { return document_; }

        // Where the page's top-left sits in the view, in pixels from the view's own top-left, and how big a document unit (a point) is on
        // screen. Until the first pan or zoom by hand the page stays centred as the view is resized.
        float zoom() const { return zoom_; }
        void setZoom(float zoom);
        float panX() const { return panX_; }
        float panY() const { return panY_; }
        void setPan(float x, float y);
        void panBy(float dx, float dy) { setPan(panX_ + dx, panY_ + dy); }
        // True until the first pan or zoom by hand (or after zoomToFit): the page is centred, so there is no pan worth remembering.
        bool centered() const { return centered_; }
        // Sets zoom so the document point under view point `anchor` stays put (wheel zoom).
        // @reflect ignore=true
        void zoomAt(const newui::Point& anchor, float newZoom);
        // Zoom so the whole page fits with a margin (no bigger than `largest`), and centre it again.
        void zoomToFit(float largest = kMaxZoom);

        // View pixels <-> document units (points), for the current zoom and pan.
        // @reflect ignore=true
        BLPoint toDocument(const newui::Point& viewPoint) const;
        // @reflect ignore=true
        BLPoint toView(const BLPoint& documentPoint) const;

        // The topmost shape of the cooked artwork under a view point: inside its fill, or within `tolerancePx` screen pixels of its stroke.
        // Nothing outside the page is hit (the artwork is clipped to it).
        // The key in the hit names the node that made the shape (see GraphDocument::nodeForShapeKey).
        // @reflect ignore=true
        std::optional<ShapeHit> shapeAt(const newui::Point& viewPoint, float tolerancePx = 3.0f) const;

        // Draws the document's cooked artwork with the page's top-left at (originX, originY) and `scale` pixels per point, clipped to the page.
        // @reflect ignore=true
        // `dimmed` (optional) names the shapes drawn faded, by key: what lies outside the group that is being edited.
        static void renderArtworkAtScale(BLContext& ctx, SketchDocument& document, double originX, double originY, double scale,
                                         const std::function<bool(const std::string&)>& dimmed = {});

        // The rulers along the top and left edge (in the document's unit, zero at the page's top left) and the document's guides, painted over
        // the artwork. Visible rulers cover a strip of Rulers::kThickness pixels of the view.
        bool rulersVisible() const { return rulersVisible_; }
        void setRulersVisible(bool visible);
        // The pointer's place on the canvas (its own pixels), shown as a marker on both rulers; only the rulers repaint.
        // @reflect ignore=true
        void setPointer(const newui::Point& viewPoint);
        // @reflect ignore=true
        Rulers::Region rulerAt(const newui::Point& viewPoint) const;
        // The guide near a view point (within `tolerancePx`; the rulers' own strips never hit one): the nearest, the last added on a tie. None
        // when there are no guides, they are hidden, or no document is open.
        // @reflect ignore=true
        std::optional<std::size_t> guideAt(const newui::Point& viewPoint, float tolerancePx = 4.0f) const;
        // A coordinate (points; an x when `vertical`) pulled onto the nearest guide of that axis when one lies within `tolerancePx` on screen,
        // else returned as it is. The one entry point that snapping goes through; hidden guides do not snap.
        // @reflect ignore=true
        double snap(double value, bool vertical, float tolerancePx = 6.0f) const;
        // A guide being dragged, drawn in place of guide `replaces` (or as an extra when it is empty) without touching the document, so a drag
        // is one undo step when it ends. Cleared by passing nothing.
        struct GuidePreview {
            std::optional<std::size_t> replaces;
            Guide guide;
        };
        // @reflect ignore=true
        void setGuidePreview(const std::optional<GuidePreview>& preview);
        // The crosshair shown while the ruler origin is dragged out of the corner (document points); nothing clears it.
        // @reflect ignore=true
        void setOriginPreview(const std::optional<BLPoint>& at);

        static constexpr float kMinZoom = 0.05f;
        static constexpr float kMaxZoom = 64.0f;
        // Fired whenever zoom() changes.
        typedef newui::Delegate<CanvasView> ViewChangedDelegate;
        ViewChangedDelegate onZoomChanged;

        std::uint32_t pasteboardColor() const { return pasteboardColor_; }
        void setPasteboardColor(std::uint32_t argb);

        // Return true when the press / key was used.
        std::function<bool(const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys)> onPressFilter;
        std::function<bool(const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys)> onDoubleClickFilter;
        std::function<bool(std::uint32_t keyMask, std::uint32_t vkCode)> onKeyFilter;
        // Painted over everything the canvas draws.
        std::function<void(BLContext& ctx)> onPaintTop;
        std::function<bool(const std::string& key)> shapeDimmed;

        void paint(BLContext& ctx) override;

    private:
        void paintPage(BLContext& ctx, const newui::Rect& client) const;
        void paintGuides(BLContext& ctx, const newui::Rect& client) const;
        void updateRulers();
        // Draws the document's cooked artwork: each primitive as a path, filled and stroked from its fill.* / stroke.* attributes, clipped to the page.
        void renderArtwork(BLContext& ctx, const newui::Rect& client);
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseDoubleClick(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleKeyDown(newui::View& sender, std::uint32_t keyMask, int keyChar, int repeatCount, std::uint32_t vkCode);

        void centre(const newui::Rect& client);
        newui::SyncReturn handleMouseWheel(newui::View& sender, const newui::Point& pt, float delta);

        SketchDocument* document_ = nullptr;
        float zoom_ = 1.0f;
        float panX_ = 0.0f, panY_ = 0.0f;
        bool centered_ = true;
        RulerView* topRuler_ = nullptr;    // children, owned by the view tree
        RulerView* leftRuler_ = nullptr;
        bool rulersVisible_ = true;
        std::optional<GuidePreview> guidePreview_;
        std::optional<BLPoint> originPreview_;
        std::uint32_t pasteboardColor_ = 0xFF2A2A2A;
    };

}
