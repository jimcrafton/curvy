#pragma once

#include "curvy/appearancenode.h"
#include "curvy/brush.h"
#include "curvy/graphdocument.h"
#include "curvy/pathops.h"
#include "curvy/sketchobject.h"

#include <newui/delegate.h>
#include <newui/models.h>
#include <newui/undostack.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace curvy {

    // What shapes a text object's words. With no matrix the object keeps the placement it has.
    // @reflect ignore=true
    struct TextState {
        std::string text, font;
        double size = 0.0;
        TextStyle style;
        std::optional<BLMatrix2D> matrix;
        bool sameWords(const TextState& o) const { return text == o.text && font == o.font && size == o.size && style == o.style; }
    };

    // An object's outline and frame angle at one moment - what undo of a move/resize/rotate restores.
    // @reflect ignore=true
    struct GeometryState {
        std::size_t id = 0;
        BLPath path;
        double angle = 0.0;
        BLMatrix2D textMatrix = BLMatrix2D::make_identity();   // a Text object's placement (see SketchObject::textMatrix)
        BLMatrix2D paintMatrix = BLMatrix2D::make_identity();  // where its paints sit (see SketchObject::paintToPage)
        PaintWarp paintWarp;                                   // and how a perspective warp bent them (see SketchObject::paintWarp)
    };

    // One object's part in a placement edit: `from` is where it started, `matrix` carries that to where it ends up, and its frame angle
    // becomes angleScale * from.angle + angleOffset.
    // @reflect ignore=true
    struct ObjectPlacement {
        const GeometryState* from = nullptr;
        BLMatrix2D matrix = BLMatrix2D::make_identity();
        double angleScale = 1.0;
        double angleOffset = 0.0;
    };

    // Four world-space corners (TL, TR, BR, BL) of a possibly distorted frame, with the projective map
    // between the unit square and the quad.
    // @reflect ignore=true
    struct QuadFrame {
        BLPoint p[4];

        BLPoint map(double u, double v) const;
        // Inverse of map(); false when the quad is degenerate.
        bool unmap(const BLPoint& world, double& u, double& v) const;
        QuadFrame transformed(const BLMatrix2D& matrix) const;
    };

    struct TraceResult;   // imagetrace.h

    // The colors a new document's swatch list starts with (hex text, "#rrggbb").
    std::vector<std::string> defaultSwatches();

    // A ruler guide: the vertical line x = position, or the horizontal line y = position (document units).
    // @reflect ignore=true
    struct Guide {
        bool vertical = false;
        double position = 0.0;
        bool operator==(const Guide& o) const { return vertical == o.vertical && position == o.position; }
    };

    // A layer: a named bucket of objects that can be hidden or locked as a whole and that holds sublayers. Layers form a tree
    // (parent 0 = top level); the document keeps them in `layers()` with siblings in stacking order, back first. A layer paints its
    // sublayers (in order) and then its own objects on top of them.
    // @reflect ignore=true
    struct Layer {
        std::size_t id = 0;
        std::size_t parent = 0;
        std::string name;
        bool visible = true;
        bool locked = false;
        bool expanded = true;   // shown opened in the Layers panel (view state, not saved)
        // The layer colour as 0xRRGGBB (0 = automatic: one of nine by the layer id). Its objects show their selection in it and the
        // panel marks the layer with it.
        std::uint32_t color = 0;
    };
    // The colours layers get by default (the nine Illustrator uses), and the colour a layer shows: its own, else one of those.
    constexpr std::uint32_t kLayerColors[9] = {0x4F80FF, 0xFF4F4F, 0x4FC24F, 0x3355FF, 0xE5C200, 0x9B4FFF, 0xFF8A2B, 0x1F7A1F, 0xE0309B};
    inline std::uint32_t layerColor(const Layer& layer) { return layer.color != 0 ? layer.color : kLayerColors[(layer.id > 0 ? layer.id - 1 : 0) % 9]; }

    // Opacity and blend mode of a whole group, applied to its members drawn together.
    // One artboard: a named rectangle of the canvas that is exported and aligned to on its own. A document always has at least one;
    // the first starts at the origin. The active one is "the page" (pageWidth / pageHeight, Align, Image Trace, export).
    // @reflect ignore=true
    struct Artboard {
        std::string name;
        double x = 0.0, y = 0.0, width = 612.0, height = 792.0;
        BLBox box() const { return BLBox(x, y, x + width, y + height); }
        bool contains(const BLPoint& p) const { return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height; }
        bool operator==(const Artboard& o) const { return name == o.name && x == o.x && y == o.y && width == o.width && height == o.height; }
    };

    // Opacity and blend mode of a whole group, applied to its members drawn together.
    // @reflect ignore=true
    struct GroupProps {
        double opacity = 1.0;
        BlendMode blendMode = BlendMode::Normal;
        // A clipping mask: the group's topmost object is the mask, and the rest show only inside its outline.
        bool clip = false;
        bool isDefault() const { return opacity >= 1.0 && blendMode == BlendMode::Normal && !clip; }
    };

    // A named appearance the user saved: the whole stack (fills, strokes, effects) plus the object's opacity and blend
    // mode. Applying one replaces those on every selected object. Gradients are stored relative to the object they paint,
    // so a style looks the same on a shape of any size.
    // @reflect ignore=true
    struct GraphicStyle {
        std::string name;
        std::vector<AppearanceItem> items;
        double opacity = 1.0;
        BlendMode blendMode = BlendMode::Normal;
        bool operator==(const GraphicStyle& o) const { return name == o.name && items == o.items && opacity == o.opacity && blendMode == o.blendMode; }
    };
    // The styles a new document starts with.
    std::vector<GraphicStyle> defaultGraphicStyles();

    // An object's appearance at one moment - what undo of a style edit restores.
    // @reflect ignore=true
    struct StyleState {
        std::size_t id = 0;
        std::vector<AppearanceItem> items;
        double opacity = 1.0;
        BlendMode blendMode = BlendMode::Normal;
        std::size_t activeFill = 0;    // the active items (not part of ==: choosing a row is not an edit)
        std::size_t activeStroke = 1;
        std::size_t activeEffect = 0;
        // The object's outermost group (0 = none) and that group's props, so a group edit undoes with the objects.
        std::size_t group = 0;
        GroupProps groupProps;

        static StyleState of(const SketchObject& object) {
            return StyleState{object.id(), object.appearance(), object.opacity(), object.blendMode(), object.activeFillIndex(), object.activeStrokeIndex(), object.activeEffectIndex(), 0, GroupProps()};
        }
        void applyTo(SketchObject& object) const {
            object.setAppearance(items);
            object.setActiveIndices(activeFill, activeStroke, activeEffect);
            object.setOpacity(opacity);
            object.setBlendMode(blendMode);
        }
        // The active fill / stroke and its options (what the Color / Stroke panels edit).
        const SketchPaint& fill() const { return activeItemOf(items, AppearanceKind::Fill, activeFill).paint; }
        const SketchPaint& stroke() const { return activeItemOf(items, AppearanceKind::Stroke, activeStroke).paint; }
        const StrokeOptions& strokeOptions() const { return activeItemOf(items, AppearanceKind::Stroke, activeStroke).strokeOptions; }
        bool operator==(const StyleState& o) const { return id == o.id && items == o.items && opacity == o.opacity && blendMode == o.blendMode && group == o.group &&
            groupProps.opacity == o.groupProps.opacity && groupProps.blendMode == o.groupProps.blendMode; }
        bool operator!=(const StyleState& o) const { return !(*this == o); }
    };

    // The oriented box around the selection: `local` is measured in a frame turned by `angle`
    // (local = R(-angle) * world), so handles follow a rotated shape the way Illustrator's do.
    // @reflect ignore=true
    struct SelectionFrame {
        double angle = 0.0;
        BLBox local = BLBox(0, 0, 0, 0);

        BLPoint toWorld(double lx, double ly) const;
        BLPoint toLocal(const BLPoint& world) const;
        BLPoint center() const { return toWorld((local.x0 + local.x1) * 0.5, (local.y0 + local.y1) * 0.5); }

        // Set after a warp: the frame is then this quad instead of the rectangle above.
        std::optional<QuadFrame> quad;

        // The four corners as a quad (the rectangle's, or the warped one).
        QuadFrame asQuad() const;
        // World point at fraction (u, v) of the frame (0,0 = top-left, 1,1 = bottom-right) and its inverse.
        BLPoint pointAt(double u, double v) const { return asQuad().map(u, v); }
        bool unmapPoint(const BLPoint& world, double& u, double& v) const { return asQuad().unmap(world, u, v); }
    };

    // The selection-level frame state that an undo step must restore along with the outlines.
    // @reflect ignore=true
    struct FrameState {
        std::optional<double> groupAngle;
        std::optional<QuadFrame> quad;
    };

    // The document: a page (size in points) and a flat, back-to-front list of objects. Layers and
    // groups arrive with S3b; undoable commands with S2 - until then the mutators below are called
    // directly and each ends with changed(), which marks the document modified and repaints its views.
    // File I/O is SVG (sketchsvg.h): load()/save() go through readFromFile/writeToFile.
    class SketchDocument : public GraphDocument {
    public:
        SketchDocument() = default;

        // ---- Graphic Styles (saved with the document, see sketchsvg.cpp) ----
        const std::vector<GraphicStyle>& graphicStyles() const { return graphicStyles_; }
        // Replaces the whole list with no undo step (a load, New). Does not mark the document modified.
        void setGraphicStyles(std::vector<GraphicStyle> styles) { graphicStyles_ = std::move(styles); onChanged(*this); }
        // Each of these is one undo step and returns false when nothing happened.
        // Saves the first selected object's appearance as a new style ("Style 1", "Style 2"...), at the end of the list.
        bool addGraphicStyleFromSelection();
        bool deleteGraphicStyle(std::size_t index);
        // Replaces the appearance, opacity and blend mode of every selected object with the style's.
        bool applyGraphicStyle(std::size_t index);

        // Page size in points (1/72 in) - the size of the active artboard - and the pixel density used when exporting/previewing.
        float pageWidth() const { return float(artboards_[activeArtboard_].width); }
        float pageHeight() const { return float(artboards_[activeArtboard_].height); }
        void setPageSize(float width, float height);

        // ---- Artboards (documentartboards.cpp) ----
        // Never empty. Edits are undo steps; setActiveArtboard is not (it is a view of the document, like the selection).
        const std::vector<Artboard>& artboards() const { return artboards_; }
        std::size_t activeArtboard() const { return activeArtboard_; }
        const Artboard& activeArtboardRect() const { return artboards_[activeArtboard_]; }
        void setActiveArtboard(std::size_t index);
        // The box round all the artboards.
        BLBox artboardsBounds() const;
        // The topmost (last) artboard containing the point, or -1.
        int artboardAt(const BLPoint& point) const;
        // A name for a new artboard ("Artboard 3").
        std::string nextArtboardName() const;
        // Adds an artboard (it becomes the active one) and returns its index; the size is made at least 1 x 1.
        std::size_t addArtboard(Artboard artboard);
        // Adds one the size of the active artboard, to the right of all the others with a gap.
        std::size_t addArtboardBesideLast();
        // Removes an artboard; false for the last remaining one or an index out of range.
        bool deleteArtboard(std::size_t index);
        // Moves / resizes / renames an artboard; false when nothing changes.
        bool setArtboard(std::size_t index, const Artboard& artboard);
        // Replaces the whole list (loading a file); keeps `active` in range. Not an undo step.
        void setArtboards(std::vector<Artboard> artboards, std::size_t active);
        float ppi() const { return ppi_; }
        void setPpi(float ppi);
        // The page's fill: white by default; nullopt = transparent (shown as a checkerboard, written without a
        // background rectangle). Not undoable.
        const std::optional<SketchColor>& pageBackground() const { return pageBackground_; }
        void setPageBackground(const std::optional<SketchColor>& color);

        std::size_t objectCount() const { return objects_.size(); }
        // Back-to-front: index 0 is painted first. Throws std::runtime_error when out of range.
        SketchObject& object(std::size_t index) const;

        // Takes ownership, assigns a fresh id, appends on top. Returns the object for chaining.
        SketchObject& add(SketchObject* object);
        // Removes and destroys the object with this id; false if there is none.
        bool remove(std::size_t id);
        SketchObject* findById(std::size_t id) const;

        // Topmost visible, unlocked object under point (document units), or nullptr.
        SketchObject* hitTest(const BLPoint& point, double tolerance) const;
        // ---- Point tags: named groups of an object's points, kept by a tag node between its shape and placement nodes ----
        // The tag the canvas adds points to (a name, "tag1" to start with).
        const std::string& activeTag() const { return activeTag_; }
        void setActiveTag(const std::string& name) { activeTag_ = name.empty() ? "tag1" : name; }
        // The object's tag node for `group`; made (and wired in just before the placement node) when `create` is set. invalidNodeId when none.
        jitnodes::NodeId tagNodeOf(std::size_t objectId, const std::string& group, bool create);
        // The ids (SketchObject points' `id` attribute) the tag lists.
        std::vector<int> taggedIds(std::size_t objectId, const std::string& group) const;
        // Adds the ids to the tag (`on`) or takes them away, as one undo step. False when nothing changed.
        bool tagPoints(std::size_t objectId, const std::string& group, const std::vector<int>& ids, bool on);
        // Empties the tag (removes its node), as one undo step.
        bool clearTag(std::size_t objectId, const std::string& group);
        // The id of the anchor of the object nearest `world` within `reach` (document units); -1 when none.
        int pointIdNear(std::size_t objectId, const BLPoint& world, double reach) const;
        // Where the anchors tagged in `group` are on the page.
        std::vector<BLPoint> taggedPositions(std::size_t objectId, const std::string& group) const;
        // The names of the tags the object has.
        std::vector<std::string> tagNames(std::size_t objectId) const;
        // Adds a move-points node for `group` (every point when empty) just before the object's placement node, as one undo step, and returns it.
        // The object is driven from then on: it shows what its chain cooks (see kMovePointsNodeType).
        jitnodes::NodeId addMovePoints(std::size_t objectId, const std::string& group);

        // ---- Nodes: what is selected and picked is the placement (transform) node of a shape ----
        // The outline the graph makes for the object (its shape node's curve carried by the page matrix of its transform node); the object's own
        // path when it has no nodes yet. Picking and the marquee read this; the object's cached bounds only rule out the far objects first.
        BLPath placedOutline(const SketchObject& object) const;
        // The bounding box of placedOutline(); what the selection box and frame are measured on.
        BLBox outlineBounds(const SketchObject& object) const;
        // The object a shape, placement or appearance node belongs to (0 for any other node).
        std::size_t objectOfNode(jitnodes::NodeId node) const { return objectOfNode(NodeRef(node)); }
        std::size_t objectOfNode(const NodeRef& node) const;
        // The node of the topmost visible, unlocked object under the point (invalidNodeId when none).
        NodeRef hitTestNode(const BLPoint& point, double tolerance) const;
        // The placement nodes of the selected objects, in selection order.
        std::vector<NodeRef> selectedNodes() const;
        // Selects the objects the nodes belong to (a node of no object is ignored).
        void setSelectedNodes(const std::vector<NodeRef>& nodes);

        // ---- Groups ----
        // A group is a set of objects sharing a group id in SketchObject::groups() (nested groups: several ids), kept
        // adjacent in the stacking order. Selecting one member selects the whole outermost group (the canvas does it
        // through withGroupMembers; Ctrl-click bypasses it to pick a single member).
        std::size_t newGroupId() { return nextGroupId_++; }
        // Membership accessors: the one place that knows where an object's groups and layer are stored. Code outside the loaders
        // reads and writes membership through these, so moving the storage into the graph (DG-04 S3/S4) changes only their bodies.
        // The chain is outermost first; the reference is valid until the next membership edit.
        const std::vector<std::size_t>& groupsOf(const SketchObject& object) const { return object.groups(); }
        std::size_t outermostGroupOf(const SketchObject& object) const { return object.groups().empty() ? 0 : object.groups().front(); }
        bool isGrouped(const SketchObject& object) const { return !object.groups().empty(); }
        bool inGroup(const SketchObject& object, std::size_t group) const;
        std::size_t layerOf(const SketchObject& object) const { return object.layer(); }
        void setGroupsOf(SketchObject& object, const std::vector<std::size_t>& groups) { object.setGroups(groups); }
        void setLayerOf(SketchObject& object, std::size_t layer) { object.setLayer(layer); }
        // Where an object's nodes live as a file stores it: the path of subnet ids from the root ("4/7"; empty for the root scope), and back
        // (the file's graph is loaded first, so the path names its subnets).
        std::string scopeText(std::uint32_t scope) const;
        std::uint32_t scopeOfText(const std::string& text);
        // The group id a scope's subnet stands for; 0 for the root and for a subnet that is not a document group.
        std::size_t groupIdOfScope(std::uint32_t scope) const;
        // Group opacity / blend mode (default: opaque, normal). Setting is not an undo step by itself - the Appearance panel
        // edits live and commits with commitStyle(), whose snapshots carry the group's props.
        GroupProps groupProps(std::size_t groupId) const;
        void setGroupProps(std::size_t groupId, const GroupProps& props);
        // The outermost group the selection is exactly (all its members and nothing else), else 0.
        std::size_t selectedGroup() const;
        void setSelectionGroupOpacity(double opacity);
        void setSelectionGroupBlendMode(BlendMode mode);
        // `ids` plus every visible, unlocked object sharing an outermost group with one of them.
        std::vector<std::size_t> withGroupMembers(const std::vector<std::size_t>& ids) const;
        // Groups the selection (two or more objects) into a new outermost group, placed where its topmost member was;
        // undoable. False when fewer than two objects are selected.
        bool groupSelection(bool clip = false);
        // Dissolves the outermost group of each selected object (the objects stay selected); undoable. False when none is grouped.
        bool ungroupSelection();
        bool canGroup() const { return selection_.size() >= 2; }
        // Clipping masks (documentclip.cpp): a group flagged `clip` shows its members only inside the outline of its topmost member (the
        // mask, whose own fill and stroke are not drawn). Make groups the selection (or flags the selected group) so; the topmost selected
        // object is the mask. Release clears the flag and keeps the group. Both undoable.
        bool makeClippingMask();
        bool releaseClippingMask();
        bool canMakeClippingMask() const;
        bool canReleaseClippingMask() const;
        // The mask object of a clipping group (the topmost member), or null when the group is not a clipping group.
        const SketchObject* clipMaskOf(std::size_t groupId) const;
        bool canUngroup() const;

        // ---- Symbols (documentsymbols.cpp) ----
        // The library: artwork defined once and placed as Symbol objects (SketchObject::isSymbol). Making, placing, redefining, breaking
        // the link, renaming and deleting are undo steps (undoing "Make Symbol" leaves the definition in the library).
        const std::vector<std::shared_ptr<const SymbolDef>>& symbols() const { return symbols_; }
        std::shared_ptr<const SymbolDef> findSymbol(std::size_t id) const;
        // The symbol / pattern the Libraries panel and the Object menu act on (view state, not an undo step): the one chosen in the panel,
        // else the one last made, else the first; 0 when the library is empty.
        std::size_t activeSymbol() const;
        void setActiveSymbol(std::size_t id);
        std::size_t activePattern() const;
        void setActivePattern(std::size_t id);
        // Places the active symbol in the middle of the active artboard.
        std::size_t placeActiveSymbol();
        // How many objects are instances of the symbol.
        std::size_t symbolUseCount(std::size_t id) const;
        // Replaces the library (loading a file); not an undo step.
        void setSymbols(std::vector<std::shared_ptr<const SymbolDef>> symbols);
        // The selection becomes a new symbol (named `name`, "Symbol N" when empty) and is replaced by one instance of it. Returns the id, 0
        // when nothing is selected.
        std::size_t makeSymbol(const std::string& name);
        // A new instance centred on `center`; returns its id (0 for an unknown symbol). The instance becomes the selection.
        std::size_t placeSymbol(std::size_t symbolId, const BLPoint& center);
        // The selection becomes the new artwork of the symbol; every instance follows, keeping its place. False when nothing is selected or
        // the selection holds an instance of the symbol itself.
        bool redefineSymbol(std::size_t symbolId);
        // Selected instances are replaced by copies of their artwork (as ordinary objects). False when none is selected.
        bool breakSymbolLink();
        bool canBreakSymbolLink() const;
        bool renameSymbol(std::size_t symbolId, const std::string& name);
        // Removes a symbol nothing uses; false when it is used or unknown.
        bool deleteSymbol(std::size_t symbolId);
        // `def` (from another document) as a member of this library: the entry that already is the same, else a copy under a fresh id.
        std::shared_ptr<const SymbolDef> importSymbol(const std::shared_ptr<const SymbolDef>& def);

        // ---- Patterns (documentsymbols.cpp): tiles of artwork that repeat to fill a shape (SketchPaint::pattern). ----
        // The pattern library works like the symbol library but holds the tiles. Making one copies the selection (which stays); undoing
        // "Make Pattern" removes it again. Applying sets the fill (or stroke) of every selected object.
        const std::vector<std::shared_ptr<const SymbolDef>>& patterns() const { return patterns_; }
        std::shared_ptr<const SymbolDef> findPattern(std::size_t id) const;
        std::size_t patternUseCount(std::size_t id) const;
        // Replaces the library (loading a file); not an undo step. Pattern paints that only know their id are bound to it.
        void setPatterns(std::vector<std::shared_ptr<const SymbolDef>> patterns);
        // Points every pattern paint of the document (objects and graphic styles) that has an id but no tile at the library's tile.
        void bindPatternPaints();
        // The selection, its bounds as the tile, becomes a pattern ("Pattern N" when `name` is empty); returns the id, 0 when nothing is selected.
        std::size_t makePattern(const std::string& name);
        // The fill (or stroke) of every selected object becomes the pattern; the scale and angle of a pattern already there are kept.
        bool applyPattern(std::size_t patternId, bool toStroke);
        // The scale (1 = the tile's own size) and angle (degrees) of the pattern fills and strokes of the selection. False when none has one.
        bool setPatternTransform(double scale, double angleDegrees);
        // The first selected object's pattern fill (or stroke) when it has one, else null.
        const SketchPaint* selectedPatternPaint() const;
        bool renamePattern(std::size_t patternId, const std::string& name);
        // Removes a pattern no object uses; false when it is used or unknown.
        bool deletePattern(std::size_t patternId);
        std::shared_ptr<const SymbolDef> importPattern(const std::shared_ptr<const SymbolDef>& def);

        // ---- Images (documentimages.cpp) ----
        // A new Image object showing the picture, centred on `center` (scaled down to fit the active artboard when it is bigger); it becomes the
        // selection. Returns its id, 0 for no picture. placeImageFile reads the file and puts it in the middle of the active artboard.
        std::size_t placeImage(const std::shared_ptr<const ImageData>& image, const BLPoint& center);
        std::size_t placeImageFile(const std::string& path);

        // ---- Text on a path (documenttextpath.cpp) ----
        // Text can follow a path or be stretched between two (TextWarp). Making it takes COPIES of the paths, so the paths stay where they are
        // (and stay editable) but do not steer the text any more. One text and one path make text on a path; one text and two paths make text
        // between paths (the higher one is the top; they are turned to run the same way). The text is what stays selected.
        bool canMakeTextOnPath() const;
        bool makeTextOnPath();
        bool canMakeTextBetweenPaths() const;
        bool makeTextBetweenPaths();
        // The selected text is plain text again, where it appears now.
        bool canReleaseTextWarp() const;
        bool releaseTextWarp();
        // Where the selected text on a path starts (percent of the path) and which side it is on; for text between paths `flip` swaps top and bottom.
        bool setTextPathOptions(double startPercent, bool flip);
        // The first selected text that has a warp, or null.
        const SketchObject* selectedWarpedText() const;
    private:
        struct TextWarpState {
            std::size_t id = 0;
            TextWarp warp;
            BLMatrix2D matrix = BLMatrix2D::make_identity();
        };
        void applyTextWarp(const std::vector<TextWarpState>& before, const std::vector<TextWarpState>& after, const std::string& description);
    public:

        // ---- Brushes (documentbrushes.cpp): a library of brushes (brush.h) that draw strokes. ----
        // A brush is applied to the stroke of the selected objects (StrokeOptions::brush); changing, renaming and applying are undo steps.
        const std::vector<std::shared_ptr<const BrushDef>>& brushes() const { return brushes_; }
        std::shared_ptr<const BrushDef> findBrush(std::size_t id) const;
        // The brush the Brushes panel, the Paintbrush and the Object menu use (view state, not an undo step): the one chosen in the panel,
        // else the one last made, else the first; 0 when there are none.
        std::size_t activeBrush() const;
        void setActiveBrush(std::size_t id);
        // How many objects have a stroke drawn by the brush.
        std::size_t brushUseCount(std::size_t id) const;
        // Replaces the library (loading a file); not an undo step. Strokes that only know a brush id are bound to it.
        void setBrushes(std::vector<std::shared_ptr<const BrushDef>> brushes);
        void bindBrushStrokes();
        // A new brush ("Brush N" when `name` is empty) with the default numbers of its kind. A calligraphic brush needs nothing; the
        // other kinds take the selection (which stays) as their artwork and return 0 when nothing is selected. Returns the id.
        std::size_t makeBrush(BrushKind kind, const std::string& name);
        // Adds a few ready-made brushes that are not in the library yet (matched by name); returns how many were added.
        std::size_t addBrushPresets();
        // The stroke of every selected object is drawn by the brush (0 = the plain stroke again). An object without a stroke gets one.
        bool applyBrush(std::size_t id);
        // The brush as `edited` says (its name and numbers; kind and artwork are kept); strokes using it follow. False when it changes nothing.
        bool changeBrush(std::size_t id, const BrushDef& edited);
        // Removes a brush no stroke uses; false when it is used or unknown.
        bool deleteBrush(std::size_t id);
        // `def` (from another document) as a member of this library: the entry that already is the same, else a copy under a fresh id.
        std::shared_ptr<const BrushDef> importBrush(const std::shared_ptr<const BrushDef>& def);
        // The brush of the first selected object whose stroke has one, else null.
        std::shared_ptr<const BrushDef> selectedBrush() const;
        // The selected objects (stacking order) copied into a definition `id` whose box is their bounds; null when nothing is selected.
        std::shared_ptr<SymbolDef> selectionArtwork(std::size_t id, const std::string& name) const;

        // ---- Selection (ids, in selection order; a removed object drops out) ----
        const std::vector<std::size_t>& selection() const { return selection_; }
        bool isSelected(std::size_t id) const;
        void setSelection(const std::vector<std::size_t>& ids);
        void selectAll();
        void clearSelection() { setSelection({}); }
        // Union of the selected objects' geometryBounds(); false when nothing is selected.
        bool selectionBounds(BLBox& out) const;
        // The oriented frame. One object: its own frame angle. Several: their shared angle when they all
        // agree, else the group angle (set when the group is rotated - Illustrator keeps a rotated group
        // frame even when its members are turned differently), else upright.
        // The group angle is dropped whenever the selection changes.
        std::optional<double> groupAngle() const { return groupAngle_; }
        void setGroupAngle(std::optional<double> angle) { groupAngle_ = angle; }
        // The warped frame (set by a perspective warp; the selection frame becomes this quad). Dropped
        // with the group angle when the selection changes.
        std::optional<QuadFrame> quadFrame() const { return quadFrame_; }
        void setQuadFrame(const std::optional<QuadFrame>& quad) { quadFrame_ = quad; }
        FrameState frameState() const { return FrameState{groupAngle_, quadFrame_}; }
        bool selectionFrame(SelectionFrame& out) const;
        // The oriented frame of one object on its own: its placed outline measured along its frame angle.
        SelectionFrame objectFrame(const SketchObject& object) const;
        // The transform origin (rotate/shear pivot): kept as a fraction of the frame's box so it rides along
        // with moves, resizes and rotations; back to the center whenever the selection changes. View state - not undoable.
        BLPoint selectionOrigin(const SelectionFrame& frame) const;
        // With one object selected the origin is its transform node's pivot and moving it is an undo step. While dragging it pass `live`
        // (the steps are not recorded) and call finishSelectionOrigin() on release, which records the whole drag as one.
        void setSelectionOrigin(const SelectionFrame& frame, const BLPoint& world, bool live = false);
        void finishSelectionOrigin();
        typedef newui::Delegate<SketchDocument> SelectionChangedDelegate;
        SelectionChangedDelegate onSelectionChanged;

        // ---- Undoable edits (each pushes one step on undoStack()) ----
        // Adds on top and selects it.
        // Adds the objects (new ids) as one undo step and selects them; returns the ids.
        // Their groups come along as new groups carrying the same opacity / blend / clip, taken from `groupsFrom` (the document the objects
        // were made in; this one when null).
        std::vector<std::size_t> addObjectsUndoable(std::vector<std::unique_ptr<SketchObject>> objects, const std::string& description,
            const SketchDocument* groupsFrom = nullptr);
        SketchObject& addUndoable(SketchObject* object, const std::string& description);
        // Deletes the selection.
        void deleteSelection();
        // Duplicates the selection, offset by (dx, dy); the copies become the selection.
        void duplicateSelection(double dx, double dy);
        // Clipboard: the selection as SVG text (empty when nothing is selected), and the reverse - the objects in `svg`
        // are added (moved by dx, dy) as one undo step and become the selection. Returns false when `svg` has no objects.
        std::string selectionToSvg() const;
        bool pasteSvg(const std::string& svg, double dx, double dy);
        // Records an edit that has already been applied live (a drag): `before` holds each edited
        // object's pre-edit outline, the objects' current outlines are the "after".
        void commitGeometry(const std::vector<GeometryState>& before, const std::string& description,
            const FrameState& frameBefore = FrameState());
        // The same for an edit that only moved, turned, scaled or flipped the objects (placeObjects): undo and redo write the transform nodes
        // again instead of the outlines, which the nodes keep as they were drawn.
        void commitPlacement(const std::vector<GeometryState>& before, const std::string& description, const FrameState& frameBefore = FrameState());
        // Applies `matrix` to the given objects as one undo step.
        void transformObjects(const std::vector<std::size_t>& ids, const BLMatrix2D& matrix, const std::string& description);
        // Selected objects' ids and current outlines - the `before` for a drag.
        // `withChildren` also records the objects hanging from the selection, for edits that carry them along (a move, resize, rotate or flip).
        std::vector<GeometryState> snapshotSelection(bool withChildren = false) const;
        // The transform node that places an object's shape (invalidNodeId when it has none). With one object selected the transform origin
        // is that node's pivot, so the canvas and the node show and edit the same point.
        // Only for an object in the root scope; transformRefOf answers for any scope.
        jitnodes::NodeId transformNodeOf(std::size_t objectId) const;
        NodeRef transformRefOf(std::size_t objectId) const;
        // The matrix the groups holding `scope` put on everything inside it (identity for the root).
        BLMatrix2D scopeMatrix(std::uint32_t scope) const;
        // Where a placement node puts its shape on the page: its parent links, then the groups holding it.
        BLMatrix2D worldOf(const NodeRef& place) const;
        // True when the object's outline is nothing but its shape node's curve under its placement, so a file can leave the outline out and
        // build it from the graph (not text, whose words make the outline; not a chain-driven or out-of-step object).
        bool pathHeldByNode(const SketchObject& object) const;
        // Makes `child` hang from `parent` (0 releases it), keeping where it sits; refused for itself, a missing object or a loop. Undoable.
        bool setParent(std::size_t child, std::size_t parent);
        // The last object selected becomes the parent of the others (loops refused), as one undo step.
        bool parentSelection();
        // Frees every selected object from its parent, as one undo step.
        bool releaseParent();
        bool canParentSelection() const { return selection_.size() >= 2; }
        bool canReleaseParent() const;
        // `ids` followed by every object that hangs from one of them, directly or not.
        std::vector<std::size_t> withDescendants(const std::vector<std::size_t>& ids) const;
        // ---- Live shapes: objects whose outline is the cooked polyline of a graph node ----
        // Adds a graph node of `nodeType` (a closed polyline generator such as "circle"), cooks it and adds an object that follows it,
        // as one undo step. Returns the object, or null when the node cooks to nothing.
        SketchObject* addLiveShape(const std::string& nodeType, const std::map<std::string, float>& params, const std::string& description);
        // Edits one parameter of a live shape's node (one undo step); the outline follows.
        bool setLiveShapeParam(std::size_t objectId, const std::string& name, float value);
        // Redraw after an edit made directly on an object (drag preview).
        void touch() { changed(); }
        // While a Batch exists the document does not announce changes (views, panels, layer sorting); when the last one ends it does so
        // once. A load adds thousands of objects: without it every one of them would redraw and rebuild every panel.
        class Batch {
        public:
            explicit Batch(SketchDocument& document) : document_(document) { ++document_.batchDepth_; }
            ~Batch() {
                if (--document_.batchDepth_ == 0 && document_.batchPending_) {
                    document_.batchPending_ = false;
                    document_.changed();
                }
            }
            Batch(const Batch&) = delete;
            Batch& operator=(const Batch&) = delete;
        private:
            SketchDocument& document_;
        };

        // ---- Arrange / align / transform / lock / hide: each is one undo step on the selection, and returns
        // false (and records nothing) when it would change nothing ----
        enum class ArrangeOp { BringToFront, BringForward, SendBackward, SendToBack };
        // Moves the selected objects within the stacking order (index 0 is the back). Forward/Backward step past
        // one unselected neighbor, as in Illustrator.
        bool arrangeSelection(ArrangeOp op);
        enum class AlignMode { Left, Center, Right, Top, Middle, Bottom };
        // Aligns the selection's objects to the edge/center of their combined bounds - or, for one object, of the page.
        bool alignSelection(AlignMode mode);
        // Distribute Objects: spreads three or more objects (a group counts as one) so the same edge or center of each is evenly
        // spaced between the two outermost, which stay where they are.
        bool distributeSelection(AlignMode mode);
        // Distribute Spacing: three or more objects (two when `gap` is given) get the same gap between them along one axis. Without
        // `gap` the two outermost stay and the space between them is shared out; with it the first (leftmost / topmost) stays and the
        // rest follow at that distance.
        bool distributeSpacing(bool horizontal, std::optional<double> gap = std::nullopt);
        // The matrices of the Rotate, Scale and Reflect tools: turn by `radians`, scale by (sx, sy), or mirror across the line
        // through `origin` at `axisRadians` - all about `origin`.
        static BLMatrix2D rotationAbout(const BLPoint& origin, double radians);
        static BLMatrix2D scalingAbout(const BLPoint& origin, double sx, double sy);
        static BLMatrix2D reflectionAcross(const BLPoint& origin, double axisRadians);
        // Copies of the selection with `matrix` applied (their frame angles become angleScale * angle + angleOffset); one undo step
        // that leaves the originals alone, and the copies become the selection.
        void duplicateTransformed(const BLMatrix2D& matrix, double angleScale, double angleOffset, const std::string& description);
        // Turns / mirrors the selection about the center of its bounds (the frame angles follow).
        bool rotateSelection90(bool clockwise);
        bool flipSelection(bool horizontal);

        // ---- Cutting and combining (documentcutting.cpp). Each is one undo step; the pieces wear the look of what they came from
        // and take its place in the stacking order; the new objects become the selection. ----

        // Objects swapped for others: `remove` are taken out and `add` put in where they were (at the lowest removed position, or at
        // the highest when `atTop`; at the top of the stack when nothing is removed).
        struct Replacement {
            std::vector<std::size_t> remove;
            std::vector<std::unique_ptr<SketchObject>> add;
            bool atTop = false;
        };
        // Applies every replacement as ONE undo step called `description`; returns the ids of all the objects added.
        std::vector<std::size_t> replaceUndoable(std::vector<Replacement> replacements, const std::string& description);
        // Pathfinder on the selected objects (two or more, by their outlines): Unite, Intersect, Exclude, or Subtract (the lowest
        // object minus every one above it). The result is one path with the look of the top object (of the lowest for Subtract); curves
        // become fine straight pieces. False, and nothing changes, when fewer than two are selected or the result would be empty.
        bool combineSelection(BoolOp op);
        // The Knife: the line `cut` (document units) divides the filled shapes it crosses into pieces, along the line carried straight on
        // past its two ends. When some of the shapes it crosses are selected only those are cut, otherwise all of them. False when
        // nothing was cut.
        bool knifeCut(const std::vector<BLPoint>& cut);
        // The Eraser: removes what a pen `width` wide dragged along `stroke` touches - parts of filled shapes, parts of lines. When some of
        // what it touches is selected only that is erased, otherwise everything it touches. False when it touched nothing.
        bool erase(const std::vector<BLPoint>& stroke, double width);
        // The Scissors: cuts the path nearest to `at` (within `tolerance`) at that point, keeping its curves. A closed path opens; an
        // open one divides in two. False when no path is close enough (or the point is at the end of an open path).
        bool scissorsCut(const BLPoint& at, double tolerance);

        // The Shape Builder: the selected filled shapes (shown, unlocked, not text; bottom to top) cut into the cells they make together -
        // every distinct overlap and every part that is alone. `objects` are the shapes' ids and each cell's `members` index into it.
        struct ShapeBuilderModel {
            std::vector<std::size_t> objects;
            std::vector<ShapeCell> cells;
        };
        ShapeBuilderModel shapeBuilderModel() const;
        // Joins the chosen cells (indices into shapeBuilderModel().cells) into one new shape, taking them out of the shapes they were part of
        // (what is left of a shape stays one object, and one that is used up goes). With `erase` the cells are just taken out. One undo step;
        // the shapes that result become the selection. False when no cell was chosen.
        bool shapeBuild(const std::vector<std::size_t>& cellIndices, bool erase);

        // Text objects of the selection: `change` edits the state of each one (its words, font, size or style), recorded as one undo step.
        // False when the selection holds no text or nothing changed.
        bool changeSelectionText(const std::function<void(TextState&)>& change, const std::string& description);
        // The words, font, size and style a text object has now (no matrix).
        static TextState textStateOf(const SketchObject& object);
        // Puts `state` on the text object: its outline is rebuilt and its nodes written at once. No undo step, no changed().
        void setObjectText(std::size_t id, const TextState& state);

        // Image Trace (documenttrace.cpp): the layers of a traced picture become filled shapes in one group, fitted onto the page and
        // centred, as one undo step; they become the selection. Returns their ids (none when there was nothing to add).
        std::vector<std::size_t> addTrace(const TraceResult& trace);

        // ---- Selecting by look or by lasso (documentselect.cpp): selection changes, not undo steps ----
        enum class SameKind { FillColor, StrokeColor, StrokeWeight, Opacity, BlendMode, ObjectType };
        // Selects every selectable object that matches a selected one in the way given. False with nothing selected.
        bool selectSame(SameKind kind);
        // The Magic Wand: selects the objects whose fill (stroke, for an unfilled one) is like the one of object `sourceId`, colors within
        // `tolerance` (0..1) of each other; `add` keeps the current selection too.
        bool selectSimilar(std::size_t sourceId, bool add, double tolerance);
        // The Lasso: selects the objects touched by the freehand polygon (document units); `add` keeps the current selection too.
        bool selectInPolygon(const std::vector<BLPoint>& polygon, bool add);
        // The marquee: selects the objects whose stroke or filled area the rectangle touches (not their bounding boxes).
        bool selectInRect(const BLBox& rect, bool add);

        // ---- Swatches and the Eyedropper (documentswatches.cpp) ----
        // Saved colors as "#rrggbb" text, at most 24, shown in the Color panel. Each edit is one undo step; false when nothing changed
        // (a duplicate, a full list, a bad index).
        const std::vector<std::string>& swatches() const { return swatches_; }
        bool addSwatch(const std::string& hex);
        bool deleteSwatch(std::size_t index);
        // Replaces the list with no undo step (a load, New).
        void setSwatches(std::vector<std::string> swatches);
        // The Eyedropper: the look of the object under `at` goes onto the selected objects (one undo step); with nothing else selected it
        // becomes the look new shapes are drawn with. False when nothing is under the point.
        bool sampleLook(const BLPoint& at, double tolerance);

        // The Polygon and Star tool settings when the user keeps them in the document (Settings::toolDefaultsInDocument); 0 = not stored.
        // Saved as data-tools; setting them marks the document modified.
        struct ToolDefaults {
            int polygonSides = 0;
            int starPoints = 0;
        };
        const ToolDefaults& toolDefaults() const { return toolDefaults_; }
        void setToolDefaults(const ToolDefaults& defaults) { toolDefaults_ = defaults; onChanged(*this); }

        // ---- Ruler guides (documentguides.cpp): saved with the document; objects, drawing and pen points snap to them ----
        const std::vector<Guide>& guides() const { return guides_; }
        // Each is one undo step. addGuide returns the new guide's index; moveGuide / removeGuide are false for a bad index (or no change).
        std::size_t addGuide(bool vertical, double position);
        bool moveGuide(std::size_t index, double position);
        bool removeGuide(std::size_t index);
        bool clearGuides();   // false when there were none
        // Replaces the list with no undo step (a load, New) and does not mark the document modified.
        void setGuides(std::vector<Guide> guides);

        // ---- Layers (documentlayers.cpp) ----
        // A document always has at least one layer. Every object is in exactly one; the stacking order of the objects follows the layer
        // tree (layers farther back first, a layer's sublayers below its own objects), which the document keeps true after every change.
        // Arrange commands therefore work inside the object's layer. Hiding or locking a layer hides / locks all it holds.
        const std::vector<Layer>& layers() const { return layers_; }
        const Layer* findLayer(std::size_t id) const;
        // The sublayers of `parent` (0 = the top level), back to front.
        std::vector<std::size_t> layerChildren(std::size_t parent) const;
        // 0 for a top-level layer, 1 for its sublayers, ...
        std::size_t layerDepth(std::size_t id) const;
        // True when `ancestor` is `id` or one of the layers above it.
        bool layerContains(std::size_t ancestor, std::size_t id) const;
        // A layer is shown / editable when it and every layer above it is.
        bool layerShown(std::size_t id) const;
        bool layerEditable(std::size_t id) const;
        // How many objects are in the layer, and in the layer and its sublayers.
        std::size_t layerObjectCount(std::size_t id, bool withSublayers) const;
        // The layer new objects go to (view state: not undoable, not saved). Never 0 - it is a layer that exists.
        std::size_t activeLayer() const { return activeLayer_; }
        void setActiveLayer(std::size_t id);
        // The layer of the (first) selected object, 0 when nothing is selected.
        std::size_t selectionLayer() const;
        // Opened / closed in the Layers panel (view state).
        void setLayerExpanded(std::size_t id, bool expanded);
        // Each of these is one undo step and false when nothing happened.
        // A new layer on top of the others in `parent` (0 = top level; a name of "" gives "Layer N"); it becomes the active layer.
        std::size_t addLayer(std::size_t parent, const std::string& name = std::string());
        bool renameLayer(std::size_t id, const std::string& name);
        bool setLayerVisible(std::size_t id, bool visible);
        bool setLayerLocked(std::size_t id, bool locked);
        // Deletes the layer with its sublayers and everything in them. Refused for the last layer.
        bool deleteLayer(std::size_t id);
        // Puts the layer inside `parent` (0 = top level) at `position` among that layer's sublayers (0 = back). Refused into itself.
        bool moveLayer(std::size_t id, std::size_t parent, std::size_t position);
        // The selected objects go to the top of the layer. Grouped objects bring their whole group.
        bool moveSelectionToLayer(std::size_t id);
        // Makes the selection every selectable object of the layer and its sublayers. View state like any selection change.
        void selectLayerObjects(std::size_t id);
        // Replaces the whole tree with no undo step (a load, New) and does not mark the document modified. An empty list gives the one
        // default layer. Objects that name a layer the list does not have go to the active layer (the last top-level one).
        void setLayers(std::vector<Layer> layers);
        // More layer edits, each one undo step and false when nothing happened (documentlayers.cpp).
        bool setLayerColor(std::size_t id, std::uint32_t rgb);
        // A copy of the layer with its sublayers and everything in them, just above it; it becomes the active layer. Returns its id.
        std::size_t duplicateLayer(std::size_t id);
        // One place toward the front (`front`) or the back among its siblings.
        bool moveLayerStep(std::size_t id, bool front);
        bool showAllLayers();
        bool unlockAllLayers();
        // Every top-level layer but the one holding `id` is hidden / locked.
        bool hideOtherLayers(std::size_t id);
        bool lockOtherLayers(std::size_t id);
        // Everything in the layer goes into the sibling behind it, its sublayers become that layer's, and the layer is removed.
        bool mergeLayerDown(std::size_t id);
        // All the objects go into one layer (the back-most top-level one) and the other layers are removed.
        bool flattenArtwork();
        // Opens / closes every layer in the panel (view state, no undo step).
        void setAllLayersExpanded(bool expanded);
        // Moves objects (a group is moved whole by the caller) to the top of `layer`, or - when `relativeTo` names objects - next to them,
        // in front of (`above`) or behind them, in their layer. `groups`, when given, are the groups the moved objects now sit in
        // (outermost first), followed by what they had from position `keepFrom` of their own chain on (a number past the end keeps
        // none: the object leaves its groups). One undo step; false when nothing changes.
        bool moveObjects(const std::vector<std::size_t>& ids, std::size_t layer, const std::vector<std::size_t>& relativeTo, bool above,
            const std::optional<std::vector<std::size_t>>& groups = std::nullopt, std::size_t keepFrom = 0);
        // The objects of a group (nested ones too), back to front.
        std::vector<std::size_t> groupMembers(std::size_t group) const;
        // The objects' own visible / locked flags (what the eye and the lock of an object row set); a hidden or locked one leaves the selection.
        bool setObjectsVisible(const std::vector<std::size_t>& ids, bool visible);
        bool setObjectsLocked(const std::vector<std::size_t>& ids, bool locked);
        bool renameObject(std::size_t id, const std::string& name);

        // ---- Numeric transforms (documenttransform.cpp) ----
        // Moves / resizes the selection so that its bounds become `box` (a side with no length only moves). One undo step ("Move" or "Resize").
        bool setSelectionBox(const BLBox& box);
        // The matrix that carries the box `from` onto the box `to` (a side of `from` with no length is only moved).
        static BLMatrix2D matrixForBox(const BLBox& from, const BLBox& to);
        // Live editing (scrubbing a number): puts the selection's outlines back as they were in `before` and transforms them by `matrix`
        // (the frame angles turn by `angleDelta`), with NO undo step. When the editing is over, finish with
        // commitGeometry(before, name, frameBefore) - and noteTransform() - to make it one step.
        void previewTransform(const std::vector<GeometryState>& before, const BLMatrix2D& matrix, double angleDelta);
        // Every placement edit goes through here: each object goes where its starting state carried by its matrix puts it, by writing its
        // transform node (the graph is the truth) and refreshing the object from the same numbers. No undo step.
        void placeObjects(const std::vector<ObjectPlacement>& placements);
        // Curve edits: the object's outline becomes `placed` (page coordinates) and its shape node's curve is rewritten at once, keeping the
        // point ids and groups of points that stayed. syncObject() is the same after the object was changed some other way (a warp).
        // A driven object, or one not in the graph yet, is written by the next changed() instead.
        void setObjectOutline(SketchObject& object, const BLPath& placed);
        void syncObject(SketchObject& object);
        // The same matrix for every object in `baseline`; the frame angles become angleScale * angle + angleOffset.
        void placeObjects(const std::vector<GeometryState>& baseline, const BLMatrix2D& matrix, double angleScale = 1.0, double angleOffset = 0.0);
        // Puts the objects in `baseline` back as they were and bends them through `map` (`page` is the projective map it stands for, which
        // the paints follow); the shape nodes are rewritten at once. No undo step: finish with commitGeometry().
        void warpObjects(const std::vector<GeometryState>& baseline, const std::function<BLPoint(const BLPoint&)>& map, const PaintWarp& page);
        // Turns the selection about the center of its bounds by `radians` (the frame angles follow). One undo step.
        bool rotateSelectionBy(double radians);
        // Transform Again: the last move, resize or rotate (as the matrix it applied, about the same point) is applied once more to the
        // selection. Moves, nudges, resizes, rotations and the numeric ones above are all remembered; false when nothing was done yet.
        bool transformAgain();
        bool canTransformAgain() const { return hasLastTransform_; }
        // For the canvas, which applies drags itself: remember `matrix` (and the angle it turned the frames by) as the last transform.
        void noteTransform(const BLMatrix2D& matrix, double angleDelta);

        // ---- Object > Path (documentpaths.cpp) ----
        // Walks every outline of the selection the other way round (the handles follow). One undo step.
        bool reverseSelection();
        // Redraws the selected outlines with as few anchors as keep them within `tolerance` of the original (curve fitting; curves and
        // corners are kept where they are real). One undo step.
        bool simplifySelection(double tolerance = 0.5);
        // One selected open path is closed (its ends joined). Several are chained into one path, each next one being the piece nearest
        // the chain's end - ends that meet become one anchor, the rest are joined by a straight segment. False when no open path is selected.
        bool joinSelection();
        // The stroke of each selected object becomes a filled shape of its own (wearing the stroke's paint), above the object's fill, which
        // stays as it was without a stroke. Dashes and a width profile are not part of the outline (a plain stroke of the weight).
        bool outlineStroke();
        // Object > Expand Appearance: every visible fill and stroke of the selected shapes becomes an object of its own (a stroke as a filled
        // outline - a brush stroke as its artwork), geometry effects are baked into the outlines, and several results are grouped (the group
        // takes the object's opacity and blend mode). A shape with a pixel effect (shadow, glow, blur) becomes a picture instead. One undo step.
        bool expandAppearance();
        // Lock/Hide act on the selection (which is then cleared - a locked or hidden object cannot be selected);
        // Unlock All / Show All act on the whole document.
        bool lockSelection();
        bool hideSelection();
        bool unlockAll();
        bool showAll();

        // ---- Style (fill / stroke / weight / opacity) ----
        // The setters below change the selected objects live, with no undo step - a slider drag calls them
        // many times; the caller then records one step with commitStyle(). With nothing selected the
        // panel edits the defaults new shapes are drawn with instead (no undo).
        std::vector<StyleState> snapshotStyle() const;
        void setSelectionFill(const SketchPaint& paint);
        void setSelectionStroke(const SketchPaint& paint);
        void setSelectionStrokeWidth(double width);
        void setSelectionOpacity(double opacity);
        // Blend mode of the selection (live, like the other style setters; commitStyle records the step).
        void setSelectionBlendMode(BlendMode mode);
        // ---- Appearance stack of the selection (see AppearanceItem) ----
        // "Active" = the fill / stroke row selected in the Appearance panel, on every selected object. Choosing the active
        // item is view state (no undo step, does not mark the document modified).
        void setActiveItem(AppearanceKind kind, std::size_t index);
        // Live (no undo step; the panel commits with commitStyle) edits of the active item of the kind.
        void setSelectionItemOpacity(AppearanceKind kind, double opacity);
        void setSelectionItemBlendMode(AppearanceKind kind, BlendMode mode);
        void setSelectionItemVisible(AppearanceKind kind, bool visible);
        // Each of these is one undo step and returns false when nothing changed.
        // A new fill / stroke on top of the stack (from the document's default paints), which becomes the active one.
        bool addAppearanceItem(AppearanceKind kind);
        // A new effect on top of the stack (with the defaults of its kind); it becomes the active effect.
        bool addEffect(EffectKind kind);
        // Edits the active effect of every selected object (one undo step).
        bool changeActiveEffect(const std::function<void(AppearanceItem&)>& change, const std::string& description);
        // A copy of the active item right above it. The copy becomes active.
        bool duplicateActiveItem(AppearanceKind kind);
        // Removes the active item; the last fill / stroke is set to a None paint instead, so there is always one of each.
        bool deleteActiveItem(AppearanceKind kind);
        // Moves the active item one place up (+1, painted later) or down (-1) the stack.
        bool moveActiveItem(AppearanceKind kind, int delta);
        // Leaves no fill and no stroke, full opacity, normal blend.
        bool clearAppearance();

        // Cap / join / miter limit of the selection: `change` edits a copy of each object's options.
        void changeSelectionStrokeOptions(const std::function<void(StrokeOptions&)>& change);
        // Pushes one undo step taking the selection from `before` to its current style; false (and no step)
        // when nothing differs.
        bool commitStyle(const std::vector<StyleState>& before, const std::string& description);

        const SketchPaint& defaultFill() const { return defaultFill_; }
        const SketchPaint& defaultStroke() const { return defaultStroke_; }
        double defaultStrokeWidth() const { return defaultStrokeWidth_; }
        const StrokeOptions& defaultStrokeOptions() const { return defaultStrokeOptions_; }
        void setDefaultStrokeOptions(const StrokeOptions& options);
        void setDefaultFill(const SketchPaint& paint);
        void setDefaultStroke(const SketchPaint& paint);
        void setDefaultStrokeWidth(double width);

        // ---- Color mode ----
        // The document's working color model: RGB or CMYK. It decides the space new colors are authored in
        // (the default fill/stroke, the Color panel's starting mode) and what the status bar shows; a
        // single object can still carry a color in another space (an RGB swatch in a CMYK document).
        ColorSpace colorMode() const { return colorMode_; }
        // Sets the mode and converts the defaults new shapes get; existing objects keep their colors. Not
        // undoable - used when loading a file and starting a new document. Only RGB and CMYK are document modes.
        void setColorMode(ColorSpace mode);
        // Illustrator's Document Color Mode: sets the mode and converts every object's fill, stroke and
        // gradient stops (and the defaults) into it, as one undo step. False when already in that mode.
        bool convertToColorMode(ColorSpace mode);

        // Fired after any change to the document (edits, undo/redo, style) - the panels follow it.
        typedef newui::Delegate<SketchDocument> ChangedDelegate;
        ChangedDelegate onChanged;

        // Straightens the selection frame: drops the warped quad and the group angle and sets every selected
        // object's frame angle to 0, so the handles wrap the outlines in an upright box again. Undoable.
        // False when nothing changed (no selection, or the frame was already upright).
        bool resetBoundingBox();

        // Back to an empty page (size and ppi kept), not modified.
        void clearObjects();

        void paint(BLContext& ctx) const;

        bool empty() const override { return objects_.empty(); }
        std::size_t size() const override { return objects_.size(); }

    protected:
        bool readFromFile(const std::string& path) override;
        bool writeToFile(const std::string& path) override;

    protected:
        void graphChanged() override;

    private:
        void changed();
        // Rebuilds the outline of every live shape from its cooked node.
        void refreshLiveShapes();
        // Adopts the curves of every path node whose text no longer matches its object (a graph edit, undo or redo wrote it), unless the
        // object itself changed since the node was written.
        void pullPathNodes();
        void timeChanged(bool graphAffected) override;
        // Makes a driven object show what the chain ending at its placement node cooks; false when the chain cooks to nothing usable.
        bool adoptChain(SketchObject& object, jitnodes::NodeId place);
        // Whether the object placed by `place` is driven (a move-points node or an expression or keys on the way). Worked out once per graph change.
        bool drivenPlace(const NodeRef& place) const;
        mutable std::unordered_set<NodeRef> drivenPlaces_;
        mutable bool drivenDirty_ = true;
        // One object's nodes made to match it now: the placement, the paint warp and the shape node's strings, and the curve when `curve`.
        // False (nothing written) for an object with no placement node yet or one that is driven.
        bool writeObjectNodes(SketchObject& object, bool curve);
        // Mirrors every path object into a curvyPath node holding its curves: creates the missing nodes, rewrites the ones whose outline
        // changed since they were written and removes those whose object is gone. A deleted-then-restored object gets its node id back.
        void syncPathNodes();
        // The same for the look: each object's fills, strokes and effects are curvyFill / curvyStroke / curvyEffect nodes chained after its
        // shape node. syncAppearanceNodes creates, retypes and removes them to match the objects and writes the objects' looks into them;
        // pullAppearanceNodes adopts what a graph edit or undo changed in them. appearanceSynced_ is what each node held when object and node
        // last agreed, which is how the two tell who changed.
        // `scan` looks for appearance nodes nothing owns any more (after objects went away).
        void syncAppearanceNodes(bool scan);
        void pullAppearanceNodes();
        // Containment of groups: a group is a curvySubnet (its `groupId` parameter is the group id) holding the node chains of its members.
        // The graph is the carrier. syncGroupScopes reconciles each object with it: if SketchObject::groups() was edited since the last sync
        // (it differs from placedGroups()) the object's chain moves into the subnets that chain names, making them as needed; otherwise the
        // subnet the chain sits in wins and groups() adopts its group ids (a node moved in the graph, an undo). pruneGroupScopes removes the
        // subnets no object lives in, keeping their props aside. Group props live in the subnet; groupProps_ holds only those of a group with no subnet.
        // Objects in a subnet without a groupId are left alone.
        void syncGroupScopes();
        void pruneGroupScopes();
        std::vector<std::size_t> groupChainOfScope(std::uint32_t scope) const;
        NodeRef groupSubnetOf(std::size_t group) const;
        void putGroupProps(std::size_t group, const GroupProps& props);
        mutable std::unordered_map<std::size_t, NodeRef> groupSubnets_;
        // syncPathNodes for the objects of one scope; true when it removed a shape node.
        bool syncPathScope(std::uint32_t scope, const std::vector<SketchObject*>& objects);
        std::map<NodeRef, AppearanceParams> appearanceSynced_;
        // What each shape node's chain looked like at the last sync, so an object whose look did not change is passed over without touching
        // the graph (a thousand-object document syncs on every edit). A graph change throws it away.
        struct AppearanceChain {
            std::vector<std::uint32_t> nodes;
            std::vector<AppearanceItem> items;
        };
        std::map<NodeRef, AppearanceChain> appearanceChains_;
        std::size_t appearanceObjectCount_ = 0;
        std::string activeTag_ = "tag1";
        std::shared_ptr<jitnodes::GraphSnapshot> pivotBefore_;   // the graph when a live pivot drag started
        // placedOutline() per object, valid for the object's geometry revision at the time; cleared when the graph or the time changes.
        struct PlacedOutline {
            std::uint64_t revision = 0;
            BLPath path;
        };
        mutable std::unordered_map<std::size_t, PlacedOutline> outlineCache_;
        // Where findById last found each id; every hit is checked against the object there, so a stale entry only costs a rescan.
        mutable std::unordered_map<std::size_t, std::size_t> indexOfId_;
        std::unordered_map<std::uint32_t, std::unordered_map<jitnodes::NodeId, jitnodes::NodeId>> placedCache_;   // per scope, a shape node's transform node; rebuilt when the graph's wiring may have changed
        // Puts the objects in the stacking order given by ids (all of them, back to front).
        void reorder(const std::vector<std::size_t>& ids);
        // Records a visible/locked change of some objects (already applied) as one undo step.
        struct FlagState {
            std::size_t id = 0;
            bool visible = true;
            bool locked = false;
        };
        // Group membership of some objects, for the undo steps of group/ungroup.
        struct GroupState {
            std::size_t id = 0;
            std::vector<std::size_t> groups;
        };
        void applyGroups(const std::vector<GroupState>& states);
        // `state` plus the group props of its object's outermost group.
        StyleState withGroup(StyleState state) const;
        // Paints objects_[begin, end), which all share the first `depth` groups; a group with opacity / blend goes through a layer.
        void paintRange(BLContext& ctx, std::size_t begin, std::size_t end, std::size_t depth) const;
        bool applyFlags(const std::vector<FlagState>& before, const std::vector<FlagState>& after, const std::string& description);
        // Removes and returns the object (its index is stored in *index); null if not found.
        std::unique_ptr<SketchObject> detach(std::size_t id, std::size_t* index);
        void attach(std::unique_ptr<SketchObject> object, std::size_t index);
        void pruneSelection();

        // ---- layers (documentlayers.cpp) ----
        // A snapshot of the layer tree for undo; `objectLayers` / `order` are only filled by the steps that move objects.
        struct LayerState {
            std::vector<Layer> layers;
            std::size_t activeLayer = 0;
            std::vector<std::pair<std::size_t, std::size_t>> objectLayers;   // (object id, layer id)
            std::vector<std::size_t> order;                                   // every object id, back to front
        };
        LayerState snapshotLayers(bool withObjects) const;
        void applyLayers(const LayerState& state);
        void pushLayerStep(const std::string& description, const LayerState& before, const LayerState& after);
        // Rank of each layer in the stacking order, indexed by layer id (a layer unknown to the tree gets the active layer's).
        std::vector<int> layerRanks() const;
        // Puts the objects in layers that exist, brings the layers' hidden / locked state to them and sorts the stack by layer.
        void syncLayers();
        // `ids` (all the objects, back to front) sorted the way the layers want them (stable, so the order inside a layer is kept).
        std::vector<std::size_t> layerSorted(const std::vector<std::size_t>& ids) const;
        void resetLayers();
        std::size_t nextLayerId() const;
        std::vector<std::size_t> objectsInLayer(std::size_t id) const;   // the layer and its sublayers
        void dropUnselectable();

        std::vector<Artboard> artboards_{Artboard{"Artboard 1", 0.0, 0.0, 612.0, 792.0}};
        std::size_t activeArtboard_ = 0;
        ToolDefaults toolDefaults_;
        std::vector<std::shared_ptr<const SymbolDef>> symbols_;
        std::vector<std::shared_ptr<const SymbolDef>> patterns_;
        std::size_t activeSymbol_ = 0;
        std::size_t activePattern_ = 0;
        int batchDepth_ = 0;
        bool batchPending_ = false;
        std::vector<std::shared_ptr<const BrushDef>> brushes_;
        std::size_t activeBrush_ = 0;
        std::size_t nextBrushId_ = 1;
        std::size_t nextSymbolId_ = 1;
        float ppi_ = 72.0f;
        std::optional<SketchColor> pageBackground_ = SketchColor::rgb(1.0f, 1.0f, 1.0f);
        std::vector<std::unique_ptr<SketchObject>> objects_;
        std::size_t nextId_ = 1;
        std::size_t nextGroupId_ = 1;
        std::map<std::size_t, GroupProps> groupProps_;
        std::vector<GraphicStyle> graphicStyles_ = defaultGraphicStyles();
        SketchPaint defaultFill_ = SketchPaint::solid(SketchColor::fromHex("#d9e6ff"));
        SketchPaint defaultStroke_ = SketchPaint::solid(SketchColor::fromHex("#2f5fb3"));
        double defaultStrokeWidth_ = 1.5;
        StrokeOptions defaultStrokeOptions_;
        ColorSpace colorMode_ = ColorSpace::RGB;
        std::vector<std::size_t> selection_;
        std::vector<Guide> guides_;
        std::vector<std::string> swatches_ = defaultSwatches();
        std::vector<Layer> layers_ = {Layer{1, 0, "Layer 1", true, false, true}};
        std::size_t activeLayer_ = 1;
        BLMatrix2D lastTransform_ = BLMatrix2D::make_identity();
        double lastTransformAngle_ = 0.0;
        bool hasLastTransform_ = false;
        std::optional<double> groupAngle_;
        std::optional<QuadFrame> quadFrame_;
        double originU_ = 0.5;
        double originV_ = 0.5;
    };

}
