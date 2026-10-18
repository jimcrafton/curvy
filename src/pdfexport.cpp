#include "curvy/pdfexport.h"

#include "curvy/gzip.h"
#include "curvy/sketchdocument.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

// PDF export (see pdfexport.h). The file is assembled in memory: a list of numbered objects, a content stream for every page and every
// transparency group, and one shared resource dictionary (graphics states, images, forms and patterns) that all of them refer to.

namespace curvy {

    namespace {

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
            return num(m.m00) + " " + num(m.m01) + " " + num(m.m10) + " " + num(m.m11) + " " + num(m.m20) + " " + num(m.m21);
        }

        const char* blendName(BlendMode mode) {
            static const char* kNames[] = {"Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "ColorDodge", "ColorBurn", "HardLight", "SoftLight", "Difference", "Exclusion"};
            return kNames[std::size_t(mode)];
        }

        BLBox boundsOf(const BLPath& path) {
            BLBox box(0, 0, 0, 0);
            path.get_bounding_box(&box);
            return box;
        }

        bool intersects(const BLBox& a, const BLBox& b) {
            return a.x0 <= b.x1 && a.x1 >= b.x0 && a.y0 <= b.y1 && a.y1 >= b.y0;
        }

        class Writer {
        public:
            std::string build(const SketchDocument& document);

        private:
            // ---- objects ----
            int reserve() {
                objects_.emplace_back();
                return int(objects_.size());
            }
            int add(const std::string& body) {
                objects_.push_back(body);
                return int(objects_.size());
            }
            void set(int id, const std::string& body) { objects_[std::size_t(id) - 1] = body; }
            int addStream(const std::string& dict, const std::string& data, bool compress = true) {
                const std::string packed = compress ? zlibCompress(data) : data;
                return add("<< " + dict + (compress ? " /Filter /FlateDecode" : "") + " /Length " + std::to_string(packed.size()) + " >>\nstream\n" + packed + "\nendstream");
            }

            // ---- resources ----
            std::string graphicsState(double fillAlpha, double strokeAlpha, BlendMode blend);
            std::string imageObject(const BLImage& image);   // the XObject's name; the picture is premultiplied RGB32
            std::string addForm(const std::string& content, const BLBox& box);
            std::string shadingPattern(const SketchGradient& gradient, bool radial, const BLBox& box);
            std::string tilingPattern(const SketchPaint& paint);
            int gradientFunction(const SketchGradient& gradient);

            // ---- content ----
            void setColor(std::string& out, const SketchColor& color, bool stroke);
            // Selects `paint` as the fill / stroke color; false when it paints nothing. `alpha` receives the opacity the paint carries itself.
            bool applyPaint(std::string& out, const SketchPaint& paint, bool stroke, const BLBox& box, double& alpha);
            void pathOps(std::string& out, const BLPath& path);
            std::string fillPath(const BLPath& path, const SketchPaint& paint, double opacity, BlendMode blend);
            std::string drawItem(const SketchObject& object, std::size_t index);
            std::string drawBrush(const SketchObject& object, const AppearanceItem& item, std::size_t index);
            std::string drawParts(const std::vector<BrushPart>& parts, double opacity);
            std::string drawObject(const SketchObject& object);
            std::string drawObjectBody(const SketchObject& object);
            std::string drawPicture(const BLImage& image, const BLMatrix2D& placement);
            std::string drawRange(const SketchDocument& document, std::size_t begin, std::size_t end, std::size_t depth);
            std::string wrapped(double opacity, BlendMode blend, const BLBox& box, const std::function<std::string()>& content);

            std::vector<std::string> objects_;
            std::map<std::string, std::string> stateNames_;   // key -> name
            std::vector<std::pair<std::string, int>> states_, xobjects_, patterns_;
            std::map<const ImageData*, std::string> pictureNames_;
            BLMatrix2D patternBase_ = BLMatrix2D::make_identity();   // what a pattern's matrix is relative to (the page's flip, or none in a form)
            BLBox pageBox_ = BLBox(0, 0, 0, 0);
            int resourcesId_ = 0;
        };

        // ---- resources ----------------------------------------------------------------------------------------------------

        std::string Writer::graphicsState(double fillAlpha, double strokeAlpha, BlendMode blend) {
            const std::string key = num(fillAlpha) + "|" + num(strokeAlpha) + "|" + std::to_string(int(blend));
            auto found = stateNames_.find(key);
            if (found != stateNames_.end()) {
                return found->second;
            }
            const std::string name = "GS" + std::to_string(stateNames_.size() + 1);
            const int id = add("<< /Type /ExtGState /ca " + num(fillAlpha) + " /CA " + num(strokeAlpha) + " /BM /" + blendName(blend) + " >>");
            stateNames_[key] = name;
            states_.push_back({name, id});
            return name;
        }

        std::string Writer::imageObject(const BLImage& image) {
            BLImageData data;
            if (image.is_empty() || const_cast<BLImage&>(image).get_data(&data) != BL_SUCCESS || data.size.w <= 0 || data.size.h <= 0) {
                return std::string();
            }
            const int w = data.size.w, h = data.size.h;
            std::string rgb(std::size_t(w) * std::size_t(h) * 3, '\0'), alpha(std::size_t(w) * std::size_t(h), '\0');
            bool translucent = false;
            for (int y = 0; y < h; ++y) {
                const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
                for (int x = 0; x < w; ++x) {
                    const std::uint32_t p = row[x];
                    const std::uint32_t a = p >> 24;
                    std::uint32_t r = (p >> 16) & 255u, g = (p >> 8) & 255u, b = p & 255u;
                    if (a > 0 && a < 255) {   // premultiplied -> straight
                        r = std::min<std::uint32_t>(255u, r * 255u / a);
                        g = std::min<std::uint32_t>(255u, g * 255u / a);
                        b = std::min<std::uint32_t>(255u, b * 255u / a);
                    }
                    translucent = translucent || a != 255u;
                    const std::size_t at = std::size_t(y) * std::size_t(w) + std::size_t(x);
                    rgb[at * 3] = char(r);
                    rgb[at * 3 + 1] = char(g);
                    rgb[at * 3 + 2] = char(b);
                    alpha[at] = char(a);
                }
            }
            std::string dict = "/Type /XObject /Subtype /Image /Width " + std::to_string(w) + " /Height " + std::to_string(h) + " /ColorSpace /DeviceRGB /BitsPerComponent 8";
            if (translucent) {
                const int mask = addStream("/Type /XObject /Subtype /Image /Width " + std::to_string(w) + " /Height " + std::to_string(h) + " /ColorSpace /DeviceGray /BitsPerComponent 8", alpha);
                dict += " /SMask " + std::to_string(mask) + " 0 R";
            }
            const int id = addStream(dict, rgb);
            const std::string name = "Im" + std::to_string(xobjects_.size() + 1);
            xobjects_.push_back({name, id});
            return name;
        }

        std::string Writer::addForm(const std::string& content, const BLBox& box) {
            const int id = addStream("/Type /XObject /Subtype /Form /BBox [" + num(box.x0) + " " + num(box.y0) + " " + num(box.x1) + " " + num(box.y1) +
                "] /Group << /S /Transparency >> /Resources " + std::to_string(resourcesId_) + " 0 R", content);
            const std::string name = "Fm" + std::to_string(xobjects_.size() + 1);
            xobjects_.push_back({name, id});
            return name;
        }

        // The colors of a gradient as a PDF function over 0..1 (a stitching function of exponential interpolations between the stops).
        int Writer::gradientFunction(const SketchGradient& gradient) {
            struct Stop { double at; double rgb[3]; };
            std::vector<Stop> stops;
            for (const GradientStop& stop : gradient.stops) {
                const BLRgba32 c = stop.color.toRgba32();
                stops.push_back({std::min(1.0, std::max(0.0, stop.offset)), {c.r() / 255.0, c.g() / 255.0, c.b() / 255.0}});
            }
            if (stops.empty()) {
                stops.push_back({0.0, {0, 0, 0}});
            }
            std::stable_sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) { return a.at < b.at; });
            if (stops.front().at > 0.0) { stops.insert(stops.begin(), Stop{0.0, {stops.front().rgb[0], stops.front().rgb[1], stops.front().rgb[2]}}); }
            if (stops.back().at < 1.0) { stops.push_back(Stop{1.0, {stops.back().rgb[0], stops.back().rgb[1], stops.back().rgb[2]}}); }
            if (stops.size() < 2) { stops.push_back(stops.front()); stops.back().at = 1.0; }
            auto color = [](const double* c) { return "[" + num(c[0]) + " " + num(c[1]) + " " + num(c[2]) + "]"; };
            std::vector<int> pieces;
            for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
                pieces.push_back(add("<< /FunctionType 2 /Domain [0 1] /C0 " + color(stops[i].rgb) + " /C1 " + color(stops[i + 1].rgb) + " /N 1 >>"));
            }
            if (pieces.size() == 1) {
                return pieces.front();
            }
            std::string functions, bounds, encode;
            double last = 0.0;
            for (std::size_t i = 0; i < pieces.size(); ++i) {
                functions += std::to_string(pieces[i]) + " 0 R ";
                encode += "0 1 ";
                if (i + 1 < pieces.size()) {
                    last = std::max(last + 1e-6, stops[i + 1].at);   // bounds increase, even where two stops share a place
                    bounds += num(std::min(last, 1.0 - 1e-6 * double(pieces.size() - i))) + " ";
                }
            }
            return add("<< /FunctionType 3 /Domain [0 1] /Functions [" + functions + "] /Bounds [" + bounds + "] /Encode [" + encode + "] >>");
        }

        std::string Writer::shadingPattern(const SketchGradient& gradient, bool radial, const BLBox& box) {
            BLPoint start, end;
            gradient.axisPoints(box, radial, start, end);
            const int function = gradientFunction(gradient);
            std::string coords;
            if (radial) {
                coords = num(start.x) + " " + num(start.y) + " 0 " + num(start.x) + " " + num(start.y) + " " + num(std::max(1e-6, std::hypot(end.x - start.x, end.y - start.y)));
            } else {
                coords = num(start.x) + " " + num(start.y) + " " + num(end.x) + " " + num(end.y);
            }
            const int shading = add("<< /ShadingType " + std::string(radial ? "3" : "2") + " /ColorSpace /DeviceRGB /Coords [" + coords + "] /Function " + std::to_string(function) + " 0 R /Extend [true true] >>");
            BLMatrix2D matrix = BLMatrix2D::make_identity();
            if (gradient.hasAxis && (gradient.axisSkew != 0.0 || (radial && gradient.axisAspect != 1.0))) {
                matrix = gradient.axisMatrix(start, end, radial);
            }
            matrix.post_transform(patternBase_);
            const int pattern = add("<< /Type /Pattern /PatternType 2 /Shading " + std::to_string(shading) + " 0 R /Matrix [" + matrixText(matrix) + "] >>");
            const std::string name = "P" + std::to_string(patterns_.size() + 1);
            patterns_.push_back({name, pattern});
            return name;
        }

        std::string Writer::tilingPattern(const SketchPaint& paint) {
            const SymbolDef* def = paint.patternDef().get();
            if (def == nullptr || !def->content) {
                return std::string();
            }
            const double tw = def->box.x1 - def->box.x0, th = def->box.y1 - def->box.y0, scale = paint.patternScale();
            if (!(tw > 0.0) || !(th > 0.0) || !(scale > 0.0)) {
                return std::string();
            }
            const int pw = std::max(1, std::min(1024, int(std::lround(tw * scale * 2.0)))), ph = std::max(1, std::min(1024, int(std::lround(th * scale * 2.0))));
            const std::string image = imageObject(def->tile(pw, ph));
            if (image.empty()) {
                return std::string();
            }
            int imageId = 0;
            for (const auto& entry : xobjects_) {
                if (entry.first == image) { imageId = entry.second; }
            }
            BLMatrix2D matrix = BLMatrix2D::make_scaling(scale, scale);
            matrix.post_rotate(paint.patternAngle() * 3.14159265358979323846 / 180.0);
            matrix.post_transform(patternBase_);
            // The tile image is y-down: flip it into the cell.
            const std::string cell = "q " + num(tw) + " 0 0 " + num(-th) + " 0 " + num(th) + " cm /Tile Do Q\n";
            const int id = addStream("/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 " + num(tw) + " " + num(th) + "] /XStep " + num(tw) + " /YStep " + num(th) +
                " /Resources << /XObject << /Tile " + std::to_string(imageId) + " 0 R >> >> /Matrix [" + matrixText(matrix) + "]", cell);
            const std::string name = "P" + std::to_string(patterns_.size() + 1);
            patterns_.push_back({name, id});
            return name;
        }

        // ---- content -------------------------------------------------------------------------------------------------------

        void Writer::setColor(std::string& out, const SketchColor& color, bool stroke) {
            switch (color.space()) {
                case ColorSpace::CMYK:
                    out += num(color.component(0)) + " " + num(color.component(1)) + " " + num(color.component(2)) + " " + num(color.component(3)) + (stroke ? " K\n" : " k\n");
                    break;
                case ColorSpace::Gray:
                    out += num(color.component(0)) + (stroke ? " G\n" : " g\n");
                    break;
                default: {
                    const BLRgba32 c = color.toRgba32();
                    out += num(c.r() / 255.0) + " " + num(c.g() / 255.0) + " " + num(c.b() / 255.0) + (stroke ? " RG\n" : " rg\n");
                    break;
                }
            }
        }

        bool Writer::applyPaint(std::string& out, const SketchPaint& paint, bool stroke, const BLBox& box, double& alpha) {
            alpha = 1.0;
            if (paint.isNone()) {
                return false;
            }
            if (paint.isPattern()) {
                const std::string name = tilingPattern(paint);
                if (name.empty()) {
                    return false;
                }
                out += std::string("/Pattern ") + (stroke ? "CS" : "cs") + " /" + name + (stroke ? " SCN\n" : " scn\n");
                return true;
            }
            if (paint.isGradient() && !paint.gradient().stops.empty()) {
                const std::string name = shadingPattern(paint.gradient(), paint.kind() == SketchPaint::Kind::Radial, box);
                out += std::string("/Pattern ") + (stroke ? "CS" : "cs") + " /" + name + (stroke ? " SCN\n" : " scn\n");
                return true;
            }
            setColor(out, paint.color(), stroke);
            alpha = paint.color().alpha();
            return true;
        }

        void Writer::pathOps(std::string& out, const BLPath& path) {
            BLPathView view = path.view();
            BLPoint current(0, 0), start(0, 0);
            for (std::size_t i = 0; i < view.size; ++i) {
                const BLPoint p = view.vertex_data[i];
                switch (view.command_data[i]) {
                    case BL_PATH_CMD_MOVE:
                        out += num(p.x) + " " + num(p.y) + " m\n";
                        current = start = p;
                        break;
                    case BL_PATH_CMD_ON:
                        out += num(p.x) + " " + num(p.y) + " l\n";
                        current = p;
                        break;
                    case BL_PATH_CMD_QUAD:
                    case BL_PATH_CMD_CONIC: {   // [QUAD, ON] / [CONIC, WEIGHT, ON]: a quadratic raised to a cubic (a conic's weight is ignored)
                        const std::size_t endAt = view.command_data[i] == BL_PATH_CMD_QUAD ? i + 1 : i + 2;
                        if (endAt >= view.size) { break; }
                        const BLPoint e = view.vertex_data[endAt];
                        out += num(current.x + 2.0 / 3.0 * (p.x - current.x)) + " " + num(current.y + 2.0 / 3.0 * (p.y - current.y)) + " " +
                            num(e.x + 2.0 / 3.0 * (p.x - e.x)) + " " + num(e.y + 2.0 / 3.0 * (p.y - e.y)) + " " + num(e.x) + " " + num(e.y) + " c\n";
                        current = e;
                        i = endAt;
                        break;
                    }
                    case BL_PATH_CMD_CUBIC: {
                        if (i + 2 >= view.size) { break; }
                        const BLPoint c2 = view.vertex_data[i + 1], e = view.vertex_data[i + 2];
                        out += num(p.x) + " " + num(p.y) + " " + num(c2.x) + " " + num(c2.y) + " " + num(e.x) + " " + num(e.y) + " c\n";
                        current = e;
                        i += 2;
                        break;
                    }
                    case BL_PATH_CMD_CLOSE:
                        out += "h\n";
                        current = start;
                        break;
                    default:
                        break;
                }
            }
        }

        std::string Writer::fillPath(const BLPath& path, const SketchPaint& paint, double opacity, BlendMode blend) {
            if (path.size() == 0) {
                return std::string();
            }
            std::string out = "q\n";
            double alpha = 1.0;
            if (!applyPaint(out, paint, false, boundsOf(path), alpha)) {
                return std::string();
            }
            alpha *= opacity;
            if (alpha < 1.0 || blend != BlendMode::Normal) {
                out += "/" + graphicsState(alpha, alpha, blend) + " gs\n";
            }
            pathOps(out, path);
            return out + "f\nQ\n";
        }

        std::string Writer::drawParts(const std::vector<BrushPart>& parts, double opacity) {
            std::string out;
            for (const BrushPart& part : parts) {
                if (part.closed) {
                    out += fillPath(part.path, part.fill, opacity, BlendMode::Normal);
                }
                if (part.strokeWidth > 0.0 && !part.stroke.isNone()) {
                    std::string s = "q\n";
                    double alpha = 1.0;
                    if (applyPaint(s, part.stroke, true, boundsOf(part.path), alpha)) {
                        alpha *= opacity;
                        if (alpha < 1.0) { s += "/" + graphicsState(alpha, alpha, BlendMode::Normal) + " gs\n"; }
                        s += num(part.strokeWidth) + " w\n";
                        pathOps(s, part.path);
                        out += s + "S\nQ\n";
                    }
                }
            }
            return out;
        }

        std::string Writer::drawBrush(const SketchObject& object, const AppearanceItem& item, std::size_t index) {
            const BrushDef& brush = *item.strokeOptions.brush;
            const BLPath shown = object.itemOutline(index);
            if (brush.kind == BrushKind::Calligraphic) {
                return fillPath(brush.nibOutline(shown, item.strokeWidth), item.paint, item.opacity, item.blendMode);
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
            const AppearanceItem& item = object.appearance()[index];
            if (item.kind == AppearanceKind::Fill) {
                return object.isClosed() ? fillPath(object.itemOutline(index), item.paint, item.opacity, item.blendMode) : std::string();
            }
            if (item.kind != AppearanceKind::Stroke || item.paint.isNone() || item.strokeWidth <= 0.0) {
                return std::string();
            }
            if (item.strokeOptions.brush) {
                return drawBrush(object, item, index);
            }
            if (!item.strokeOptions.widths.empty()) {   // a variable width: the outline of the stroke, filled
                BLPath outline;
                return object.strokeOutline(index, outline) ? fillPath(outline, item.paint, item.opacity, item.blendMode) : std::string();
            }
            const BLPath shown = object.itemOutline(index);
            std::string out = "q\n";
            double alpha = 1.0;
            if (!applyPaint(out, item.paint, true, object.geometryBounds(), alpha)) {
                return std::string();
            }
            alpha *= item.opacity;
            if (alpha < 1.0 || item.blendMode != BlendMode::Normal) {
                out += "/" + graphicsState(alpha, alpha, item.blendMode) + " gs\n";
            }
            const StrokeOptions& options = item.strokeOptions;
            out += num(item.strokeWidth) + " w " + std::to_string(options.cap == StrokeCap::Butt ? 0 : (options.cap == StrokeCap::Round ? 1 : 2)) + " J " +
                std::to_string(options.join == StrokeJoin::Miter ? 0 : (options.join == StrokeJoin::Round ? 1 : 2)) + " j " + num(std::max(1.0, options.miterLimit)) + " M\n";
            if (!options.dashes.empty()) {
                double period = 0.0;
                std::string dashes;
                for (double d : options.dashes) { dashes += num(d) + " "; period += d; }
                if (period > 1e-6) { out += "[" + dashes + "] 0 d\n"; }
            }
            pathOps(out, shown);
            return out + "S\nQ\n";
        }

        std::string Writer::drawPicture(const BLImage& image, const BLMatrix2D& placement) {
            const std::string name = imageObject(image);
            if (name.empty()) {
                return std::string();
            }
            // The unit square of a PDF image has its top at y = 1; the picture's pixel box is y-down: flip it, then place it.
            BLMatrix2D m(double(image.width()), 0.0, 0.0, -double(image.height()), 0.0, double(image.height()));
            m.post_transform(placement);
            return "q\n" + matrixText(m) + " cm\n/" + name + " Do\nQ\n";
        }

        std::string Writer::wrapped(double opacity, BlendMode blend, const BLBox& box, const std::function<std::string()>& content) {
            if (opacity >= 1.0 && blend == BlendMode::Normal) {
                return content();
            }
            // Drawn into a transparency group, which is then placed with the opacity and blend mode (the pattern matrices inside are
            // relative to the group's own space, which is the document's).
            const BLMatrix2D saved = patternBase_;
            patternBase_ = BLMatrix2D::make_identity();
            const std::string inner = content();
            patternBase_ = saved;
            if (inner.empty()) {
                return std::string();
            }
            const std::string form = addForm(inner, BLBox(box.x0 - 2.0, box.y0 - 2.0, box.x1 + 2.0, box.y1 + 2.0));
            return "q\n/" + graphicsState(opacity, opacity, blend) + " gs\n/" + form + " Do\nQ\n";
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
                const BLMatrix2D m = object.symbolMatrix();
                const BLMatrix2D saved = patternBase_;
                patternBase_ = m;
                patternBase_.post_transform(saved);
                const std::string inner = drawRange(*def->content, 0, def->content->objectCount(), 0);
                patternBase_ = saved;
                return inner.empty() ? std::string() : "q\n" + matrixText(m) + " cm\n" + inner + "Q\n";
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
            if (object.hasPixelEffects() && !object.isImage() && !object.isSymbol()) {
                // Shadows, glows and blurs work on pixels: the object as it looks goes in as a picture (opacity and blend included).
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
            if (object.isImage() || object.isSymbol()) {
                return wrapped(object.opacity(), object.blendMode(), object.geometryBounds(), [&]() { return drawObjectBody(object); });
            }
            return wrapped(object.opacity(), object.blendMode(), object.paintBounds(), [&]() { return drawObjectBody(object); });
        }

        // The objects begin..end of `document` in stacking order, groups and all (the same walk as SketchDocument::paintRange).
        std::string Writer::drawRange(const SketchDocument& document, std::size_t begin, std::size_t end, std::size_t depth) {
            std::string out;
            std::size_t i = begin;
            while (i < end) {
                const SketchObject& object = document.object(i);
                if (object.groups().size() <= depth) {
                    if (intersects(object.paintBounds(), pageBox_) || true) {
                        out += drawObject(object);
                    }
                    ++i;
                    continue;
                }
                const std::size_t group = object.groups()[depth];
                std::size_t j = i + 1;
                while (j < end && document.object(j).groups().size() > depth && document.object(j).groups()[depth] == group) {
                    ++j;
                }
                const GroupProps props = document.groupProps(group);
                const bool clipped = props.clip && j - i >= 2;
                BLBox box(1e30, 1e30, -1e30, -1e30);
                for (std::size_t k = i; k < j; ++k) {
                    const BLBox b = document.object(k).paintBounds();
                    box = BLBox(std::min(box.x0, b.x0), std::min(box.y0, b.y0), std::max(box.x1, b.x1), std::max(box.y1, b.y1));
                }
                out += wrapped(props.opacity, props.blendMode, box, [&]() {
                    if (!clipped) {
                        return drawRange(document, i, j, depth + 1);
                    }
                    // The mask is the topmost member: the others are drawn inside its outline.
                    std::string clip = "q\n";
                    pathOps(clip, document.object(j - 1).geometry());
                    clip += "W n\n";
                    clip += drawRange(document, i, j - 1, depth + 1);
                    return clip + "Q\n";
                });
                i = j;
            }
            return out;
        }

        std::string Writer::build(const SketchDocument& document) {
            const int catalog = reserve(), pages = reserve();
            resourcesId_ = reserve();
            std::vector<int> pageIds;
            for (std::size_t a = 0; a < document.artboards().size(); ++a) {
                const Artboard& board = document.artboards()[a];
                pageBox_ = board.box();
                patternBase_ = BLMatrix2D(1.0, 0.0, 0.0, -1.0, -board.x, board.y + board.height);   // document (y down) -> page (y up)
                std::string content;
                if (document.pageBackground()) {
                    setColor(content, *document.pageBackground(), false);
                    content += "0 0 " + num(board.width) + " " + num(board.height) + " re f\n";
                }
                content += "q\n" + matrixText(patternBase_) + " cm\n" + drawRange(document, 0, document.objectCount(), 0) + "Q\n";
                const int stream = addStream("", content);
                pageIds.push_back(add("<< /Type /Page /Parent " + std::to_string(pages) + " 0 R /MediaBox [0 0 " + num(board.width) + " " + num(board.height) +
                    "] /Resources " + std::to_string(resourcesId_) + " 0 R /Contents " + std::to_string(stream) + " 0 R /Group << /S /Transparency /CS /DeviceRGB >> >>"));
            }
            std::string kids;
            for (int id : pageIds) { kids += std::to_string(id) + " 0 R "; }
            set(pages, "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pageIds.size()) + " >>");
            set(catalog, "<< /Type /Catalog /Pages " + std::to_string(pages) + " 0 R >>");
            std::string resources = "<< /ProcSet [/PDF /ImageB /ImageC]";
            auto list = [](const std::vector<std::pair<std::string, int>>& entries) {
                std::string text = "<< ";
                for (const auto& e : entries) { text += "/" + e.first + " " + std::to_string(e.second) + " 0 R "; }
                return text + ">>";
            };
            if (!states_.empty()) { resources += " /ExtGState " + list(states_); }
            if (!xobjects_.empty()) { resources += " /XObject " + list(xobjects_); }
            if (!patterns_.empty()) { resources += " /Pattern " + list(patterns_); }
            set(resourcesId_, resources + " >>");

            std::string file = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
            std::vector<std::size_t> offsets;
            for (std::size_t i = 0; i < objects_.size(); ++i) {
                offsets.push_back(file.size());
                file += std::to_string(i + 1) + " 0 obj\n" + objects_[i] + "\nendobj\n";
            }
            const std::size_t xref = file.size();
            file += "xref\n0 " + std::to_string(objects_.size() + 1) + "\n0000000000 65535 f \n";
            for (std::size_t offset : offsets) {
                char line[32];
                std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
                file += line;
            }
            file += "trailer\n<< /Size " + std::to_string(objects_.size() + 1) + " /Root " + std::to_string(catalog) + " 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
            return file;
        }

    }

    std::string pdfFromDocument(const SketchDocument& document) {
        Writer writer;
        return writer.build(document);
    }

    bool writePdf(const SketchDocument& document, const std::string& path) {
        try {
            const std::string data = pdfFromDocument(document);
            std::ofstream out(path, std::ios::binary);
            out.write(data.data(), std::streamsize(data.size()));
            return bool(out);
        } catch (...) {
            return false;
        }
    }

}
