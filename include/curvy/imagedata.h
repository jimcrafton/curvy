#pragma once

#include "curvy/sketchobject.h"

#include <memory>
#include <string>

namespace curvy {

    // Pictures that sit in a document as Image objects (SketchObjectType::Image). A picture is kept as the bytes of the file it came from
    // (PNG, JPEG, BMP or QOI - saved as they are, nothing is re-encoded) together with the decoded pixels it is drawn from.

    // Decodes `bytes` (a whole picture file). Null when it is not a picture Blend2D can read.
    std::shared_ptr<const ImageData> decodeImage(const std::string& bytes, const std::string& name);

    // Reads a picture file; null when it cannot be read or decoded.
    std::shared_ptr<const ImageData> loadImageFile(const std::string& path);

    // "image/png", "image/jpeg", "image/bmp" or "image/qoi" by the first bytes of the file ("" when unknown).
    std::string imageMimeType(const std::string& bytes);

    std::string base64Encode(const std::string& bytes);
    // False when `text` is not base64 (blanks are skipped).
    bool base64Decode(const std::string& text, std::string& out);

    // "data:image/png;base64,...." for the picture, and the reverse: the bytes of a data: URI (base64 or percent-escaped); false when `uri`
    // is not a data URI.
    std::string imageDataUri(const ImageData& image);
    bool bytesFromDataUri(const std::string& uri, std::string& bytes);

}
