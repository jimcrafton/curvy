#include "curvy/sketchbin.h"

#include "curvy/imagedata.h"

#include "curvy/gzip.h"
#include "curvy/sketchdocument.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>

namespace curvy {

    namespace {

        constexpr char kMagic[4] = {'S', 'K', 'B', '1'};
        // Version 2 stores the vertices of a path as 32-bit floats, each as the step from the one before (see writePath); version 1 stored
        // them as pairs of doubles. Both are read.
        constexpr std::uint16_t kVersion = 3;
        constexpr std::uint16_t kFlagCompressed = 1;
        constexpr std::size_t kHeaderSize = 8;

        // ---------------------------------------------------------------- writing

        // Appends little-endian numbers to a byte string. Counts and indices are LEB128 varints (small values take one byte).
        class ByteWriter {
        public:
            void u8(unsigned value) { bytes_.push_back(char(value)); }
            void u16(unsigned value) { raw(&value, 2); }
            void varint(std::uint64_t value) {
                while (value >= 0x80) {
                    bytes_.push_back(char((value & 0x7F) | 0x80));
                    value >>= 7;
                }
                bytes_.push_back(char(value));
            }
            void f32(float value) { raw(&value, 4); }
            void f64(double value) { raw(&value, 8); }
            void u64(std::uint64_t value) { raw(&value, 8); }
            void bytes(const void* data, std::size_t size) { raw(data, size); }
            void text(const std::string& value) {
                varint(value.size());
                bytes_.append(value);
            }
            const std::string& data() const { return bytes_; }
            std::string take() { return std::move(bytes_); }

        private:
            // The format is little-endian; every platform sketch builds for is too.
            void raw(const void* data, std::size_t size) { bytes_.append(static_cast<const char*>(data), size); }
            std::string bytes_;
        };

        void writeColor(ByteWriter& out, const SketchColor& color) {
            out.u8(unsigned(color.space()));
            for (std::size_t i = 0; i < 4; ++i) {
                out.f32(color.component(i));
            }
            out.f32(color.alpha());
        }

        void writeGradient(ByteWriter& out, const SketchGradient& g) {
            out.f64(g.angleDegrees);
            out.u8(unsigned(g.spread));
            out.u8((g.hasAxis ? 1u : 0u) | (g.alongStroke ? 2u : 0u));
            out.f64(g.axisX1);
            out.f64(g.axisY1);
            out.f64(g.axisX2);
            out.f64(g.axisY2);
            out.f64(g.axisAspect);
            out.f64(g.axisSkew);
            out.varint(g.stops.size());
            for (const GradientStop& stop : g.stops) {
                out.f64(stop.offset);
                writeColor(out, stop.color);
            }
        }

        void writePaint(ByteWriter& out, const SketchPaint& paint) {
            out.u8(unsigned(paint.kind()));
            if (paint.kind() == SketchPaint::Kind::Solid) {
                writeColor(out, paint.color());
            } else if (paint.isGradient()) {
                writeGradient(out, paint.gradient());
            } else if (paint.isPoints()) {
                out.varint(paint.gradient().points.size());
                for (const GradientPoint& point : paint.gradient().points) {
                    out.f64(point.x);
                    out.f64(point.y);
                    out.f64(point.radius);
                    writeColor(out, point.color);
                }
            } else if (paint.isPattern()) {
                out.varint(paint.patternId());
                out.f64(paint.patternScale());
                out.f64(paint.patternAngle());
            }
        }

        void writeItems(ByteWriter& out, const std::vector<AppearanceItem>& items) {
            out.varint(items.size());
            for (const AppearanceItem& item : items) {
                out.u8(unsigned(item.kind));
                out.u8(item.visible ? 1u : 0u);
                out.u8(unsigned(item.blendMode));
                out.f64(item.opacity);
                writePaint(out, item.paint);
                if (item.kind == AppearanceKind::Stroke) {
                    out.f64(item.strokeWidth);
                    out.u8(unsigned(item.strokeOptions.cap) | (item.strokeOptions.brush ? 0x80u : 0u) | (item.strokeOptions.decorated() ? 0x40u : 0u));   // bit 7: a brush follows; bit 6: align and arrows follow
                    out.u8(unsigned(item.strokeOptions.join));
                    out.f64(item.strokeOptions.miterLimit);
                    out.varint(item.strokeOptions.dashes.size());
                    for (double dash : item.strokeOptions.dashes) {
                        out.f64(dash);
                    }
                    out.varint(item.strokeOptions.widths.size());
                    for (const WidthPoint& w : item.strokeOptions.widths) {
                        out.f64(w.t);
                        out.f64(w.left);
                        out.f64(w.right);
                    }
                    if (item.strokeOptions.brush) {
                        out.varint(item.strokeOptions.brush->id);
                    }
                    if (item.strokeOptions.decorated()) {
                        out.u8(unsigned(item.strokeOptions.align));
                        out.u8(unsigned(item.strokeOptions.arrowStart));
                        out.u8(unsigned(item.strokeOptions.arrowEnd));
                        out.f64(item.strokeOptions.arrowScale);
                    }
                } else if (item.kind == AppearanceKind::Effect) {
                    out.u8(unsigned(item.effect));
                    writeColor(out, item.color);
                    out.f64(item.softness);
                    out.f64(item.offsetX);
                    out.f64(item.offsetY);
                    out.f64(item.radius);
                }
            }
        }

        // A string table: every distinct string once, objects refer to them by index. Index 0 is always the empty string.
        class StringTable {
        public:
            StringTable() { add(""); }
            std::uint32_t add(const std::string& value) {
                auto found = index_.find(value);
                if (found != index_.end()) {
                    return found->second;
                }
                std::uint32_t at = std::uint32_t(strings_.size());
                strings_.push_back(value);
                index_.emplace(value, at);
                return at;
            }
            const std::vector<std::string>& strings() const { return strings_; }

        private:
            std::vector<std::string> strings_;
            std::unordered_map<std::string, std::uint32_t> index_;
        };

        void chunk(std::string& body, const char* tag, const std::string& payload) {
            body.append(tag, 4);
            std::uint64_t size = payload.size();
            body.append(reinterpret_cast<const char*>(&size), 8);
            body.append(payload);
        }

        // A path: the command bytes, then the vertices as steps of 32-bit floats. Each step is measured from the vertex as the reader
        // will have it (not as it was), so rounding never builds up along a path; a vertex a path keeps for a Close (it has no
        // position) is left out. A coordinate that a float holds exactly - the usual case for drawn shapes - comes back exactly.
        void writePath(ByteWriter& out, const BLPath& path, unsigned version) {
            BLPathView view = path.view();
            out.varint(view.size);
            out.bytes(view.command_data, view.size);
            if (version < 2) {
                out.bytes(view.vertex_data, view.size * sizeof(BLPoint));
                return;
            }
            double x = 0.0, y = 0.0;
            for (std::size_t i = 0; i < view.size; ++i) {
                if (view.command_data[i] == BL_PATH_CMD_CLOSE) {
                    continue;
                }
                const float dx = float(view.vertex_data[i].x - x), dy = float(view.vertex_data[i].y - y);
                out.f32(dx);
                out.f32(dy);
                x += double(dx);
                y += double(dy);
            }
        }

        // ---------------------------------------------------------------- reading

        // Reads from a byte range; any read past the end (or a failed check) sets bad() and gives zeros from then on, so a
        // damaged file ends in one test of ok() instead of a check on every field.
        class ByteReader {
        public:
            ByteReader(const char* data, std::size_t size) : data_(data), size_(size) {}
            bool ok() const { return ok_; }
            void fail() { ok_ = false; }
            std::size_t left() const { return ok_ ? size_ - at_ : 0; }
            unsigned u8() { unsigned char v = 0; take(&v, 1); return v; }
            unsigned u16() { std::uint16_t v = 0; take(&v, 2); return v; }
            std::uint64_t u64() { std::uint64_t v = 0; take(&v, 8); return v; }
            float f32() { float v = 0; take(&v, 4); return v; }
            double f64() { double v = 0; take(&v, 8); return v; }
            std::uint64_t varint() {
                std::uint64_t value = 0;
                for (int shift = 0; shift < 64; shift += 7) {
                    unsigned byte = u8();
                    if (!ok_) {
                        return 0;
                    }
                    value |= std::uint64_t(byte & 0x7F) << shift;
                    if ((byte & 0x80) == 0) {
                        return value;
                    }
                }
                fail();   // more than ten bytes: not a number
                return 0;
            }
            // A count of things that each take at least `minBytes`: refused when the file cannot hold that many, so a damaged
            // count never makes a huge allocation.
            std::size_t count(std::size_t minBytes) {
                std::uint64_t n = varint();
                if (!ok_ || minBytes == 0 || n > left() / minBytes) {
                    if (n != 0) {
                        fail();
                    }
                    return 0;
                }
                return std::size_t(n);
            }
            // An enumeration value that must be below `limit`.
            unsigned enumeration(unsigned limit) {
                unsigned v = u8();
                if (v >= limit) {
                    fail();
                    return 0;
                }
                return v;
            }
            std::string text() {
                std::uint64_t n = varint();
                if (!ok_ || n > left()) {
                    fail();
                    return std::string();
                }
                std::string value(data_ + at_, std::size_t(n));
                at_ += std::size_t(n);
                return value;
            }
            const char* view(std::size_t size) {
                if (!ok_ || size > size_ - at_) {
                    fail();
                    return nullptr;
                }
                const char* at = data_ + at_;
                at_ += size;
                return at;
            }

        private:
            void take(void* out, std::size_t size) {
                if (!ok_ || size > size_ - at_) {
                    ok_ = false;
                    return;
                }
                std::memcpy(out, data_ + at_, size);
                at_ += size;
            }
            const char* data_;
            std::size_t size_;
            std::size_t at_ = 0;
            bool ok_ = true;
        };

        // The command bytes of a path as Blend2D lays them out: a path starts with a move, a quad is [QUAD, ON], a conic
        // [CONIC, WEIGHT, ON], a cubic [CUBIC, CUBIC, ON]. Anything else would make Blend2D read past what a segment owns.
        bool validPathCommands(const std::uint8_t* c, std::size_t n) {
            if (n == 0 || c[0] != BL_PATH_CMD_MOVE) {
                return false;
            }
            for (std::size_t i = 0; i < n;) {
                switch (c[i]) {
                    case BL_PATH_CMD_MOVE:
                    case BL_PATH_CMD_ON:
                    case BL_PATH_CMD_CLOSE:
                        i += 1;
                        break;
                    case BL_PATH_CMD_QUAD:
                        if (i + 1 >= n || c[i + 1] != BL_PATH_CMD_ON) {
                            return false;
                        }
                        i += 2;
                        break;
                    case BL_PATH_CMD_CONIC:
                        if (i + 2 >= n || c[i + 1] != BL_PATH_CMD_WEIGHT || c[i + 2] != BL_PATH_CMD_ON) {
                            return false;
                        }
                        i += 3;
                        break;
                    case BL_PATH_CMD_CUBIC:
                        if (i + 2 >= n || c[i + 1] != BL_PATH_CMD_CUBIC || c[i + 2] != BL_PATH_CMD_ON) {
                            return false;
                        }
                        i += 3;
                        break;
                    default:
                        return false;
                }
            }
            return true;
        }

        // Reads what writePath wrote (version 2) or the version 1 layout of raw doubles. False for a damaged or impossible path.
        bool readPath(ByteReader& in, unsigned version, BLPath& path) {
            const std::size_t count = in.count(version >= 2 ? 1 : 1 + sizeof(BLPoint));
            if (count == 0) {
                return in.ok();
            }
            const char* commands = in.view(count);
            if (!in.ok() || !validPathCommands(reinterpret_cast<const std::uint8_t*>(commands), count)) {
                return false;
            }
            if (version < 2) {
                const char* points = in.view(count * sizeof(BLPoint));
                if (!in.ok()) {
                    return false;
                }
                std::uint8_t* commandOut = nullptr;
                BLPoint* vertexOut = nullptr;
                if (path.modify_op(BL_MODIFY_OP_ASSIGN_FIT, count, &commandOut, &vertexOut) != BL_SUCCESS) {
                    return false;
                }
                std::memcpy(commandOut, commands, count);
                std::memcpy(vertexOut, points, count * sizeof(BLPoint));
                return true;
            }
            double x = 0.0, y = 0.0;
            auto next = [&]() {
                x += double(in.f32());
                y += double(in.f32());
                return BLPoint(x, y);
            };
            for (std::size_t i = 0; i < count && in.ok(); ++i) {
                switch (std::uint8_t(commands[i])) {
                    case BL_PATH_CMD_MOVE: path.move_to(next()); break;
                    case BL_PATH_CMD_ON: path.line_to(next()); break;
                    case BL_PATH_CMD_CLOSE: path.close(); break;
                    case BL_PATH_CMD_QUAD: {
                        BLPoint control = next(), end = next();   // [QUAD, ON]
                        path.quad_to(control, end);
                        ++i;
                        break;
                    }
                    case BL_PATH_CMD_CONIC: {
                        BLPoint control = next(), weight = next(), end = next();   // [CONIC, WEIGHT, ON]: the weight rides in x
                        path.conic_to(control, end, weight.x);
                        i += 2;
                        break;
                    }
                    case BL_PATH_CMD_CUBIC: {
                        BLPoint first = next(), second = next(), end = next();   // [CUBIC, CUBIC, ON]
                        path.cubic_to(first, second, end);
                        i += 2;
                        break;
                    }
                    default: return false;
                }
            }
            return in.ok();
        }

        SketchColor readColor(ByteReader& in) {
            ColorSpace space = ColorSpace(in.enumeration(4));
            float c[4];
            for (float& component : c) {
                component = in.f32();
            }
            float alpha = in.f32();
            switch (space) {
                case ColorSpace::CMYK: return SketchColor::cmyk(c[0], c[1], c[2], c[3], alpha);
                case ColorSpace::Gray: return SketchColor::gray(c[0], alpha);
                case ColorSpace::HSB: return SketchColor::hsb(c[0], c[1], c[2], alpha);
                default: return SketchColor::rgb(c[0], c[1], c[2], alpha);
            }
        }

        SketchGradient readGradient(ByteReader& in) {
            SketchGradient g;
            g.angleDegrees = in.f64();
            g.spread = GradientSpread(in.enumeration(3));
            unsigned flags = in.u8();
            g.hasAxis = (flags & 1u) != 0;
            g.alongStroke = (flags & 2u) != 0;
            g.axisX1 = in.f64();
            g.axisY1 = in.f64();
            g.axisX2 = in.f64();
            g.axisY2 = in.f64();
            g.axisAspect = in.f64();
            g.axisSkew = in.f64();
            std::size_t n = in.count(8 + 21);
            for (std::size_t i = 0; i < n && in.ok(); ++i) {
                GradientStop stop;
                stop.offset = in.f64();
                stop.color = readColor(in);
                g.stops.push_back(stop);
            }
            return g;
        }

        SketchPaint readPaint(ByteReader& in) {
            unsigned kind = in.enumeration(6);
            switch (SketchPaint::Kind(kind)) {
                case SketchPaint::Kind::Points: {
                    SketchGradient g;
                    std::size_t n = in.count(24 + 21);
                    for (std::size_t i = 0; i < n && in.ok(); ++i) {
                        GradientPoint point;
                        point.x = in.f64();
                        point.y = in.f64();
                        point.radius = in.f64();
                        point.color = readColor(in);
                        g.points.push_back(point);
                    }
                    return SketchPaint::points(g);
                }
                case SketchPaint::Kind::Pattern: {
                    std::size_t id = std::size_t(in.varint());
                    double scale = in.f64(), angle = in.f64();
                    if (!(scale > 0.0) || !std::isfinite(scale) || !std::isfinite(angle)) {
                        return SketchPaint::none();
                    }
                    return SketchPaint::patternRef(id, scale, angle);   // bound to its tile once the library is in
                }
                case SketchPaint::Kind::Solid: return SketchPaint::solid(readColor(in));
                case SketchPaint::Kind::Linear: return SketchPaint::linear(readGradient(in));
                case SketchPaint::Kind::Radial: return SketchPaint::radial(readGradient(in));
                default: return SketchPaint::none();
            }
        }

        std::vector<AppearanceItem> readItems(ByteReader& in) {
            std::vector<AppearanceItem> items;
            std::size_t n = in.count(12);
            for (std::size_t i = 0; i < n && in.ok(); ++i) {
                AppearanceItem item;
                item.kind = AppearanceKind(in.enumeration(3));
                item.visible = in.u8() != 0;
                item.blendMode = BlendMode(in.enumeration(12));
                item.opacity = in.f64();
                item.paint = readPaint(in);
                if (item.kind == AppearanceKind::Stroke) {
                    item.strokeWidth = in.f64();
                    const unsigned capByte = in.u8();
                    item.strokeOptions.cap = (capByte & 0x3Fu) < 3u ? StrokeCap(capByte & 0x3Fu) : StrokeCap::Butt;
                    item.strokeOptions.join = StrokeJoin(in.enumeration(3));
                    item.strokeOptions.miterLimit = in.f64();
                    std::size_t dashes = in.count(8);
                    for (std::size_t d = 0; d < dashes && in.ok(); ++d) {
                        item.strokeOptions.dashes.push_back(in.f64());
                    }
                    std::size_t widths = in.count(24);
                    for (std::size_t w = 0; w < widths && in.ok(); ++w) {
                        WidthPoint point;
                        point.t = in.f64();
                        point.left = in.f64();
                        point.right = in.f64();
                        item.strokeOptions.widths.push_back(point);
                    }
                    if ((capByte & 0x80u) != 0u) {
                        item.strokeOptions.brushRef = std::size_t(in.varint());   // bound to the brush once the library is in
                    }
                    if ((capByte & 0x40u) != 0u) {
                        item.strokeOptions.align = StrokeAlign(in.enumeration(3));
                        item.strokeOptions.arrowStart = ArrowStyle(in.enumeration(4));
                        item.strokeOptions.arrowEnd = ArrowStyle(in.enumeration(4));
                        item.strokeOptions.arrowScale = in.f64();
                    }
                } else if (item.kind == AppearanceKind::Effect) {
                    item.effect = EffectKind(in.enumeration(7));
                    item.color = readColor(in);
                    item.softness = in.f64();
                    item.offsetX = in.f64();
                    item.offsetY = in.f64();
                    item.radius = in.f64();
                }
                items.push_back(item);
            }
            return items;
        }

        // One appearance as it is shared between objects: the stack, whole-object opacity and blend, and the active rows.
        struct Look {
            std::vector<AppearanceItem> items;
            double opacity = 1.0;
            BlendMode blendMode = BlendMode::Normal;
            std::size_t activeFill = 0, activeStroke = 0, activeEffect = 0;
        };

    }

    // ---------------------------------------------------------------- the writer

    bool isBinaryDocument(const std::string& data) {
        return data.size() >= kHeaderSize && std::memcmp(data.data(), kMagic, 4) == 0;
    }

    std::string binaryFromDocument(const SketchDocument& document, bool compress, unsigned version) {
        version = version < 2 ? 1u : (version == 2 ? 2u : 3u);
        StringTable strings;

        // The appearances, each distinct one once.
        std::unordered_map<std::string, std::uint32_t> lookIndex;
        std::vector<std::string> looks;
        std::vector<std::uint32_t> lookOf(document.objectCount());
        // The groups in use.
        std::set<std::size_t> groupIds;

        std::map<const ImageData*, std::size_t> imageIndex;   // the pictures in use, each saved once
        std::vector<std::shared_ptr<const ImageData>> images;
        ByteWriter objects;
        objects.varint(document.objectCount());
        for (std::size_t i = 0; i < document.objectCount(); ++i) {
            const SketchObject& object = document.object(i);

            ByteWriter look;
            writeItems(look, object.appearance());
            look.f64(object.opacity());
            look.u8(unsigned(object.blendMode()));
            look.varint(object.activeFillIndex());
            look.varint(object.activeStrokeIndex());
            look.varint(object.activeEffectIndex());
            auto found = lookIndex.find(look.data());
            std::uint32_t lookAt;
            if (found == lookIndex.end()) {
                lookAt = std::uint32_t(looks.size());
                looks.push_back(look.data());
                lookIndex.emplace(look.data(), lookAt);
            } else {
                lookAt = found->second;
            }

            const BLMatrix2D& placed = object.paintToPage();
            const bool hasPlaced = version >= 3 && !(placed.m00 == 1.0 && placed.m01 == 0.0 && placed.m10 == 0.0 && placed.m11 == 1.0 && placed.m20 == 0.0 && placed.m21 == 0.0);
            const bool hasWarp = version >= 3 && !object.paintWarp().isIdentity();
            unsigned flags = (object.ownVisible() ? 1u : 0u) | (object.ownLocked() ? 2u : 0u) | (object.frameAngle() != 0.0 || hasPlaced || hasWarp ? 4u : 0u) |
                (object.groups().empty() ? 0u : 8u) | (object.isText() ? 16u : 0u) |
                (object.isText() && !object.textStyle().isDefault() ? 32u : 0u) | (object.isSymbol() && object.symbol() ? 64u : 0u) |
                (object.isText() && object.textWarp().active() ? 128u : 0u);
            objects.u8(unsigned(object.type()));
            objects.u8(flags);
            objects.varint(strings.add(object.name()));
            objects.varint(lookAt);
            if (flags & 4u) {
                objects.f64(object.frameAngle());
                if (version >= 3) {
                    objects.u8((hasPlaced ? 1u : 0u) | (hasWarp ? 2u : 0u));
                    if (hasPlaced) {
                        for (double v : {placed.m00, placed.m01, placed.m10, placed.m11, placed.m20, placed.m21}) {
                            objects.f64(v);
                        }
                    }
                    if (hasWarp) {
                        for (double v : object.paintWarp().m) {
                            objects.f64(v);
                        }
                    }
                }
            }
            if (flags & 8u) {
                objects.varint(object.groups().size());
                for (std::size_t g : object.groups()) {
                    objects.varint(g);
                    groupIds.insert(g);
                }
            }
            if (flags & 16u) {
                objects.varint(strings.add(object.text()));
                objects.varint(strings.add(object.fontName()));
                objects.f64(object.fontSize());
                const BLMatrix2D& m = object.textMatrix();
                objects.f64(m.m00);
                objects.f64(m.m01);
                objects.f64(m.m10);
                objects.f64(m.m11);
                objects.f64(m.m20);
                objects.f64(m.m21);
            }
            if (flags & 32u) {   // text style: weight / slant / alignment, tracking, leading
                const TextStyle& style = object.textStyle();
                objects.u8((style.bold ? 1u : 0u) | (style.italic ? 2u : 0u) | (unsigned(style.align) << 2));
                objects.f64(style.tracking);
                objects.f64(style.leading);
            }
            if (flags & 64u) {
                objects.varint(object.symbol()->id);
            }
            if (object.isImage()) {   // which picture of the IMGS chunk (its position there)
                auto at = imageIndex.find(object.image().get());
                if (at == imageIndex.end()) {
                    at = imageIndex.emplace(object.image().get(), images.size()).first;
                    images.push_back(object.image());
                }
                objects.varint(at->second);
            }
            if (flags & 128u) {   // text on a path / between two paths
                const TextWarp& warp = object.textWarp();
                objects.u8(unsigned(warp.kind) | (warp.flip ? 0x80u : 0u));
                objects.f64(warp.start);
                writePath(objects, warp.first, version);
                writePath(objects, warp.second, version);
            }
            writePath(objects, object.geometry(), version);
        }

        ByteWriter page;
        page.f32(document.pageWidth());
        page.f32(document.pageHeight());
        page.f32(document.ppi());
        page.u8(document.colorMode() == ColorSpace::CMYK ? 1u : 0u);
        if (document.pageBackground()) {
            page.u8(1);
            writeColor(page, *document.pageBackground());
        } else {
            page.u8(0);
        }

        ByteWriter guideChunk;
        guideChunk.varint(document.guides().size());
        for (const Guide& guide : document.guides()) {
            guideChunk.u8(guide.vertical ? 1u : 0u);
            guideChunk.f64(guide.position);
        }

        // Layers: the tree (id, parent, flags, name), then the stacking order as runs of (layer id, object count).
        ByteWriter layerChunk;
        layerChunk.varint(document.layers().size());
        for (const Layer& layer : document.layers()) {
            layerChunk.varint(layer.id);
            layerChunk.varint(layer.parent);
            layerChunk.u8((layer.visible ? 0u : 1u) | (layer.locked ? 2u : 0u) | (layer.expanded ? 0u : 4u) | (layer.color != 0 ? 8u : 0u));
            layerChunk.varint(strings.add(layer.name));
            if (layer.color != 0) {
                layerChunk.varint(layer.color);
            }
        }
        {
            std::vector<std::pair<std::size_t, std::size_t>> runs;
            for (std::size_t i = 0; i < document.objectCount(); ++i) {
                std::size_t layer = document.object(i).layer();
                if (runs.empty() || runs.back().first != layer) {
                    runs.push_back({layer, 0});
                }
                ++runs.back().second;
            }
            layerChunk.varint(runs.size());
            for (const auto& run : runs) {
                layerChunk.varint(run.first);
                layerChunk.varint(run.second);
            }
        }

        ByteWriter swatchChunk;
        swatchChunk.varint(document.swatches().size());
        for (const std::string& swatch : document.swatches()) {
            swatchChunk.text(swatch);
        }

        ByteWriter groups;
        groups.varint(groupIds.size());
        for (std::size_t id : groupIds) {
            GroupProps props = document.groupProps(id);
            groups.varint(id);
            groups.f64(props.opacity);
            groups.u8(unsigned(props.blendMode));
        }

        ByteWriter boards;
        boards.varint(document.artboards().size());
        boards.varint(document.activeArtboard());
        for (const Artboard& board : document.artboards()) {
            boards.text(board.name);
            boards.f64(board.x);
            boards.f64(board.y);
            boards.f64(board.width);
            boards.f64(board.height);
        }

        ByteWriter symbolChunk;
        symbolChunk.varint(document.symbols().size());
        for (const std::shared_ptr<const SymbolDef>& def : document.symbols()) {
            symbolChunk.varint(def->id);
            symbolChunk.text(def->name);
            symbolChunk.f64(def->box.x0);
            symbolChunk.f64(def->box.y0);
            symbolChunk.f64(def->box.x1);
            symbolChunk.f64(def->box.y1);
            symbolChunk.text(def->content ? binaryFromDocument(*def->content, false) : std::string());
        }

        ByteWriter patternChunk;
        patternChunk.varint(document.patterns().size());
        for (const std::shared_ptr<const SymbolDef>& def : document.patterns()) {
            patternChunk.varint(def->id);
            patternChunk.text(def->name);
            patternChunk.f64(def->box.x0);
            patternChunk.f64(def->box.y0);
            patternChunk.f64(def->box.x1);
            patternChunk.f64(def->box.y1);
            patternChunk.text(def->content ? binaryFromDocument(*def->content, false) : std::string());
        }

        ByteWriter brushChunk;
        brushChunk.varint(document.brushes().size());
        for (const std::shared_ptr<const BrushDef>& def : document.brushes()) {
            brushChunk.varint(def->id);
            brushChunk.text(def->name);
            brushChunk.u8(unsigned(def->kind));
            for (double value : def->params()) {
                brushChunk.f64(value);
            }
            const bool art = def->hasArt() && def->art && def->art->content;
            brushChunk.f64(art ? def->art->box.x0 : 0.0);
            brushChunk.f64(art ? def->art->box.y0 : 0.0);
            brushChunk.f64(art ? def->art->box.x1 : 1.0);
            brushChunk.f64(art ? def->art->box.y1 : 1.0);
            brushChunk.text(art ? binaryFromDocument(*def->art->content, false) : std::string());
        }

        ByteWriter imageChunk;
        imageChunk.varint(images.size());
        for (const std::shared_ptr<const ImageData>& image : images) {
            imageChunk.text(image ? image->name : std::string());
            imageChunk.text(image ? image->bytes : std::string());
        }

        ByteWriter clips;
        {
            std::vector<std::size_t> clipIds;
            for (std::size_t id : groupIds) {
                if (document.groupProps(id).clip) {
                    clipIds.push_back(id);
                }
            }
            clips.varint(clipIds.size());
            for (std::size_t id : clipIds) {
                clips.varint(id);
            }
        }

        ByteWriter styles;
        styles.varint(document.graphicStyles().size());
        for (const GraphicStyle& style : document.graphicStyles()) {
            styles.varint(strings.add(style.name));
            writeItems(styles, style.items);
            styles.f64(style.opacity);
            styles.u8(unsigned(style.blendMode));
        }

        ByteWriter stringChunk;
        stringChunk.varint(strings.strings().size());
        for (const std::string& value : strings.strings()) {
            stringChunk.text(value);
        }

        ByteWriter lookChunk;
        lookChunk.varint(looks.size());
        for (const std::string& look : looks) {
            lookChunk.bytes(look.data(), look.size());
        }

        std::string body;
        chunk(body, "PAGE", page.data());
        chunk(body, "GUID", guideChunk.data());
        chunk(body, "LAYR", layerChunk.data());
        chunk(body, "SWCH", swatchChunk.data());
        chunk(body, "STRS", stringChunk.data());
        chunk(body, "STYL", lookChunk.data());
        chunk(body, "GRPS", groups.data());
        chunk(body, "CLIP", clips.data());
        chunk(body, "SYMB", symbolChunk.data());
        chunk(body, "PATN", patternChunk.data());
        chunk(body, "BRSH", brushChunk.data());
        chunk(body, "IMGS", imageChunk.data());
        chunk(body, "ABRD", boards.data());
        chunk(body, "GSTY", styles.data());
        chunk(body, "OBJS", objects.data());

        ByteWriter file;
        file.bytes(kMagic, 4);
        file.u16(std::uint16_t(version));
        file.u16(compress ? kFlagCompressed : 0u);
        std::string result = file.take();
        result += compress ? gzipCompress(body, 3) : body;
        return result;
    }

    // ---------------------------------------------------------------- the reader

    bool documentFromBinary(const std::string& data, SketchDocument& document) {
        if (!isBinaryDocument(data)) {
            return false;
        }
        SketchDocument::Batch batch(document);   // one announcement at the end, not one per object
        ByteReader head(data.data() + 4, 4);
        unsigned version = head.u16();
        unsigned flags = head.u16();
        if (version == 0 || version > kVersion) {
            return false;   // written by a newer sketch than this one understands
        }
        std::string inflated;
        const char* body = data.data() + kHeaderSize;
        std::size_t bodySize = data.size() - kHeaderSize;
        if (flags & kFlagCompressed) {
            if (!gzipDecompress(data.substr(kHeaderSize), inflated)) {
                return false;
            }
            body = inflated.data();
            bodySize = inflated.size();
        }

        // Everything is read into these first; the document is only touched once the whole file has been accepted.
        float pageWidth = 612.0f, pageHeight = 792.0f, ppi = 72.0f;
        ColorSpace colorMode = ColorSpace::RGB;
        std::optional<SketchColor> background;
        std::vector<std::string> strings;
        std::vector<Look> looks;
        std::map<std::size_t, GroupProps> groupProps;
        std::set<std::size_t> clipGroups;
        std::vector<Artboard> artboards;
        std::size_t artboardActive = 0;
        std::vector<GraphicStyle> graphicStyles;
        std::vector<Guide> guides;
        std::vector<std::string> swatches = defaultSwatches();
        std::vector<Layer> layers;
        std::vector<std::size_t> layerNames;                              // string index per layer (resolved once the strings are in)
        std::vector<std::pair<std::size_t, std::size_t>> layerRuns;       // (layer id, object count) in stacking order
        std::vector<std::unique_ptr<SketchObject>> objects;
        std::vector<std::size_t> symbolRefs;   // the symbol id of each object (0 = not an instance)
        std::vector<std::shared_ptr<const SymbolDef>> symbolLibrary, patternLibrary;
        std::vector<std::shared_ptr<const BrushDef>> brushLibrary;
        std::vector<std::shared_ptr<const ImageData>> pictures;   // the IMGS chunk (a picture that cannot be decoded is null)
        std::vector<std::size_t> imageRefs;                       // the picture of each object (an index into `pictures`)
        bool havePage = false, haveObjects = false;

        ByteReader file(body, bodySize);
        while (file.left() > 0) {
            const char* tagBytes = file.view(4);
            std::uint64_t size = file.u64();
            if (!file.ok() || size > file.left()) {
                return false;
            }
            std::string tag(tagBytes, 4);
            ByteReader in(file.view(std::size_t(size)), std::size_t(size));
            if (!file.ok()) {
                return false;
            }

            if (tag == "PAGE") {
                pageWidth = in.f32();
                pageHeight = in.f32();
                ppi = in.f32();
                colorMode = in.u8() == 1u ? ColorSpace::CMYK : ColorSpace::RGB;
                if (in.u8() == 1u) {
                    background = readColor(in);
                }
                if (!in.ok() || !(pageWidth > 0.0f) || !(pageHeight > 0.0f) || !(ppi > 0.0f) || !std::isfinite(pageWidth) || !std::isfinite(pageHeight)) {
                    return false;
                }
                havePage = true;
            } else if (tag == "GUID") {
                std::size_t n = in.count(9);
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    Guide guide;
                    guide.vertical = in.u8() == 1u;
                    guide.position = in.f64();
                    if (std::isfinite(guide.position)) {
                        guides.push_back(guide);
                    }
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "SWCH") {
                std::size_t n = in.count(1);
                swatches.clear();
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    swatches.push_back(in.text());
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "LAYR") {
                std::size_t n = in.count(4);
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    Layer layer;
                    layer.id = std::size_t(in.varint());
                    layer.parent = std::size_t(in.varint());
                    unsigned flags = in.u8();
                    layer.visible = (flags & 1u) == 0;
                    layer.locked = (flags & 2u) != 0;
                    layer.expanded = (flags & 4u) == 0;
                    layerNames.push_back(std::size_t(in.varint()));
                    if ((flags & 8u) != 0) {
                        layer.color = std::uint32_t(in.varint()) & 0xFFFFFFu;
                    }
                    if (layer.id != 0) {
                        layers.push_back(layer);
                    } else {
                        layerNames.pop_back();
                    }
                }
                std::size_t runs = in.count(2);
                for (std::size_t i = 0; i < runs && in.ok(); ++i) {
                    std::size_t id = std::size_t(in.varint());
                    layerRuns.push_back({id, std::size_t(in.varint())});
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "STRS") {
                std::size_t n = in.count(1);
                strings.reserve(n);
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    strings.push_back(in.text());
                }
                if (!in.ok() || strings.empty()) {
                    return false;
                }
            } else if (tag == "STYL") {
                std::size_t n = in.count(8);
                looks.reserve(n);
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    Look look;
                    look.items = readItems(in);
                    look.opacity = in.f64();
                    look.blendMode = BlendMode(in.enumeration(12));
                    look.activeFill = std::size_t(in.varint());
                    look.activeStroke = std::size_t(in.varint());
                    look.activeEffect = std::size_t(in.varint());
                    if (!(look.opacity >= 0.0 && look.opacity <= 1.0)) {
                        return false;
                    }
                    looks.push_back(std::move(look));
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "GRPS") {
                std::size_t n = in.count(10);
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    std::size_t id = std::size_t(in.varint());
                    GroupProps props;
                    props.opacity = in.f64();
                    props.blendMode = BlendMode(in.enumeration(12));
                    groupProps[id] = props;
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "SYMB" || tag == "PATN") {
                std::vector<std::shared_ptr<const SymbolDef>>& target = tag == "SYMB" ? symbolLibrary : patternLibrary;
                std::size_t n = in.count(40);
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    auto def = std::make_shared<SymbolDef>();
                    def->id = std::size_t(in.varint());
                    def->name = in.text();
                    double x0 = in.f64(), y0 = in.f64(), x1 = in.f64(), y1 = in.f64();
                    std::string blob = in.text();
                    if (!in.ok() || !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) || !(x1 > x0) || !(y1 > y0)) {
                        return false;
                    }
                    def->box = BLBox(x0, y0, x1, y1);
                    def->content = std::make_shared<SketchDocument>();
                    if (!documentFromBinary(blob, *def->content)) {
                        return false;
                    }
                    target.push_back(def);
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "IMGS") {
                std::size_t n = in.count(2);
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    std::string name = in.text();
                    std::string bytes = in.text();
                    if (!in.ok()) {
                        return false;
                    }
                    pictures.push_back(decodeImage(bytes, name));
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "BRSH") {
                std::size_t n = in.count(50);
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    auto def = std::make_shared<BrushDef>();
                    def->id = std::size_t(in.varint());
                    def->name = in.text();
                    const unsigned kind = in.enumeration(3);
                    def->kind = BrushKind(kind);
                    std::vector<double> numbers;
                    for (int k = 0; k < 7; ++k) {
                        numbers.push_back(in.f64());
                    }
                    double x0 = in.f64(), y0 = in.f64(), x1 = in.f64(), y1 = in.f64();
                    std::string blob = in.text();
                    if (!in.ok()) {
                        return false;
                    }
                    def->setParams(numbers);
                    if (def->hasArt()) {
                        if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) || !(x1 > x0) || !(y1 > y0)) {
                            return false;
                        }
                        auto art = std::make_shared<SymbolDef>();
                        art->id = def->id;
                        art->name = def->name;
                        art->box = BLBox(x0, y0, x1, y1);
                        art->content = std::make_shared<SketchDocument>();
                        if (!documentFromBinary(blob, *art->content)) {
                            return false;
                        }
                        def->art = art;
                    }
                    if (def->id != 0) {
                        brushLibrary.push_back(def);
                    }
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "ABRD") {
                std::size_t n = in.count(33);
                artboardActive = std::size_t(in.varint());
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    Artboard board;
                    board.name = in.text();
                    board.x = in.f64();
                    board.y = in.f64();
                    board.width = in.f64();
                    board.height = in.f64();
                    if (std::isfinite(board.x) && std::isfinite(board.y) && std::isfinite(board.width) && std::isfinite(board.height) &&
                        board.width >= 1.0 && board.height >= 1.0) {
                        artboards.push_back(board);
                    }
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "CLIP") {
                std::size_t n = in.count(1);
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    clipGroups.insert(std::size_t(in.varint()));
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "GSTY") {
                std::size_t n = in.count(11);
                for (std::size_t i = 0; i < n && in.ok(); ++i) {
                    GraphicStyle style;
                    std::size_t nameAt = std::size_t(in.varint());
                    if (nameAt >= strings.size()) {
                        return false;
                    }
                    style.name = strings[nameAt];
                    style.items = readItems(in);
                    style.opacity = in.f64();
                    style.blendMode = BlendMode(in.enumeration(12));
                    graphicStyles.push_back(std::move(style));
                }
                if (!in.ok()) {
                    return false;
                }
            } else if (tag == "OBJS") {
                if (strings.empty()) {
                    return false;   // the strings and appearances come first
                }
                std::size_t n = in.count(4);
                objects.reserve(n);
                for (std::size_t i = 0; i < n; ++i) {
                    unsigned type = in.enumeration(unsigned(SketchObjectType::Image) + 1);
                    unsigned objectFlags = in.u8();
                    std::size_t nameAt = std::size_t(in.varint());
                    std::size_t lookAt = std::size_t(in.varint());
                    if (!in.ok() || nameAt >= strings.size() || lookAt >= looks.size()) {
                        return false;
                    }
                    double frameAngle = (objectFlags & 4u) ? in.f64() : 0.0;
                    BLMatrix2D paintMatrix = BLMatrix2D::make_rotation(frameAngle);
                    PaintWarp paintWarp;
                    if ((objectFlags & 4u) && version >= 3) {
                        const unsigned placedBits = in.u8();
                        if (placedBits & 1u) {
                            const double a = in.f64(), b = in.f64(), c = in.f64(), d = in.f64(), e = in.f64(), f = in.f64();
                            paintMatrix = BLMatrix2D(a, b, c, d, e, f);
                        }
                        if (placedBits & 2u) {
                            for (double& value : paintWarp.m) {
                                value = in.f64();
                            }
                        }
                    }
                    std::vector<std::size_t> objectGroups;
                    if (objectFlags & 8u) {
                        std::size_t g = in.count(1);
                        for (std::size_t k = 0; k < g && in.ok(); ++k) {
                            objectGroups.push_back(std::size_t(in.varint()));
                        }
                    }
                    std::string words, font;
                    double fontSize = 24.0;
                    BLMatrix2D matrix = BLMatrix2D::make_identity();
                    if (objectFlags & 16u) {
                        std::size_t wordsAt = std::size_t(in.varint());
                        std::size_t fontAt = std::size_t(in.varint());
                        if (wordsAt >= strings.size() || fontAt >= strings.size()) {
                            return false;
                        }
                        words = strings[wordsAt];
                        font = strings[fontAt];
                        fontSize = in.f64();
                        double m[6];
                        for (double& value : m) {
                            value = in.f64();
                        }
                        matrix = BLMatrix2D(m[0], m[1], m[2], m[3], m[4], m[5]);
                        if (!(fontSize > 0.0) || !std::isfinite(fontSize)) {
                            return false;
                        }
                    }
                    TextStyle textStyle;
                    if (objectFlags & 32u) {
                        unsigned bits = in.u8();
                        textStyle.bold = (bits & 1u) != 0;
                        textStyle.italic = (bits & 2u) != 0;
                        textStyle.align = int((bits >> 2) & 3u) > 2 ? 0 : int((bits >> 2) & 3u);
                        textStyle.tracking = in.f64();
                        textStyle.leading = in.f64();
                        if (!std::isfinite(textStyle.tracking) || !(textStyle.leading > 0.0) || !std::isfinite(textStyle.leading)) {
                            return false;
                        }
                    }
                    std::size_t symbolRef = 0;
                    if (objectFlags & 64u) {
                        symbolRef = std::size_t(in.varint());
                    }
                    std::size_t imageRef = 0;
                    if (type == unsigned(SketchObjectType::Image)) {
                        imageRef = std::size_t(in.varint());
                    }
                    TextWarp textWarp;
                    if (objectFlags & 128u) {
                        const unsigned kindBits = in.u8();
                        textWarp.kind = TextWarp::Kind(kindBits & 0x7Fu);
                        textWarp.flip = (kindBits & 0x80u) != 0;
                        textWarp.start = in.f64();
                        if ((kindBits & 0x7Fu) > 2u || !std::isfinite(textWarp.start) || !readPath(in, version, textWarp.first) || !readPath(in, version, textWarp.second)) {
                            return false;
                        }
                    }
                    BLPath path;
                    if (!readPath(in, version, path)) {
                        return false;
                    }
                    if (!in.ok()) {
                        return false;
                    }
                    std::unique_ptr<SketchObject> object(SketchObject::make(SketchObjectType(type), path));
                    if (SketchObjectType(type) == SketchObjectType::Text) {
                        object->setTextState(words, font, fontSize, matrix);
                        object->setTextStyle(textStyle);
                        if (textWarp.active()) {
                            object->setTextWarp(textWarp);
                        }
                        object->setGeometry(path);   // the outline as saved, whatever the fonts here would make of the words
                    }
                    const Look& look = looks[lookAt];
                    object->setAppearance(look.items);
                    object->setActiveIndices(look.activeFill, look.activeStroke, look.activeEffect);
                    object->setOpacity(look.opacity);
                    object->setBlendMode(look.blendMode);
                    object->setName(strings[nameAt]);
                    object->setVisible((objectFlags & 1u) != 0);
                    object->setLocked((objectFlags & 2u) != 0);
                    object->setFrameAngle(frameAngle);
                    object->setPaintMatrix(paintMatrix);
                    object->setPaintWarp(paintWarp);
                    object->setGroups(objectGroups);
                    objects.push_back(std::move(object));
                    symbolRefs.push_back(symbolRef);
                    imageRefs.push_back(imageRef);
                }
                if (!in.ok()) {
                    return false;
                }
                haveObjects = true;
            }
            // any other chunk: skipped (its bytes were already consumed from `file`)
        }
        if (!havePage || !haveObjects) {
            return false;
        }

        // Accepted: install it. Group ids in the file are only labels; each gets a fresh id from the document.
        document.clearObjects();
        document.setPageSize(pageWidth, pageHeight);
        if (!artboards.empty()) {
            document.setArtboards(artboards, artboardActive);
        }
        document.setPpi(ppi);
        document.setPageBackground(background);
        document.setColorMode(colorMode);                // converts the document's defaults and styles into the mode, so
        document.setGraphicStyles(graphicStyles);
        document.setGuides(guides);        // the saved styles go in after it, exactly as they were written
        for (std::size_t i = 0; i < layers.size(); ++i) {
            layers[i].name = layerNames[i] < strings.size() ? strings[layerNames[i]] : "Layer " + std::to_string(layers[i].id);
            bool known = false;
            for (const Layer& p : layers) {
                known = known || (p.id == layers[i].parent && p.id != layers[i].id);
            }
            if (!known) {
                layers[i].parent = 0;
            }
        }
        document.setSwatches(swatches);
        document.setSymbols(symbolLibrary);
        document.setPatterns(patternLibrary);   // the paints that name a tile are bound to it once the objects are in
        document.setBrushes(brushLibrary);      // likewise the strokes that name a brush
        document.setLayers(layers);   // empty = the one default layer
        std::size_t runAt = 0, runUsed = 0;
        std::map<std::size_t, std::size_t> groupIds;
        std::size_t objectAt = 0;
        for (std::unique_ptr<SketchObject>& object : objects) {
            if (object->type() == SketchObjectType::Image) {
                if (imageRefs[objectAt] < pictures.size() && pictures[imageRefs[objectAt]]) {
                    object->setImage(pictures[imageRefs[objectAt]]);
                } else {
                    object.reset(SketchObject::make(SketchObjectType::Path, object->geometry()));   // its picture is missing: keep the outline
                }
            }
            if (object && object->type() == SketchObjectType::Symbol) {
                std::shared_ptr<const SymbolDef> def = document.findSymbol(symbolRefs[objectAt]);
                if (def) {
                    object->setSymbol(def);
                } else {
                    object.reset(SketchObject::make(SketchObjectType::Path, object->geometry()));   // its artwork is missing: keep the outline
                }
            }
            ++objectAt;
            while (runAt < layerRuns.size() && runUsed >= layerRuns[runAt].second) {
                ++runAt;
                runUsed = 0;
            }
            if (runAt < layerRuns.size()) {
                object->setLayer(layerRuns[runAt].first);
                ++runUsed;
            }
            std::vector<std::size_t> mapped;
            for (std::size_t g : object->groups()) {
                auto found = groupIds.find(g);
                if (found == groupIds.end()) {
                    std::size_t fresh = document.newGroupId();
                    found = groupIds.emplace(g, fresh).first;
                    auto props = groupProps.find(g);
                    GroupProps restored = props != groupProps.end() ? props->second : GroupProps();
                    restored.clip = clipGroups.count(g) != 0;
                    if (!restored.isDefault()) {
                        document.setGroupProps(fresh, restored);
                    }
                }
                mapped.push_back(found->second);
            }
            object->setGroups(mapped);
            document.add(object.release());
        }
        document.bindPatternPaints();
        document.bindBrushStrokes();
        document.undoStack().clear();
        document.clearSelection();
        return true;
    }

}
