#pragma once

#include "curvy/cursorset.h"
#include "curvy/shapetools.h"
#include "curvy/sketchdocument.h"

#include <newui/subview.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace curvy {

    // What a left-button drag on the canvas does. The order matches the status bar hints in main.cpp.
    enum class SketchTool { Select, DirectSelect, Rectangle, Ellipse, Circle, RoundRect, Polygon, Star, Arc, Line, Pen, Pencil, Paintbrush, Text, Gradient, Width, Scissors, Knife, Eraser, ShapeBuilder, Eyedropper, MagicWand, Lasso, Artboard, Rotate, Scale, Reflect, Hand, Zoom };

    // The drawing surface: the gray pasteboard, the white page with its grid, and (later) the
    // document's objects, the selection overlay and the rulers - all painted by this one view,
    // since a ruler is meaningless without the canvas's zoom/pan. Sketch's own "CanvasView
    // (custom SubView)" from demos/sketch/mockup/sketch-mockup.html.
    //
    // S0 scaffold: paints the pasteboard, page and grid only; SketchDocument/tools (PLAN.md S1/S2)
    // hook in here. The properties below are what a .newui file may set (see sketch.newui).
    // @reflect category=basic
    class CanvasView : public newui::SubView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        CanvasView();
        virtual ~CanvasView() {}

        // Page size in document units (points). A US Letter portrait page by default.
        float pageWidth() const { return pageWidth_; }
        void setPageWidth(float width);
        float pageHeight() const { return pageHeight_; }
        void setPageHeight(float height);

        // 1.0 == 100%. Always positive.
        float zoom() const { return zoom_; }
        void setZoom(float zoom);

        // The colour around the artboards (0xAARRGGBB).
        std::uint32_t pasteboardColor() const { return pasteboardColor_; }
        void setPasteboardColor(std::uint32_t argb);
        // False paints only the pasteboard (the workspace with nothing open).
        bool showPages() const { return showPages_; }
        void setShowPages(bool value);

        bool showGrid() const { return showGrid_; }
        void setShowGrid(bool value);

        bool showRulers() const { return showRulers_; }
        void setShowRulers(bool value);

        // Grid spacing in document units.
        float gridSpacing() const { return gridSpacing_; }
        void setGridSpacing(float spacing);

        // Draw and move drags jump to the grid when within a few pixels of a grid line (like Illustrator's
        // snap-to-point; the grid need not be visible).
        bool snapToGrid() const { return snapToGrid_; }
        void setSnapToGrid(bool value);

        // The document painted on the page; null paints an empty page. Not owned. The view
        // registers itself with the document (Model::addView) so edits repaint it.
        // @reflect ignore=true
        SketchDocument* document() const { return document_; }
        // Clipboard (the selection travels as SVG text, so it also pastes into other editors). Paste lands 12 pt
        // further down-right each time it is repeated; inPlace keeps the original position.
        // @reflect ignore=true
        bool copySelection();
        // @reflect ignore=true
        bool cutSelection();
        // @reflect ignore=true
        bool paste(bool inPlace);
        // @reflect ignore=true
        void setDocument(SketchDocument* document);

        // Where the page's top-left sits, in view pixels measured from the top-left of the
        // drawing area (just inside the rulers).
        float panX() const { return panX_; }
        float panY() const { return panY_; }
        void setPan(float x, float y);

        // Sets zoom so the document point under view point `anchor` stays put (wheel zoom).
        // @reflect ignore=true
        void zoomAt(const newui::Point& anchor, float newZoom);

        // View pixels <-> document units (points), for the current zoom and pan.
        // @reflect ignore=true
        BLPoint toDocument(const newui::Point& viewPoint) const;
        // @reflect ignore=true
        BLPoint toView(const BLPoint& documentPoint) const;

        // Zoom so the whole page fits the drawing area with a small margin, and re-center it.
        void zoomToFit();
        // Zoom so the active artboard fills the drawing area, and centre it.
        void fitActiveArtboard();

        // The active tool. Select: click/shift-click/marquee, drag to move, handles to resize/rotate.
        // Rectangle/Ellipse/Line: drag out a shape (Shift = square/circle/45 degrees). Hand: drag pans.
        // Gradient: drag across the selected object(s) to lay a gradient axis (a solid fill becomes a linear
        // gradient; Shift = 45 degrees); drag the axis ends and stops of a selected gradient; click the axis to
        // add a stop, Alt-click a stop to remove it.
        // @reflect ignore=true
        SketchTool tool() const { return tool_; }
        // @reflect ignore=true
        void setTool(SketchTool tool);
        newui::Delegate<CanvasView> onToolChanged;

        // Rounded Rectangle / Polygon / Star settings, changed with the Up / Down arrows while dragging one out.
        double cornerRadius() const { return cornerRadius_; }
        void setCornerRadius(double radius);
        int polygonSides() const { return polygonSides_; }
        void setPolygonSides(int sides);
        int starPointCount() const { return starPoints_; }
        void setStarPoints(int points);
        double arcSweep() const { return arcSweep_; }
        void setArcSweep(double radians);
        // Guides: lines dragged out of the rulers that things snap to (the document keeps them). Dragging a guide with the Select tool moves it
        // (back onto a ruler deletes it) unless guides are locked. Smart Guides: snapping to the edges and centers of the other objects.
        bool showGuides() const { return showGuides_; }
        void setShowGuides(bool value);
        bool guidesLocked() const { return guidesLocked_; }
        void setGuidesLocked(bool value);
        bool smartGuides() const { return smartGuides_; }
        void setSmartGuides(bool value);
        // The Eraser's pen width in document units (the Up / Down arrows change it by 2 while that tool is chosen).
        double eraserSize() const { return eraserSize_; }
        void setEraserSize(double size);

        // The cursor shown over the canvas, by the name of its image in Resources/cursors ("pen", "resize-nwse", "hand-grab"...;
        // the hotspots are in cursors.json). It follows the tool, what is under the pointer and what a drag is doing.
        // @reflect ignore=true
        const std::string& cursorName() const { return cursorName_; }

        // The pointer over the page, in document units (status bar coordinates); fired on every move.
        BLPoint pointerPosition() const { return pointerDoc_; }
        newui::Delegate<CanvasView> onPointerMoved;

        // Which paint the Gradient tool edits: the fill (default) or the stroke - follows the Color panel's target.
        bool gradientOnStroke() const { return gradientOnStroke_; }
        void setGradientOnStroke(bool stroke);
        // The gradient stop the tool has selected (highlighted on the axis; the Color panel edits the same one).
        // Setting it from outside does not fire onGradientStopSelected.
        std::size_t selectedGradientStop() const { return gradientSelectedStop_; }
        void setSelectedGradientStop(std::size_t index);
        newui::Delegate<CanvasView> onGradientStopSelected;

        // Fired whenever zoom() changes (wheel, setZoom, zoomToFit) - the zoom combo and status
        // bar follow it.
        typedef newui::Delegate<CanvasView> ViewChangedDelegate;
        ViewChangedDelegate onZoomChanged;

        // Ctrl+<key> shortcuts the canvas does not own (File menu: N/O/S); return true when handled.
        // @reflect ignore=true
        std::function<bool(std::uint32_t vkCode, bool shift)> onControlKey;
        // Asked before the canvas looks at a press / a key: true means the workspace (rings, Space-pan, hold-key scrubbing) used it.
        // @reflect ignore=true
        std::function<bool(const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys)> onPressFilter;
        // @reflect ignore=true
        std::function<bool(std::uint32_t keyMask, std::uint32_t vkCode)> onKeyFilter;
        // Painted last, over the rulers, in the canvas's own coordinates (the workspace's scrub card and hover chip).
        // @reflect ignore=true
        std::function<void(BLContext& ctx)> onPaintTop;

        // The Text tool (texttool.cpp): a click starts text there - or edits the text object clicked - and typing fills it in
        // on the canvas; Enter starts a new line, the arrows / Home / End / Backspace / Delete edit, Ctrl+Up / Ctrl+Down change the
        // size, Esc (or a click elsewhere, or another tool) finishes it as one undo step. Text left empty is not kept.
        // @reflect ignore=true
        bool textEditing() const { return textEditing_; }
        // @reflect ignore=true
        const std::string& editedText() const { return textBuffer_; }
        // The size (points) new text gets, and the size of the text being edited.
        double textSize() const { return textSize_; }
        void setTextSize(double size);
        // Finishes editing text now (nothing happens when none is being edited).
        void commitText();

        void paint(BLContext& ctx) override;

        // Width on the left that a dock keeps clear: the rulers and the drawing area start to its right.
        float leftInset() const { return leftInset_; }
        void setLeftInset(float inset) { leftInset_ = inset; style().markDirty(); }
        // The thickness of the rulers (0 when hidden): what the docks leave clear.
        float rulerSize() const { return showRulers_ ? kRulerThickness : 0.0f; }

    private:
        int pasteCount_ = 0;
        static constexpr float kRulerThickness = 20.0f;
        // Cursor images are shown at this fraction of the size cursors.json describes (32 px drawn, 19 px shown). The environment variable SKETCH_CURSOR_SCALE (for example 0.6) overrides it, to try sizes without rebuilding.
        static constexpr double kCursorScale = 0.585;
        static constexpr float kMinZoom = 0.05f;
        static constexpr float kMaxZoom = 64.0f;

        void paintRulers(BLContext& ctx, const newui::Rect& client) const;

        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleKeyDown(newui::View& sender, std::uint32_t keyMask, int keyChar, int repeatCount, std::uint32_t vkCode);
        newui::SyncReturn handleKeyPress(newui::View& sender, std::uint32_t keyMask, int keyChar, int repeatCount, std::uint32_t vkCode);
        newui::SyncReturn handleMouseDblClick(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseWheel(newui::View& sender, const newui::Point& pt, float delta);

        std::uint32_t pasteboardColor_ = 0xFF7A7A7Au;
        bool showPages_ = true;
        float pageWidth_ = 612.0f;
        float pageHeight_ = 792.0f;
        float zoom_ = 1.0f;
        float gridSpacing_ = 20.0f;
        bool showGrid_ = true;
        bool showRulers_ = true;
        float leftInset_ = 0.0f;
        bool snapToGrid_ = true;
        BLPoint pointerDoc_;
        BLBox dragBounds_;  // the selection's bounds when a move began
        // ---- Guides and snapping (guides.cpp) ----
        // `value` (an x for axis 0, a y for axis 1) moved onto the nearest grid line, guide or - Smart Guides - object edge / center within
        // a few pixels. `line` receives the object line it caught (NaN when none), which the overlay draws in magenta.
        double snapAxis(int axis, double value, double* line) const;
        double snapValue(double value, int axis) const;
        BLPoint snapPoint(const BLPoint& point) const;
        void rememberSnapLines(double x, double y) const;
        struct SnapLine {
            double position;
            bool guide;   // a ruler guide (always snapped to) rather than an object's edge
        };
        void collectSnapLines() const;
        int guideAt(const newui::Point& pt) const;
        bool beginGuideDrag(const newui::Point& pt);
        void updateGuideDrag(const newui::Point& pt);
        void finishGuideDrag(const newui::Point& pt);
        void paintGuides(BLContext& ctx) const;
        bool showGuides_ = true;
        bool guidesLocked_ = false;
        bool smartGuides_ = true;
        mutable bool snapCacheValid_ = false;           // rebuilt on the first snap after a press
        mutable std::vector<SnapLine> snapX_, snapY_;
        mutable std::vector<double> smartX_, smartY_;   // the object lines the last snap caught
        int guideDragIndex_ = -1;                       // the guide being dragged (-1: a new one)
        bool guideDragVertical_ = false;
        double guideDragPosition_ = 0.0;
        bool centered_ = true;  // cleared by the first manual pan/zoom
        float panX_ = 20.0f;
        float panY_ = 20.0f;
        SketchDocument* document_ = nullptr;
        bool panning_ = false;
        newui::Point panAnchor_;
        float panStartX_ = 0.0f;
        float panStartY_ = 0.0f;

        // ---- Tool interaction ----
        enum class Drag { None, Move, Resize, Rotate, Shear, Warp, Origin, Marquee, Draw, GradientNew, GradientEnd, GradientStop, Width, Pencil, PenHandle, DirectEdit, LiveShape, Knife, Erase, ShapeBuild, Lasso, ArtboardNew, ArtboardMove, ArtboardResize, Guide, TransformTool };
        // Handles 0..7 clockwise from the top-left corner; 8 is the rotate knob above top-center.
        // 9 is the transform-origin marker.
        static constexpr int kRotateHandle = 8;
        static constexpr int kOriginHandle = 9;
        static constexpr double kHandleHit = 6.0;
        static constexpr double kRotateKnobOffset = 22.0;

        // Handle position in view pixels (the rotate knob sits kRotateKnobOffset px out along the frame's "up").
        BLPoint handlePosition(int handle, const SelectionFrame& frame) const;
        int hitHandle(const newui::Point& viewPoint) const;
        // Alt+Shift on a corner starts a perspective warp, on a middle handle a shear; on an already warped
        // (quad) frame every handle edits the quad. The origin marker moves the pivot.
        void beginResize(int handle, std::uint32_t keys);
        void updateDrag(const newui::Point& pt, std::uint32_t keys);
        void finishDrag(const newui::Point& pt, std::uint32_t keys);
        // Re-applies `matrix` to the pre-drag outlines; the frame angles become their old values + angleDelta.
        void applyToSelection(const BLMatrix2D& matrix, double angleDelta = 0.0);
        void applyMapToSelection(const std::function<BLPoint(const BLPoint&)>& map, const QuadFrame& newQuad);
        void paintOverlay(BLContext& ctx) const;
        // Chooses the cursor from the tool, an active pan / drag, or (Select tool) the handle under the pointer - resize arrows
        // that follow the frame's turn, the rotate knob, the pivot - and shows it. Cheap: nothing happens unless the name changes.
        void refreshCursor(const newui::Point& pt, std::uint32_t keys);
        void setCanvasCursor(const std::string& name);
        static const char* toolCursorName(SketchTool tool);

        std::string cursorName_;
        std::map<std::string, CursorHotspot> cursorHotspots_;
        bool cursorHotspotsLoaded_ = false;
        newui::Point lastPointer_;

        // ---- Shape, pencil and pen tools (canvastools.cpp) ----
        bool isShapeTool() const {
            return tool_ == SketchTool::RoundRect || tool_ == SketchTool::Polygon || tool_ == SketchTool::Star || tool_ == SketchTool::Circle || tool_ == SketchTool::Arc;
        }
        // The outline a drag from dragStartDoc_ to `to` makes (document units); `valid` is false while it is too small.
        BLPath shapeOutline(const BLPoint& to, bool shift, bool& valid) const;
        void finishShapeDraw(bool shift);
        void paintShapePreview(BLContext& ctx) const;
        // Up / Down while dragging: more / fewer sides, points or a bigger / smaller corner radius. True when it applied.
        bool adjustShapeParameter(int direction);
        // Adds a drawn object with the document's default fill and stroke as one undo step; it becomes the selection.
        void addDrawn(SketchObject* object, const char* name, const char* undoName, bool filled = true);
        void pencilMove(const BLPoint& point);
        void finishPencil();
        void paintPencilPreview(BLContext& ctx) const;
        bool penBegin(const newui::Point& pt, std::uint32_t keys);
        void penDrag(const BLPoint& pointerDoc, bool alt);
        void finishPen(bool close);
        void cancelPen();
        void penUndoLast();
        void paintPenOverlay(BLContext& ctx) const;

        // ---- Text tool (texttool.cpp) ----
        struct TextSnapshot {
            std::string text;
            std::string font;
            double size = 24.0;
            BLMatrix2D matrix = BLMatrix2D::make_identity();
        };
        void beginText(const newui::Point& pt);
        SketchObject* textObjectAt(const BLPoint& doc) const;
        SketchObject* textObject() const;           // the object being edited (the draft while it is new)
        std::size_t textIndexAt(const BLPoint& doc) const;
        void refreshText();
        void textInsert(const std::string& utf8);
        bool textKeyDown(std::uint32_t keyMask, std::uint32_t vkCode);
        void paintTextEditor(BLContext& ctx) const;
        bool textEditing_ = false;
        std::size_t textId_ = 0;                    // the document object being edited; 0 while the text is new
        std::unique_ptr<SketchObject> textDraft_;   // new text, added to the document when finished
        TextSnapshot textBefore_;                   // an existing object as it was when editing began (for undo)
        std::string textBuffer_;                    // UTF-8
        std::size_t textCaret_ = 0;                 // byte index into textBuffer_
        double textSize_ = 24.0;
        unsigned long textHighSurrogate_ = 0;

        // ---- Scissors, Knife and Eraser (cuttools.cpp) ----
        // The canvas only collects the pointer's path (cutPoints_) and shows it; SketchDocument::scissorsCut / knifeCut / erase do the cutting.
        void cutMove(const BLPoint& point);
        void finishKnife();
        void finishErase();
        void paintCutOverlay(BLContext& ctx) const;
        std::vector<BLPoint> cutPoints_;   // the Knife's or Eraser's path so far, in document units
        double eraserSize_ = 14.0;

        // ---- Shape Builder (cuttools.cpp) ----
        // At the press the selected shapes are cut into cells (SketchDocument::shapeBuilderModel); the drag collects the cells it passes
        // over (shapeHit_, shown tinted) and the release joins them into one shape - or, with Alt, erases them.
        void shapeBuildMove(const BLPoint& point);
        void finishShapeBuild();
        void paintShapeBuilder(BLContext& ctx) const;
        // The Lasso collects its freehand path in cutPoints_ and selects what it touches when let go (Shift keeps the selection).
        void paintLasso(BLContext& ctx) const;

        // The Artboard tool (artboardtool.cpp): the artboard being made / moved / resized is kept in artboardLive_ and only goes to the
        // document when the pointer is let go.
        int artboardHandleAt(const BLPoint& point) const;
        void artboardPress(const BLPoint& point);
        void artboardMove(const BLPoint& point, std::uint32_t keys);
        void finishArtboard(Drag kind);
        void paintArtboardTool(BLContext& ctx) const;
        std::size_t artboardIndex_ = 0;
        Artboard artboardStart_;
        Artboard artboardLive_;
        SketchDocument::ShapeBuilderModel shapeModel_;
        std::vector<bool> shapeHit_;
        bool shapeErase_ = false;

        // ---- Rotate, Scale and Reflect (transformtools.cpp) ----
        // All three work about the selection's transform origin (the marker in its frame): a click sets it, a drag from anywhere turns
        // (Shift = 15 degree steps), scales (uniformly; Shift = each axis on its own) or mirrors (the axis follows the pointer round the
        // origin; Shift = 45 degree steps) the selection live. Alt when let go leaves the original and makes a transformed copy.
        bool isTransformTool() const { return tool_ == SketchTool::Rotate || tool_ == SketchTool::Scale || tool_ == SketchTool::Reflect; }
        void beginTransformTool();
        void updateTransformTool(std::uint32_t keys);
        void finishTransformTool(std::uint32_t keys);
        void paintTransformTool(BLContext& ctx) const;
        // Puts the selection at `matrix` applied to its state at the press; frame angles become angleScale * angle + angleOffset.
        void applyTransformTool(const BLMatrix2D& matrix, double angleScale, double angleOffset);
        double toolAngleScale_ = 1.0;
        double toolAngleOffset_ = 0.0;

        // ---- Direct Select (canvastools.cpp) ----
        // Shows the anchors of the selected objects; a click picks an anchor, a drag moves it (or a handle of the picked
        // anchor); Alt-dragging a corner anchor pulls curve handles out of it; Delete removes the picked anchor.
        struct DirectRef {
            std::size_t object = 0;   // the object's id
            std::size_t sub = 0;      // which outline of it
            std::size_t anchor = 0;
            int part = 0;             // 0 the anchor, 1 its in-handle, 2 its out-handle, 3 pulling both handles out
            bool valid = false;
        };
        bool directHit(const newui::Point& pt, DirectRef& out) const;
        void directMouseDown(const newui::Point& pt, std::uint32_t keys);
        void directDrag(const BLPoint& pointerDoc, bool alt);
        void finishDirectDrag();
        bool deleteDirectAnchor();
        void paintDirectOverlay(BLContext& ctx) const;
        DirectRef directSelected_;           // the picked anchor
        DirectRef directDragRef_;            // what the drag in progress moves
        std::vector<SubPath> directPaths_;   // the outlines being edited (the object's geometry is rebuilt from them)
        PenAnchor directOriginal_;           // the anchor as it was when the drag began

        // ---- Live shape handles (canvastools.cpp) ----
        // A single selected star, polygon, rounded rectangle or arc shows extra handles (see mockup/live-shapes.html): the
        // number of points / sides, the star inner radius, the corner radius, the two ends of an arc. The shape is recognised
        // from its outline each time (shapetools.h), so nothing extra is stored and it also works for files and shapes made
        // before - a shape that was squashed or edited into something irregular simply has none.
        enum class LiveKind { None, RoundRect, Polygon, Star, Arc };
        struct LiveShape {
            LiveKind kind = LiveKind::None;
            std::size_t object = 0;
            RegularShape regular;   // polygon, star
            RoundRectShape rect;    // rounded rectangle
            ArcShape arc;           // arc
        };
        enum LiveHandle { kLiveNone = 0, kLivePoints = 1, kLiveInner = 2, kLiveRadius = 3, kLiveArcStart = 4, kLiveArcEnd = 5 };
        bool liveShape(LiveShape& out) const;
        // Where a handle sits, in view pixels.
        BLPoint liveHandlePoint(const LiveShape& shape, int handle) const;
        int liveHandleAt(const newui::Point& pt, const LiveShape& shape) const;
        // The cursor that shows which way the handle is dragged: the points handle tracks sideways motion (a left-right arrow); the
        // inner-radius and corner handles track the line they slide along (an arrow along it); an arc end tracks motion around
        // the center (an arrow along the tangent). All follow the shape turn.
        const char* liveHandleCursor(const LiveShape& shape, int handle) const;
        void beginLiveDrag(int handle, const LiveShape& shape, const newui::Point& pt);
        void updateLiveDrag(const BLPoint& pointerDoc, const newui::Point& pointerView);
        void finishLiveDrag();
        void paintLiveOverlay(BLContext& ctx) const;
        LiveShape liveStart_;               // the shape when the drag began
        int liveHandle_ = kLiveNone;
        float liveStartX_ = 0.0f;           // the pointer x (view pixels) when a points drag began
        std::string liveWhat_;              // undo step name
        std::string liveLabel_;             // the value shown next to the handle while dragging

        double arcSweep_ = 1.5707963267948966;   // the sweep a new arc gets (a quarter turn); Up / Down change it while dragging
        double cornerRadius_ = 12.0;
        int polygonSides_ = 5;
        int starPoints_ = 5;
        bool dragShift_ = false;               // Shift was down at the last drag update (the preview follows it)
        std::vector<BLPoint> pencilPoints_;    // the freehand points so far
        std::vector<PenAnchor> penAnchors_;    // the pen path being built (empty when none)

        // ---- Gradient tool ----
        // The tool edits the fill or the stroke paint of the selection.
        const SketchPaint& gradientPaint(const SketchObject& object) const { return gradientOnStroke_ ? object.stroke() : object.fill(); }
        const SketchPaint& gradientPaint(const StyleState& state) const { return gradientOnStroke_ ? state.stroke() : state.fill(); }
        void setGradientPaint(SketchObject& object, const SketchPaint& paint) const {
            if (gradientOnStroke_) { object.setStroke(paint); } else { object.setFill(paint); }
        }
        // The handle that reshapes a radial gradient (view pixels): where its across radius ends - out from the
        // center, across the axis, leaning by `skew`. Dragging it sets the aspect and the lean together.
        static BLPoint aspectHandle(const BLPoint& center, const BLPoint& end, double aspect, double skew);
        // The handle that tilts a linear gradient's lines of equal color: a fixed distance from the middle of the
        // axis along the direction those lines run.
        static BLPoint tiltHandle(const BLPoint& start, const BLPoint& end, double skew);
        // The one selected object whose fill is a gradient (the tool's handles belong to it), or null.
        SketchObject* gradientTarget() const;
        // The target's gradient axis in view pixels (linear: start..end; radial: center..a point on the circle).
        bool gradientAxisView(const SketchObject& object, BLPoint& start, BLPoint& end) const;
        // Mouse-down with the gradient tool: grabs an axis end or stop of the target, adds a stop on its axis,
        // or starts dragging out a new axis on the (selected / clicked) objects.
        void beginGradientDrag(const newui::Point& pt, std::uint32_t keys);
        void updateGradientDrag(std::uint32_t keys);
        void finishGradientDrag();
        void cancelGradientDrag();
        void paintGradientOverlay(BLContext& ctx) const;

        bool gradientOnStroke_ = false;
        std::size_t gradientSelectedStop_ = 0;
        std::vector<StyleState> gradientBefore_;  // fills when a gradient drag began
        int gradientEnd_ = 0;                     // GradientEnd: 0 = start / center, 1 = end / radius point
        std::size_t gradientStop_ = 0;            // GradientStop: index of the stop being moved
        std::string gradientWhat_;                // undo step name

        // ---- Width tool ----
        // Edits the variable-width profile (StrokeOptions::widths) of the one selected object. Each width point shows
        // as a diamond on the path with a square handle out on each side: dragging a handle widens both sides
        // (Alt: that side only), dragging the diamond slides the point along the path (Alt-click deletes it), and
        // dragging the path itself adds a point there.
        enum class WidthMode { Slide, Both, Left, Right };
        SketchObject* widthTarget() const;
        // View-pixel positions of a width point's diamond and its two side handles.
        bool widthHandles(const SketchObject& object, const WidthPoint& point, BLPoint& center, BLPoint& left, BLPoint& right) const;
        void beginWidthDrag(const newui::Point& pt, std::uint32_t keys);
        void updateWidthDrag(std::uint32_t keys);
        void finishWidthDrag();
        void cancelWidthDrag();
        void paintWidthOverlay(BLContext& ctx) const;
        void setWidths(const std::vector<WidthPoint>& widths);

        std::vector<StyleState> widthBefore_;  // styles when a width drag began
        WidthMode widthMode_ = WidthMode::Both;
        std::size_t widthIndex_ = 0;           // the point being dragged
        std::size_t widthObject_ = 0;          // its object's id
        std::string widthWhat_;                // undo step name

        SketchTool tool_ = SketchTool::Select;
        Drag drag_ = Drag::None;
        BLPoint dragStartDoc_;
        BLPoint dragNowDoc_;
        newui::Point dragNowView_;
        std::vector<GeometryState> dragBefore_;
        BLMatrix2D dragMatrix_ = BLMatrix2D::make_identity();   // the last matrix applyToSelection() applied (what Transform Again repeats)
        double dragAngle_ = 0.0;
        FrameState dragFrameBefore_;  // the document group angle / quad frame when a drag began
        SelectionFrame dragFrame_;  // selection frame when a resize/rotate began
        int dragHandle_ = 0;
        double rotateStartAngle_ = 0.0;
        BLPoint dragOrigin_;     // transform origin (document units) when a rotate/shear began
        BLPoint warpQuad_[4];    // the distorted frame corners (TL, TR, BR, BL) while warping
        bool dragMoved_ = false;
        bool dragAdditive_ = false;
    };

}
