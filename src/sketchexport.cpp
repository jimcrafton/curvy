#include "curvy/sketchexport.h"

#include "curvy/sketchdocument.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace curvy {

    namespace {
        constexpr double kMaxPixels = 100.0e6;   // refuses an export that would need more than this many pixels

        std::string lowerExtension(const std::string& path) {
            std::size_t dot = path.find_last_of('.');
            std::string extension = dot == std::string::npos ? std::string() : path.substr(dot + 1);
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return extension;
        }
    }

    std::string artboardFilePath(const std::string& path, const std::string& artboardName, std::size_t number) {
        std::size_t dot = path.find_last_of('.');
        std::size_t slash = path.find_last_of("/\\");
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
            dot = path.size();
        }
        std::string name;
        for (unsigned char c : artboardName) {
            name += std::isalnum(c) ? char(c) : '-';
        }
        return path.substr(0, dot) + "_" + std::to_string(number) + (name.empty() ? "" : "_" + name) + path.substr(dot);
    }

    double exportScaleFor(const SketchDocument& document) {
        return std::max(0.1, double(document.ppi()) / 72.0);
    }

    bool renderPageImage(const SketchDocument& document, double scale, BLImage& out) {
        return renderArtboardImage(document, document.activeArtboard(), scale, out);
    }

    bool renderArtboardImage(const SketchDocument& document, std::size_t artboard, double scale, BLImage& out) {
        if (!(scale > 0.0) || artboard >= document.artboards().size()) {
            return false;
        }
        const Artboard& board = document.artboards()[artboard];
        double width = std::ceil(board.width * scale), height = std::ceil(board.height * scale);
        if (width < 1.0 || height < 1.0 || width * height > kMaxPixels) {
            return false;
        }
        BLImage image;
        if (image.create(int(width), int(height), BL_FORMAT_PRGB32) != BL_SUCCESS) {
            return false;
        }
        BLContext ctx(image);
        ctx.clear_all();
        if (document.pageBackground()) {
            ctx.set_fill_style(document.pageBackground()->toRgba32());
            ctx.fill_all();
        }
        ctx.scale(scale);
        ctx.translate(-board.x, -board.y);
        ctx.clip_to_rect(BLRect(board.x, board.y, board.width, board.height));
        document.paint(ctx);
        ctx.end();
        out = image;
        return true;
    }

    bool exportPageImage(const SketchDocument& document, const std::string& path, double scale) {
        return exportArtboardImage(document, document.activeArtboard(), path, scale);
    }

    bool exportArtboardImage(const SketchDocument& document, std::size_t artboard, const std::string& path, double scale) {
        BLImage image;
        if (!renderArtboardImage(document, artboard, scale, image)) {
            return false;
        }
        std::string extension = lowerExtension(path);
        if (extension == "bmp" && !document.pageBackground()) {
            // No transparency in these formats: put the page on white.
            BLImage flat;
            if (flat.create(image.width(), image.height(), BL_FORMAT_PRGB32) != BL_SUCCESS) {
                return false;
            }
            BLContext ctx(flat);
            ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
            ctx.fill_all();
            ctx.blit_image(BLPointI(0, 0), image);
            ctx.end();
            image = flat;
        }
        return image.write_to_file(path.c_str()) == BL_SUCCESS;
    }

}
