#pragma once

#include "curvy/noderef.h"
#include "curvy/selectionmodel.h"
#include "curvy/sketchdocument.h"

#include <blend2d/blend2d.h>

#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // One measurement of the selection, as the canvas labels it and as a script reads it. `value` is in points (the document's stored unit);
    // the caller converts it with Units for display. `anchor` is where the label sits, in document points.
    // @reflect ignore=true
    struct DimensionItem {
        enum class Kind { Width, Height, Radius, CurveLength };

        Kind kind = Kind::Width;
        double value = 0.0;
        BLPoint anchor;
        bool editable = false;
    };

    // What the selection measures and how a measurement is changed. Reading never touches the document. Changing one is a session: begin()
    // takes the graph as it is, preview() sets a value live (no undo step, as a scrub does), commit() makes the one undo step and cancel() puts the
    // graph back. apply() is the three in a row, for a typed value or a script. A width or height scales the selection along the document axes
    // about the top-left corner of its box, a radius about its centre, by editing the transform of each selected object or shape (a shape
    // gets a local transform when it has none), so it works through rotated and scaled groups around it.
    // @reflect ignore=true
    class DimensionModel {
    public:
        DimensionModel(SketchDocument& document, SelectionModel& selection) : document_(document), selection_(selection) {}

        static const char* name(DimensionItem::Kind kind);
        static std::optional<DimensionItem::Kind> kindFromName(const std::string& text);

        // The box of the selected shapes in document points (the paths, not their strokes); false when no shape is selected.
        bool bounds(BLBox& box);
        // Width and Height of the box; Radius when exactly one circle is selected; CurveLength when exactly one other shape is.
        std::vector<DimensionItem> compute();

        bool editing() const { return editing_; }
        // False (and nothing changes) when the measurement is not editable now; the reason is in message().
        bool begin(DimensionItem::Kind kind);
        // `value` in points. False when it is refused (zero or negative, or a transform that cannot hold the result). `aboutPivot` holds the pivot
        // of the transform being edited still instead of the corner of the box (the centre of a radius edit): Alt, as in Illustrator.
        bool preview(double value, bool aboutPivot = false);
        bool commit();
        void cancel();
        bool apply(DimensionItem::Kind kind, double value, bool aboutPivot = false);

        // Where the pivots of the selection are, in document points, for the canvas to mark. Reading never changes the document.
        struct PivotMark {
            enum class Level { Shape, Object };

            Level level = Level::Shape;
            NodeRef owner;      // the selected layer, group or shape node the pivot was found from
            NodeRef xform;      // the transform node the pivot belongs to; invalid for a shape that has none yet
            BLPoint at;
        };
        std::vector<PivotMark> pivots();

        // Moves one pivot to `at` (document points) without moving anything on screen: the transform's translation is adjusted to match. The
        // same begin / preview / commit session as a size edit, so a drag is one undo step. False when the move is refused.
        bool beginPivotMove(const PivotMark& mark);
        bool previewPivotMove(const BLPoint& at);
        const std::string& message() const { return message_; }

        // A 2D matrix x' = a x + c y + e, y' = b x + d y + f, and the few operations the transform editing here needs; SketchDocument uses them
        // to put a new pivot at the centre of a shape.
        struct Affine {
            double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;
        };
        static Affine multiply(const Affine& left, const Affine& right);
        static bool invert(const Affine& m, Affine& out);
        // The matrix a `transform` node applies (its pivot included).
        static Affine fromNode(const jitnodes::Node& transform);

    private:
        struct Target {
            NodeRef xform;
            Affine above;      // what the node's output goes through before it reaches the document (the objects around it)
            Affine original;
            float px = 0.0f, py = 0.0f;
        };

        std::vector<NodeRef> editableNodes() const;
        // The node that made the shape when exactly one shape is selected (a lone shape inside a selected group counts); else invalid.
        NodeRef singleShape();
        Affine aboveOf(const NodeRef& object) const;
        bool refuse(const std::string& text);
        static BLPoint through(const Affine& m, double x, double y);
        // Starts the edit session for `targets_` once they are known; shared by a size edit and a pivot move.
        bool open(const std::vector<NodeRef>& items);

        SketchDocument& document_;
        SelectionModel& selection_;
        bool editing_ = false;
        DimensionItem::Kind kind_ = DimensionItem::Kind::Width;
        bool previewed_ = false;
        bool pivotMove_ = false;
        BLBox box_;
        std::optional<jitnodes::GraphSnapshot> before_;
        std::vector<Target> targets_;
        std::string message_;
    };

}
