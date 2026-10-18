#include "curvy/sketchdocument.h"

#include "curvy/imagedata.h"

#include <algorithm>

// Pictures in the document: Image objects (see ImageData in sketchobject.h).

namespace curvy {

    std::size_t SketchDocument::placeImage(const std::shared_ptr<const ImageData>& image, const BLPoint& center) {
        if (!image || image->width <= 0 || image->height <= 0) {
            return 0;
        }
        // A picture bigger than the artboard is scaled down to fit in it (one pixel is one point otherwise).
        const Artboard& board = activeArtboardRect();
        const double w = double(image->width), h = double(image->height);
        double scale = std::min(1.0, std::min(0.8 * board.width / w, 0.8 * board.height / h));
        BLMatrix2D placement = BLMatrix2D::make_scaling(scale, scale);
        placement.post_translate(center.x - w * scale * 0.5, center.y - h * scale * 0.5);
        std::vector<std::unique_ptr<SketchObject>> objects;
        objects.emplace_back(SketchObject::makeImageObject(image, placement));
        std::vector<std::size_t> ids = addObjectsUndoable(std::move(objects), "Place Image");
        return ids.empty() ? 0 : ids.front();
    }

    std::size_t SketchDocument::placeImageFile(const std::string& path) {
        const Artboard& board = activeArtboardRect();
        return placeImage(loadImageFile(path), BLPoint(board.x + board.width * 0.5, board.y + board.height * 0.5));
    }

}
