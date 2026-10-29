#include "curvy/sketchdocument.h"

#include "curvy/imagetrace.h"

#include <algorithm>
#include <cstdio>

// Image Trace: the traced layers become filled shapes, fitted onto the page and grouped.

namespace curvy {

    std::vector<std::size_t> SketchDocument::addTrace(const TraceResult& trace) {
        if (trace.shapes.empty() || trace.width <= 0 || trace.height <= 0) {
            return {};
        }
        // Fit the picture inside the page with a margin, centred (never enlarged past what fits).
        const double margin = 18.0;
        const Artboard& board = artboards_[activeArtboard_];
        const double scale = std::min((board.width - 2.0 * margin) / double(trace.width), (board.height - 2.0 * margin) / double(trace.height));
        if (!(scale > 0.0)) {
            return {};
        }
        const double dx = board.x + (board.width - double(trace.width) * scale) * 0.5, dy = board.y + (board.height - double(trace.height) * scale) * 0.5;
        BLMatrix2D place = BLMatrix2D::make_scaling(scale, scale);
        place.post_translate(dx, dy);
        const std::size_t group = newGroupId();
        std::vector<std::unique_ptr<SketchObject>> objects;
        for (const TracedShape& shape : trace.shapes) {
            BLPath path = shape.path;
            path.transform(place);
            std::unique_ptr<SketchObject> object(SketchObject::makePath(path));
            char hex[16];
            std::snprintf(hex, sizeof(hex), "#%06x", unsigned(shape.rgb & 0xFFFFFFu));
            object->setFill(SketchPaint::solid(SketchColor::fromHex(hex).convertedTo(colorMode_)));
            object->setStroke(SketchPaint::none());
            setGroupsOf(*object, {group});
            objects.push_back(std::move(object));
        }
        return addObjectsUndoable(std::move(objects), "Image Trace");
    }

}
