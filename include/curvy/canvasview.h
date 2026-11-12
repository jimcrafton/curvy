#pragma once

#include "curvy/sketchdocument.h"

#include <newui/subview.h>

#include <cstdint>
#include <functional>

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

        // Draws the document's cooked artwork with the page's top-left at (originX, originY) and `scale` pixels per point, clipped to the page.
        // @reflect ignore=true
        static void renderArtworkAtScale(BLContext& ctx, SketchDocument& document, double originX, double originY, double scale);

        static constexpr float kMinZoom = 0.05f;
        static constexpr float kMaxZoom = 64.0f;
        // Fired whenever zoom() changes.
        typedef newui::Delegate<CanvasView> ViewChangedDelegate;
        ViewChangedDelegate onZoomChanged;

        std::uint32_t pasteboardColor() const { return pasteboardColor_; }
        void setPasteboardColor(std::uint32_t argb);

        // Return true when the press / key was used.
        std::function<bool(const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys)> onPressFilter;
        std::function<bool(std::uint32_t keyMask, std::uint32_t vkCode)> onKeyFilter;
        // Painted over everything the canvas draws.
        std::function<void(BLContext& ctx)> onPaintTop;

        void paint(BLContext& ctx) override;

    private:
        void paintPage(BLContext& ctx, const newui::Rect& client) const;
        // Draws the document's cooked artwork: each primitive as a path, filled and stroked from its fill.* / stroke.* attributes, clipped to the page.
        void renderArtwork(BLContext& ctx, const newui::Rect& client);
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleKeyDown(newui::View& sender, std::uint32_t keyMask, int keyChar, int repeatCount, std::uint32_t vkCode);

        void centre(const newui::Rect& client);
        newui::SyncReturn handleMouseWheel(newui::View& sender, const newui::Point& pt, float delta);

        SketchDocument* document_ = nullptr;
        float zoom_ = 1.0f;
        float panX_ = 0.0f, panY_ = 0.0f;
        bool centered_ = true;
        std::uint32_t pasteboardColor_ = 0xFF2A2A2A;
    };

}
