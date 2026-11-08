#include "curvy/io/eps/epsexport.h"

#include "curvy/sketchdocument.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

// EPS export (see epsexport.h). One DSC-conforming PostScript level 2 program: a prolog, then the active artboard's objects in stacking order,
// drawn in document coordinates (y down) under one flipping matrix.

namespace curvy {

    namespace {

        constexpr int kGradientSteps = 96;

        std::string num(double value) {
            if (!std::isfinite(value)) {
                return "0";
            }
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.4f", value);
            std::string text = buffer;
            while (!text.empty() && text.back() == '0') { text.pop_back(); }
            if (!text.empty() && text.back() == '.') { text.pop_back(); }
            return text == "-0" || text.empty() ? std::string("0") : text;
        }

        std::string matrixText(const BLMatrix2D& m) {
            return "[" + num(m.m00) + " " + num(m.m01) + " " + num(m.m10) + " " + num(m.m11) + " " + num(m.m20) + " " + num(m.m21) + "]";
        }

        struct Rgb { double r = 0, g = 0, b = 0; };

        class Writer {
        public:
            std::string build(const SketchDocument& document);

        private:
            Rgb mixed(const SketchColor& color) const;
            void setColor(std::string& out, const SketchColor& color);
            void pathOps(std::string& out, const BLPath& path);
            void gradientFill(std::string& out, const SketchPaint& paint, const BLPath& path, const BLBox& box, bool evenOdd = false);
            // Selects `paint` for a solid area; false for the paints that need more than a color (gradient, pattern) or paint nothing.
            bool solidPaint(std::string& out, const SketchPaint& paint, double opacity);
            std::string fillPath(const BLPath& path, const SketchPaint& paint, double opacity, bool evenOdd = false);
            std::string drawBrush(const SketchObject& object, const AppearanceItem& item, std::size_t index);
            std::string drawParts(const std::vector<BrushPart>& parts, double opacity);
            std::string drawItem(const SketchObject& object, std::size_t index);
            std::string drawObject(const SketchObject& object);
            std::string drawObjectBody(const SketchObject& object);
            std::string drawPicture(const BLImage& image, const BLMatrix2D& placement);
            std::string drawRange(const SketchDocument& document, std::size_t begin, std::size_t end, std::size_t depth);

            const SketchObject* painting_ = nullptr;
            Rgb background_{1, 1, 1};
        };

        // A color at some opacity, mixed with the page color (PostScript has no alpha).
        Rgb Writer::mixed(const SketchColor& color) const {
            const BLRgba32 c = color.toRgba32();
            const double a = color.alpha();
            return {c.r() / 255.0 * a + background_.r * (1.0 - a), c.g() / 255.0 * a + background_.g * (1.0 - a), c.b() / 255.0 * a + background_.b * (1.0 - a)};
        }

        void Writer::setColor(std::string& out, const SketchColor& color) {
            if (color.alpha() >= 1.0f) {
                switch (color.space()) {
                    case ColorSpace::CMYK:
                        out += num(color.component(0)) + " " + num(color.component(1)) + " " + num(color.component(2)) + " " + num(color.component(3)) + " setcmykcolor\n";
                        return;
                    case ColorSpace::Gray:
                        out += num(color.component(0)) + " setgray\n";
                        return;
                    default:
                        break;
                }
            }
            const Rgb c = mixed(color);
            out += num(c.r) + " " + num(c.g) + " " + num(c.b) + " setrgbcolor\n";
        }

        void Writer::pathOps(std::string& out, const BLPath& path) {
            BLPathView view = path.view();
            BLPoint current(0, 0), start(0, 0);
            out += "newpath\n";
            for (std::size_t i = 0; i < view.size; ++i) {
                const BLPoint p = view.vertex_data[i];
                switch (view.command_data[i]) {
                    case BL_PATH_CMD_MOVE:
                        out += num(p.x) + " " + num(p.y) + " moveto\n";
                        current = start = p;
                        break;
                    case BL_PATH_CMD_ON:
                        out += num(p.x) + " " + num(p.y) + " lineto\n";
                        current = p;
                        break;
                    case BL_PATH_CMD_QUAD:
                    case BL_PATH_CMD_CONIC: {
                        const std::size_t endAt = view.command_data[i] == BL_PATH_CMD_QUAD ? i + 1 : i + 2;
                        if (endAt >= view.size) { break; }
                        const BLPoint e = view.vertex_data[endAt];
                        out += num(current.x + 2.0 / 3.0 * (p.x - current.x)) + " " + num(current.y + 2.0 / 3.0 * (p.y - current.y)) + " " +
                            num(e.x + 2.0 / 3.0 * (p.x - e.x)) + " " + num(e.y + 2.0 / 3.0 * (p.y - e.y)) + " " + num(e.x) + " " + num(e.y) + " curveto\n";
                        current = e;
                        i = endAt;
                        break;
                    }
                    case BL_PATH_CMD_CUBIC: {
                        if (i + 2 >= view.size) { break; }
                        const BLPoint c2 = view.vertex_data[i + 1], e = view.vertex_data[i + 2];
                        out += num(p.x) + " " + num(p.y) + " " + num(c2.x) + " " + num(c2.y) + " " + num(e.x) + " " + num(e.y) + " curveto\n";
                        current = e;
                        i += 2;
                        break;
                    }
                    case BL_PATH_CMD_CLOSE:
                        out += "closepath\n";
                        current = start;
                        break;
                    default:
                        break;
                }
            }
        }

        bool Writer::solidPaint(std::string& out, const SketchPaint& paint, double opacity) {
            if (paint.isNone() || paint.isPattern() || paint.isGradient() || paint.isPoints()) {
                return false;
            }
            SketchColor color = paint.color();
            color.setAlpha(float(color.alpha() * opacity));
            setColor(out, color);
            return true;
        }

        // The gradient as bands: clip to the shape, then fill stripes (linear) or rings (radial) in the gradient's own space.
        void Writer::gradientFill(std::string& out, const SketchPaint& paint, const BLPath& path, const BLBox& box, bool evenOdd) {
            const SketchGradient& gradient = paint.gradient();
            const bool radial = paint.kind() == SketchPaint::Kind::Radial;
            struct Stop { double at; SketchColor color; };
            std::vector<Stop> stops;
            for (const GradientStop& s : gradient.stops) { stops.push_back({std::min(1.0, std::max(0.0, s.offset)), s.color}); }
            std::stable_sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) { return a.at < b.at; });
            auto colorAt = [&](double t) {
                if (t <= stops.front().at) { return stops.front().color; }
                if (t >= stops.back().at) { return stops.back().color; }
                for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
                    if (t <= stops[i + 1].at) {
                        const double span = stops[i + 1].at - stops[i].at;
                        const double f = span <= 1e-9 ? 1.0 : (t - stops[i].at) / span;
                        const BLRgba32 a = stops[i].color.toRgba32(), b = stops[i + 1].color.toRgba32();
                        const float al = float(stops[i].color.alpha() * (1.0 - f) + stops[i + 1].color.alpha() * f);
                        return SketchColor::rgb(float((a.r() * (1.0 - f) + b.r() * f) / 255.0), float((a.g() * (1.0 - f) + b.g() * f) / 255.0),
                            float((a.b() * (1.0 - f) + b.b() * f) / 255.0), al);
                    }
                }
                return stops.back().color;
            };
            BLPoint start, end;
            gradient.axisPoints(box, radial, start, end);
            const double length = std::max(1e-6, std::hypot(end.x - start.x, end.y - start.y));
            // Far enough to cover the shape from any start point.
            BLBox pb(0, 0, 0, 0);
            path.get_bounding_box(&pb);
            const double reach = 2.0 * (std::hypot(pb.x1 - pb.x0, pb.y1 - pb.y0) + std::hypot(start.x - pb.x0, start.y - pb.y0) + length) + 10.0;

            out += "gsave\n";
            pathOps(out, path);
            out += evenOdd ? "eoclip newpath\n" : "clip newpath\n";
            if (painting_ != nullptr) {
                out += matrixText(painting_->paintToPage()) + " concat\n";
            }
            if (gradient.hasAxis && (gradient.axisSkew != 0.0 || (radial && gradient.axisAspect != 1.0))) {
                out += matrixText(gradient.axisMatrix(start, end, radial)) + " concat\n";
            }
            const double angle = std::atan2(end.y - start.y, end.x - start.x) * 180.0 / 3.14159265358979323846;
            out += num(start.x) + " " + num(start.y) + " translate\n";
            if (!radial) {
                out += num(angle) + " rotate\n";
                // Before the start and after the end: the end colors, extended.
                setColor(out, colorAt(0.0));
                out += num(-reach) + " " + num(-reach) + " " + num(reach) + " " + num(2 * reach) + " rectfill\n";
                setColor(out, colorAt(1.0));
                out += num(length - 0.0) + " " + num(-reach) + " " + num(reach) + " " + num(2 * reach) + " rectfill\n";
                for (int i = 0; i < kGradientSteps; ++i) {
                    const double t0 = double(i) / kGradientSteps, t1 = double(i + 1) / kGradientSteps;
                    setColor(out, colorAt((t0 + t1) * 0.5));
                    // A little overlap hides the seam between neighbours.
                    out += num(t0 * length) + " " + num(-reach) + " " + num((t1 - t0) * length + (i + 1 < kGradientSteps ? 0.25 : 0.0)) + " " + num(2 * reach) + " rectfill\n";
                }
            } else {
                setColor(out, colorAt(1.0));
                out += "0 0 " + num(reach) + " 0 360 arc fill\n";
                for (int i = kGradientSteps - 1; i >= 0; --i) {
                    const double t0 = double(i) / kGradientSteps, t1 = double(i + 1) / kGradientSteps;
                    setColor(out, colorAt((t0 + t1) * 0.5));
                    out += "newpath 0 0 " + num(t1 * length) + " 0 360 arc fill\n";
                }
            }
            out += "grestore\n";
        }

        std::string Writer::fillPath(const BLPath& path, const SketchPaint& paint, double opacity, bool evenOdd) {
            if (path.size() == 0 || paint.isNone()) {
                return std::string();
            }
            std::string out = "gsave\n";
            if (paint.isGradient() && !paint.gradient().stops.empty()) {
                BLBox box = painting_ != nullptr ? painting_->paintFrameBounds() : BLBox(0, 0, 0, 0);
                if (painting_ == nullptr) { path.get_bounding_box(&box); }
                gradientFill(out, paint, path, box, evenOdd);
                return out + "grestore\n";
            }
            if (!solidPaint(out, paint, opacity)) {
                return std::string();
            }
            pathOps(out, path);
            return out + (evenOdd ? "eofill\ngrestore\n" : "fill\ngrestore\n");
        }

        std::string Writer::drawParts(const std::vector<BrushPart>& parts, double opacity) {
            std::string out;
            for (const BrushPart& part : parts) {
                if (part.closed) {
                    out += fillPath(part.path, part.fill, opacity);
                }
                if (part.strokeWidth > 0.0) {
                    std::string s = "gsave\n";
                    if (solidPaint(s, part.stroke, opacity)) {
                        s += num(part.strokeWidth) + " setlinewidth\n";
                        pathOps(s, part.path);
                        out += s + "stroke\n";
                    }
                    out += "grestore\n";
                }
            }
            return out;
        }

        std::string Writer::drawBrush(const SketchObject& object, const AppearanceItem& item, std::size_t index) {
            const BrushDef& brush = *item.strokeOptions.brush;
            const BLPath shown = object.itemOutline(index);
            if (brush.kind == BrushKind::Calligraphic) {
                return fillPath(brush.nibOutline(shown, item.strokeWidth), item.paint, item.opacity);
            }
            if (brush.kind == BrushKind::Art) {
                return drawParts(brush.bendAlong(shown, item.strokeWidth), item.opacity);
            }
            std::string out;
            for (const BLMatrix2D& matrix : brush.placements(shown, item.strokeWidth)) {
                std::vector<BrushPart> moved = brush.parts();
                for (BrushPart& part : moved) {
                    part.path.transform(matrix);
                    part.strokeWidth *= std::sqrt(std::fabs(matrix.m00 * matrix.m11 - matrix.m01 * matrix.m10));
                }
                out += drawParts(moved, item.opacity);
            }
            return out;
        }

        std::string Writer::drawItem(const SketchObject& object, std::size_t index) {
            painting_ = &object;
            const AppearanceItem& item = object.appearance()[index];
            std::string out;
            if (item.kind == AppearanceKind::Fill) {
                if (object.isClosed()) {
                    if (item.paint.isPattern()) {
                        // A pattern fill: the average of its tile is not known here, so the shape is left to the picture fallback in drawObject.
                        out = std::string();
                    } else {
                        out = fillPath(object.itemOutline(index), item.paint, item.opacity, item.evenOdd);
                    }
                }
            } else if (item.kind == AppearanceKind::Stroke && !item.paint.isNone() && item.strokeWidth > 0.0) {
                if (item.strokeOptions.brush) {
                    out = drawBrush(object, item, index);
                } else if (!item.strokeOptions.widths.empty()) {
                    BLPath outline;
                    if (object.strokeOutline(index, outline)) { out = fillPath(outline, item.paint, item.opacity); }
                } else if (item.paint.isGradient()) {
                    // A gradient stroke: the stroke's outline, filled with the bands.
                    BLPath outline;
                    if (object.strokeOutline(index, outline)) { out = fillPath(outline, item.paint, item.opacity); }
                } else {
                    std::string s = "gsave\n";
                    if (solidPaint(s, item.paint, item.opacity)) {
                        const StrokeOptions& options = item.strokeOptions;
                        s += num(item.strokeWidth) + " setlinewidth " + std::to_string(options.cap == StrokeCap::Butt ? 0 : (options.cap == StrokeCap::Round ? 1 : 2)) + " setlinecap " +
                            std::to_string(options.join == StrokeJoin::Miter ? 0 : (options.join == StrokeJoin::Round ? 1 : 2)) + " setlinejoin " + num(std::max(1.0, options.miterLimit)) + " setmiterlimit\n";
                        double period = 0.0;
                        std::string dashes;
                        for (double d : options.dashes) { dashes += num(d) + " "; period += d; }
                        if (period > 1e-6) { s += "[" + dashes + "] 0 setdash\n"; }
                        pathOps(s, object.itemOutline(index));
                        out = s + "stroke\n";
                    }
                    if (!out.empty()) { out += "grestore\n"; }
                }
            }
            painting_ = nullptr;
            return out;
        }

        // A picture as ASCIIHex RGB data; transparency is composited on the page color.
        std::string Writer::drawPicture(const BLImage& image, const BLMatrix2D& placement) {
            BLImageData data;
            if (image.is_empty() || const_cast<BLImage&>(image).get_data(&data) != BL_SUCCESS || data.size.w <= 0 || data.size.h <= 0) {
                return std::string();
            }
            const int w = data.size.w, h = data.size.h;
            std::string out = "gsave\n" + matrixText(placement) + " concat\n" +
                std::to_string(w) + " " + std::to_string(h) + " 8 [1 0 0 1 0 0]\ncurrentfile /ASCIIHexDecode filter\nfalse 3 colorimage\n";
            static const char kHex[] = "0123456789abcdef";
            std::size_t column = 0;
            for (int y = 0; y < h; ++y) {
                const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
                for (int x = 0; x < w; ++x) {
                    const std::uint32_t p = row[x];
                    const double inv = 1.0 - double(p >> 24) / 255.0;   // premultiplied: add the background's share
                    const double channel[3] = {double((p >> 16) & 255u) + background_.r * 255.0 * inv, double((p >> 8) & 255u) + background_.g * 255.0 * inv,
                        double(p & 255u) + background_.b * 255.0 * inv};
                    for (double c : channel) {
                        const unsigned v = unsigned(std::min(255.0, std::max(0.0, c)) + 0.5);
                        out += kHex[v >> 4];
                        out += kHex[v & 15];
                    }
                    column += 6;
                    if (column >= 72) { out += '\n'; column = 0; }
                }
            }
            return out + ">\ngrestore\n";
        }

        std::string Writer::drawObjectBody(const SketchObject& object) {
            if (object.isImage()) {
                return object.image() ? drawPicture(object.image()->image, object.imageMatrix()) : std::string();
            }
            if (object.isSymbol()) {
                const std::shared_ptr<const SymbolDef>& def = object.symbol();
                if (!def || !def->content) {
                    return std::string();
                }
                const std::string inner = drawRange(*def->content, 0, def->content->objectCount(), 0);
                return inner.empty() ? std::string() : "gsave\n" + matrixText(object.symbolMatrix()) + " concat\n" + inner + "grestore\n";
            }
            std::string out;
            const std::vector<AppearanceItem>& items = object.appearance();
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (items[i].visible && items[i].kind != AppearanceKind::Effect) {
                    out += drawItem(object, i);
                }
            }
            return out;
        }

        std::string Writer::drawObject(const SketchObject& object) {
            if (!object.isVisible()) {
                return std::string();
            }
            bool needsPicture = object.hasPixelEffects() && !object.isImage() && !object.isSymbol();
            if (!needsPicture && !object.isImage() && !object.isSymbol()) {
                for (const AppearanceItem& item : object.appearance()) {
                    needsPicture = needsPicture || (item.visible && item.kind == AppearanceKind::Fill && item.paint.isPattern() && object.isClosed());
                }
            }
            if (needsPicture) {
                const BLBox b = object.paintBounds();
                const double width = b.x1 - b.x0, height = b.y1 - b.y0;
                if (!(width > 0.0) || !(height > 0.0)) {
                    return std::string();
                }
                const double scale = std::min(2.0, 4096.0 / std::max(width, height));
                BLImage image(int(std::ceil(width * scale)), int(std::ceil(height * scale)), BL_FORMAT_PRGB32);
                {
                    BLContext ctx(image);
                    ctx.clear_all();
                    ctx.set_transform(BLMatrix2D(scale, 0.0, 0.0, scale, -b.x0 * scale, -b.y0 * scale));
                    object.paint(ctx);
                    ctx.end();
                }
                BLMatrix2D placement = BLMatrix2D::make_scaling(1.0 / scale, 1.0 / scale);
                placement.post_translate(b.x0, b.y0);
                return drawPicture(image, placement);
            }
            return drawObjectBody(object);
        }

        std::string Writer::drawRange(const SketchDocument& document, std::size_t begin, std::size_t end, std::size_t depth) {
            std::string out;
            std::size_t i = begin;
            while (i < end) {
                const SketchObject& object = document.object(i);
                if (document.groupsOf(object).size() <= depth) {
                    out += drawObject(object);
                    ++i;
                    continue;
                }
                const std::size_t group = document.groupsOf(object)[depth];
                std::size_t j = i + 1;
                while (j < end && document.groupsOf(document.object(j)).size() > depth && document.groupsOf(document.object(j))[depth] == group) {
                    ++j;
                }
                if (document.groupProps(group).clip && j - i >= 2) {
                    // The mask is the topmost member: the others are drawn inside its outline.
                    std::string clip = "gsave\n";
                    pathOps(clip, document.object(j - 1).geometry());
                    out += clip + "clip newpath\n" + drawRange(document, i, j - 1, depth + 1) + "grestore\n";
                } else {
                    out += drawRange(document, i, j, depth + 1);
                }
                i = j;
            }
            return out;
        }

        std::string Writer::build(const SketchDocument& document) {
            const Artboard& board = document.activeArtboardRect();
            if (document.pageBackground()) {
                const BLRgba32 c = document.pageBackground()->toRgba32();
                background_ = {c.r() / 255.0, c.g() / 255.0, c.b() / 255.0};
            }
            std::string out = "%!PS-Adobe-3.0 EPSF-3.0\n%%Creator: curvy\n%%Title: " + board.name + "\n";
            out += "%%BoundingBox: 0 0 " + std::to_string(int(std::ceil(board.width))) + " " + std::to_string(int(std::ceil(board.height))) + "\n";
            out += "%%HiResBoundingBox: 0 0 " + num(board.width) + " " + num(board.height) + "\n";
            out += "%%LanguageLevel: 2\n%%Pages: 1\n%%EndComments\n%%BeginProlog\n%%EndProlog\n%%Page: 1 1\nsave\n";
            // Clip to the artboard, then work in document coordinates (y down).
            out += "newpath 0 0 moveto " + num(board.width) + " 0 lineto " + num(board.width) + " " + num(board.height) + " lineto 0 " + num(board.height) + " lineto closepath clip newpath\n";
            if (document.pageBackground()) {
                SketchColor opaque = *document.pageBackground();
                opaque.setAlpha(1.0f);
                setColor(out, opaque);
                out += "0 0 " + num(board.width) + " " + num(board.height) + " rectfill\n";
            }
            out += "[1 0 0 -1 " + num(-board.x) + " " + num(board.y + board.height) + "] concat\n";
            out += drawRange(document, 0, document.objectCount(), 0);
            out += "restore\nshowpage\n%%Trailer\n%%EOF\n";
            return out;
        }

    }

    std::string epsFromDocument(const SketchDocument& document) {
        Writer writer;
        return writer.build(document);
    }

    bool writeEps(const SketchDocument& document, const std::string& path) {
        try {
            const std::string data = epsFromDocument(document);
            std::ofstream out(path, std::ios::binary);
            out.write(data.data(), std::streamsize(data.size()));
            return bool(out);
        } catch (...) {
            return false;
        }
    }

}
