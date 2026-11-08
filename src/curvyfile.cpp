#include "curvy/curvyfile.h"

#include "curvy/imagedata.h"
#include "curvy/pathnode.h"
#include "curvy/sketchdocument.h"

#include <jitnodes/graph/serializeValue.h>
#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>
#include <json5/json5_input.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace curvy {

    namespace {

        constexpr int kVersion = 2;   // 2: no object table, the graph holds the objects; 1 is still read

        // ---------------------------------------------------------------- a small document model, written with a layout people can read

        struct Json {
            enum class Type { Null, Bool, Num, Str, Arr, Obj };
            Type type = Type::Null;
            bool flag = false;
            double number = 0.0;
            std::string text;
            std::vector<Json> items;
            std::vector<std::pair<std::string, Json>> members;

            static Json boolean(bool value) { Json j; j.type = Type::Bool; j.flag = value; return j; }
            static Json num(double value) { Json j; j.type = Type::Num; j.number = value; return j; }
            static Json str(std::string value) { Json j; j.type = Type::Str; j.text = std::move(value); return j; }
            static Json arr() { Json j; j.type = Type::Arr; return j; }
            static Json obj() { Json j; j.type = Type::Obj; return j; }
            // A float as the shortest decimal that reads back to the same float, so 0.1f is written 0.1 and not 0.10000000149011612.
            static Json real(float value) {
                char buffer[32];
                const auto written = std::to_chars(buffer, buffer + sizeof(buffer), value);
                double widened = double(value);
                std::from_chars(buffer, written.ptr, widened);
                return num(widened);
            }
            Json& set(const char* key, Json value) { members.emplace_back(key, std::move(value)); return *this; }
            Json& add(Json value) { items.push_back(std::move(value)); return *this; }
            bool scalar() const { return type != Type::Arr && type != Type::Obj; }
        };

        void writeNumber(std::string& out, double value) {
            if (!std::isfinite(value)) {
                out += "null";
                return;
            }
            char buffer[40];
            if (value == std::floor(value) && std::fabs(value) < 1e15) {
                const auto written = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<long long>(value));
                out.append(buffer, written.ptr);
                return;
            }
            const auto written = std::to_chars(buffer, buffer + sizeof(buffer), value);
            out.append(buffer, written.ptr);
        }

        void writeString(std::string& out, const std::string& value) {
            out += '"';
            for (const char c : value) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20) {
                            char escape[8];
                            std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                            out += escape;
                        } else {
                            out += c;
                        }
                }
            }
            out += '"';
        }

        void writeKey(std::string& out, const std::string& key) {
            bool plain = !key.empty() && !std::isdigit(static_cast<unsigned char>(key[0]));
            for (const char c : key) {
                plain = plain && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
            }
            if (plain) {
                out += key;
            } else {
                writeString(out, key);
            }
        }

        // The value on one line. `budget` counts the characters still allowed; false once it is spent.
        bool flat(std::string& out, const Json& value, long& budget) {
            const std::size_t before = out.size();
            switch (value.type) {
                case Json::Type::Null: out += "null"; break;
                case Json::Type::Bool: out += value.flag ? "true" : "false"; break;
                case Json::Type::Num: writeNumber(out, value.number); break;
                case Json::Type::Str: writeString(out, value.text); break;
                case Json::Type::Arr: {
                    out += '[';
                    bool first = true;
                    for (const Json& item : value.items) {
                        out += first ? "" : ", ";
                        first = false;
                        if (!flat(out, item, budget)) {
                            return false;
                        }
                    }
                    out += ']';
                    break;
                }
                case Json::Type::Obj: {
                    out += '{';
                    bool first = true;
                    for (const auto& member : value.members) {
                        out += first ? " " : ", ";
                        first = false;
                        writeKey(out, member.first);
                        out += ": ";
                        if (!flat(out, member.second, budget)) {
                            return false;
                        }
                    }
                    out += value.members.empty() ? "}" : " }";
                    break;
                }
            }
            budget -= long(out.size() - before);
            return budget >= 0;
        }

        bool allScalar(const Json& value) {
            return std::all_of(value.items.begin(), value.items.end(), [](const Json& item) { return item.scalar(); });
        }

        // A container goes on one line when it is short, or is an array of plain values; otherwise one member per line.
        void emit(std::string& out, const Json& value, int depth) {
            if (value.scalar()) {
                long budget = 1 << 30;
                flat(out, value, budget);
                return;
            }
            const bool plainArray = value.type == Json::Type::Arr && allScalar(value);
            if (plainArray || (value.members.empty() && value.items.empty())) {
                long budget = 1 << 30;
                flat(out, value, budget);
                return;
            }
            std::string line;
            long budget = 110 - 2 * depth;
            if (flat(line, value, budget)) {
                out += line;
                return;
            }
            const std::string pad(std::size_t(depth + 1) * 2, ' ');
            if (value.type == Json::Type::Arr) {
                out += "[\n";
                for (std::size_t i = 0; i < value.items.size(); ++i) {
                    out += pad;
                    emit(out, value.items[i], depth + 1);
                    out += i + 1 < value.items.size() ? ",\n" : ",\n";
                }
                out += std::string(std::size_t(depth) * 2, ' ') + "]";
            } else {
                out += "{\n";
                for (const auto& member : value.members) {
                    out += pad;
                    writeKey(out, member.first);
                    out += ": ";
                    emit(out, member.second, depth + 1);
                    out += ",\n";
                }
                out += std::string(std::size_t(depth) * 2, ' ') + "}";
            }
        }

        Json fromValue(const json5::value& value) {
            if (value.is_null()) {
                return Json();
            }
            if (value.is_boolean()) {
                return Json::boolean(value.get_bool());
            }
            if (value.is_number()) {
                return Json::num(value.get<double>());
            }
            if (value.is_string()) {
                return Json::str(value.get_c_str());
            }
            if (value.is_array()) {
                Json result = Json::arr();
                for (const json5::value& item : json5::array_view(value)) {
                    result.add(fromValue(item));
                }
                return result;
            }
            Json result = Json::obj();
            for (auto [key, child] : json5::object_view(value)) {
                result.set(std::string(key).c_str(), fromValue(child));
            }
            return result;
        }

        // ---------------------------------------------------------------- writing the parts

        // An outline as {cmds, xy}: one letter per vertex (M move, L line to, Q quad control, C cubic control, Z close; the point that ends a
        // Q or C run is an L) and the coordinates of every vertex but the Z ones, exactly.
        Json pathJson(const BLPath& path) {
            std::string commands;
            Json coordinates = Json::arr();
            const std::uint8_t* c = path.command_data();
            const BLPoint* v = path.vertex_data();
            for (std::size_t i = 0; i < path.size(); ++i) {
                switch (c[i]) {
                    case BL_PATH_CMD_MOVE: commands += 'M'; break;
                    case BL_PATH_CMD_ON: commands += 'L'; break;
                    case BL_PATH_CMD_QUAD: commands += 'Q'; break;
                    case BL_PATH_CMD_CUBIC: commands += 'C'; break;
                    default: commands += 'Z'; continue;
                }
                coordinates.add(Json::num(v[i].x)).add(Json::num(v[i].y));
            }
            return Json::obj().set("cmds", Json::str(commands)).set("xy", std::move(coordinates));
        }

        Json colorJson(const SketchColor& color) {
            Json out = Json::arr();
            std::size_t count = 3;
            switch (color.space()) {
                case ColorSpace::CMYK: out.add(Json::str("cmyk")); count = 4; break;
                case ColorSpace::Gray: out.add(Json::str("gray")); count = 1; break;
                case ColorSpace::HSB: out.add(Json::str("hsb")); break;
                default: out.add(Json::str("rgb")); break;
            }
            for (std::size_t i = 0; i < count; ++i) {
                out.add(Json::real(color.component(i)));
            }
            out.add(Json::real(color.alpha()));
            return out;
        }

        Json gradientJson(const SketchGradient& g) {
            Json out = Json::obj();
            out.set("angle", Json::num(g.angleDegrees));
            out.set("spread", Json::num(double(unsigned(g.spread))));
            if (g.hasAxis) {
                out.set("axis", Json::arr().add(Json::num(g.axisX1)).add(Json::num(g.axisY1)).add(Json::num(g.axisX2)).add(Json::num(g.axisY2)));
            }
            if (g.alongStroke) {
                out.set("alongStroke", Json::boolean(true));
            }
            out.set("aspect", Json::num(g.axisAspect));
            out.set("skew", Json::num(g.axisSkew));
            Json stops = Json::arr();
            for (const GradientStop& stop : g.stops) {
                stops.add(Json::arr().add(Json::num(stop.offset)).add(colorJson(stop.color)));
            }
            out.set("stops", std::move(stops));
            return out;
        }

        Json paintJson(const SketchPaint& paint) {
            Json out = Json::obj();
            if (paint.kind() == SketchPaint::Kind::Solid) {
                out.set("kind", Json::str("solid")).set("color", colorJson(paint.color()));
            } else if (paint.kind() == SketchPaint::Kind::Linear) {
                out.set("kind", Json::str("linear")).set("gradient", gradientJson(paint.gradient()));
            } else if (paint.kind() == SketchPaint::Kind::Radial) {
                out.set("kind", Json::str("radial")).set("gradient", gradientJson(paint.gradient()));
            } else if (paint.isPoints()) {
                Json points = Json::arr();
                for (const GradientPoint& point : paint.gradient().points) {
                    points.add(Json::arr().add(Json::num(point.x)).add(Json::num(point.y)).add(Json::num(point.radius)).add(colorJson(point.color)));
                }
                out.set("kind", Json::str("points")).set("points", std::move(points));
            } else if (paint.isPattern()) {
                out.set("kind", Json::str("pattern")).set("pattern", Json::num(double(paint.patternId())));
                out.set("scale", Json::num(paint.patternScale())).set("angle", Json::num(paint.patternAngle()));
            } else {
                out.set("kind", Json::str("none"));
            }
            return out;
        }

        Json itemsJson(const std::vector<AppearanceItem>& items) {
            Json out = Json::arr();
            for (const AppearanceItem& item : items) {
                Json row = Json::obj();
                row.set("kind", Json::str(item.kind == AppearanceKind::Fill ? "fill" : (item.kind == AppearanceKind::Stroke ? "stroke" : "effect")));
                if (!item.visible) {
                    row.set("visible", Json::boolean(false));
                }
                row.set("blend", Json::num(double(unsigned(item.blendMode))));
                row.set("opacity", Json::num(item.opacity));
                if (item.kind == AppearanceKind::Effect) {
                    row.set("effect", Json::num(double(unsigned(item.effect))));
                    row.set("color", colorJson(item.color));
                    row.set("softness", Json::num(item.softness));
                    row.set("offset", Json::arr().add(Json::num(item.offsetX)).add(Json::num(item.offsetY)));
                    row.set("radius", Json::num(item.radius));
                } else {
                    row.set("paint", paintJson(item.paint));
                }
                if (item.kind == AppearanceKind::Stroke) {
                    const StrokeOptions& options = item.strokeOptions;
                    row.set("width", Json::num(item.strokeWidth));
                    row.set("cap", Json::num(double(unsigned(options.cap)))).set("join", Json::num(double(unsigned(options.join))));
                    row.set("miterLimit", Json::num(options.miterLimit));
                    if (!options.dashes.empty()) {
                        Json dashes = Json::arr();
                        for (double dash : options.dashes) {
                            dashes.add(Json::num(dash));
                        }
                        row.set("dashes", std::move(dashes));
                    }
                    if (!options.widths.empty()) {
                        Json widths = Json::arr();
                        for (const WidthPoint& w : options.widths) {
                            widths.add(Json::arr().add(Json::num(w.t)).add(Json::num(w.left)).add(Json::num(w.right)));
                        }
                        row.set("widths", std::move(widths));
                    }
                    if (options.brush) {
                        row.set("brush", Json::num(double(options.brush->id)));
                    }
                    if (options.decorated()) {
                        row.set("align", Json::num(double(unsigned(options.align))));
                        row.set("arrowStart", Json::num(double(unsigned(options.arrowStart))));
                        row.set("arrowEnd", Json::num(double(unsigned(options.arrowEnd))));
                        row.set("arrowScale", Json::num(options.arrowScale));
                    }
                }
                out.add(std::move(row));
            }
            return out;
        }

        // ---------------------------------------------------------------- reading the parts

        struct Bad : std::runtime_error {
            using std::runtime_error::runtime_error;
        };

        [[noreturn]] void fail(const std::string& what) { throw Bad(what); }

        bool find(const json5::value& object, const char* key, json5::value& out) {
            for (auto [name, child] : json5::object_view(object)) {
                if (std::string_view(name) == key) {
                    out = child;
                    return true;
                }
            }
            return false;
        }

        json5::value need(const json5::value& object, const char* key, const std::string& where) {
            json5::value out;
            if (!object.is_object() || !find(object, key, out)) {
                fail(where + ": missing \"" + key + "\"");
            }
            return out;
        }

        double number(const json5::value& value, const std::string& where) {
            if (!value.is_number() || !std::isfinite(value.get<double>())) {
                fail(where + " must be a number");
            }
            return value.get<double>();
        }

        double numberOr(const json5::value& object, const char* key, double fallback, const std::string& where) {
            json5::value v;
            return find(object, key, v) ? number(v, where + "." + key) : fallback;
        }

        bool flagOr(const json5::value& object, const char* key, bool fallback) {
            json5::value v;
            return find(object, key, v) && v.is_boolean() ? v.get_bool() : fallback;
        }

        std::string stringOf(const json5::value& value, const std::string& where) {
            if (!value.is_string()) {
                fail(where + " must be a string");
            }
            return value.get_c_str();
        }

        std::string stringOr(const json5::value& object, const char* key, const std::string& fallback, const std::string& where) {
            json5::value v;
            return find(object, key, v) ? stringOf(v, where + "." + key) : fallback;
        }

        unsigned enumeration(const json5::value& value, unsigned count, const std::string& where) {
            const double n = number(value, where);
            if (n < 0.0 || n >= count || n != std::floor(n)) {
                fail(where + " is out of range");
            }
            return unsigned(n);
        }

        unsigned enumOr(const json5::value& object, const char* key, unsigned fallback, unsigned count, const std::string& where) {
            json5::value v;
            return find(object, key, v) ? enumeration(v, count, where + "." + key) : fallback;
        }

        std::size_t index(const json5::value& value, const std::string& where) {
            const double n = number(value, where);
            if (n < 0.0 || n > 4294967295.0 || n != std::floor(n)) {
                fail(where + " must be a whole number");
            }
            return std::size_t(n);
        }

        std::vector<double> numbers(const json5::value& value, std::size_t count, const std::string& where) {
            const json5::array_view list(value);
            if (!list.is_valid() || list.size() != count) {
                fail(where + " must be a list of " + std::to_string(count) + " numbers");
            }
            std::vector<double> out;
            for (const json5::value& item : list) {
                out.push_back(number(item, where));
            }
            return out;
        }

        bool pathOf(const json5::value& value, BLPath& path, const std::string& where) {
            const std::string commands = stringOf(need(value, "cmds", where), where + ".cmds");
            const json5::array_view xy(need(value, "xy", where));
            if (!xy.is_valid()) {
                fail(where + ".xy must be a list");
            }
            std::vector<double> numbers;
            numbers.reserve(xy.size());
            for (const json5::value& item : xy) {
                numbers.push_back(number(item, where + ".xy"));
            }
            std::size_t at = 0;
            const auto point = [&]() {
                if (at + 2 > numbers.size()) {
                    fail(where + ": the list of coordinates is too short");
                }
                const BLPoint p(numbers[at], numbers[at + 1]);
                at += 2;
                return p;
            };
            BLPath result;
            for (std::size_t i = 0; i < commands.size(); ++i) {
                switch (commands[i]) {
                    case 'M': result.move_to(point()); break;
                    case 'L': result.line_to(point()); break;
                    case 'Z': result.close(); break;
                    case 'Q': {
                        const BLPoint control = point();
                        if (i + 1 >= commands.size() || commands[i + 1] != 'L') {
                            fail(where + ": a Q must be followed by the L that ends it");
                        }
                        ++i;
                        result.quad_to(control, point());
                        break;
                    }
                    case 'C': {
                        const BLPoint first = point();
                        if (i + 2 >= commands.size() || commands[i + 1] != 'C' || commands[i + 2] != 'L') {
                            fail(where + ": a C must be followed by a C and the L that ends them");
                        }
                        const BLPoint second = point();
                        i += 2;
                        result.cubic_to(first, second, point());
                        break;
                    }
                    default: fail(where + ": unknown path command");
                }
            }
            if (at != numbers.size()) {
                fail(where + ": the list of coordinates is too long");
            }
            path = std::move(result);
            return true;
        }

        SketchColor colorOf(const json5::value& value, const std::string& where) {
            const json5::array_view list(value);
            if (!list.is_valid() || list.size() < 3) {
                fail(where + " must be a colour: [space, components..., alpha]");
            }
            const std::string space = stringOf(list[0], where);
            std::size_t count = 3;
            if (space == "cmyk") {
                count = 4;
            } else if (space == "gray") {
                count = 1;
            } else if (space != "rgb" && space != "hsb") {
                fail(where + ": unknown colour space \"" + space + "\"");
            }
            if (list.size() != count + 2) {
                fail(where + ": a " + space + " colour has " + std::to_string(count) + " components and an alpha");
            }
            float c[4] = {0, 0, 0, 0};
            for (std::size_t i = 0; i < count; ++i) {
                c[i] = float(number(list[i + 1], where));
            }
            const float alpha = float(number(list[count + 1], where));
            if (space == "cmyk") {
                return SketchColor::cmyk(c[0], c[1], c[2], c[3], alpha);
            }
            if (space == "gray") {
                return SketchColor::gray(c[0], alpha);
            }
            if (space == "hsb") {
                return SketchColor::hsb(c[0], c[1], c[2], alpha);
            }
            return SketchColor::rgb(c[0], c[1], c[2], alpha);
        }

        SketchGradient gradientOf(const json5::value& value, const std::string& where) {
            SketchGradient g;
            g.angleDegrees = numberOr(value, "angle", 0.0, where);
            g.spread = GradientSpread(enumOr(value, "spread", 0, 3, where));
            json5::value axis;
            if (find(value, "axis", axis)) {
                const std::vector<double> a = numbers(axis, 4, where + ".axis");
                g.hasAxis = true;
                g.axisX1 = a[0];
                g.axisY1 = a[1];
                g.axisX2 = a[2];
                g.axisY2 = a[3];
            }
            g.alongStroke = flagOr(value, "alongStroke", false);
            g.axisAspect = numberOr(value, "aspect", g.axisAspect, where);
            g.axisSkew = numberOr(value, "skew", g.axisSkew, where);
            const json5::array_view stops(need(value, "stops", where));
            if (!stops.is_valid()) {
                fail(where + ".stops must be a list");
            }
            for (const json5::value& stop : stops) {
                const json5::array_view pair(stop);
                if (!pair.is_valid() || pair.size() != 2) {
                    fail(where + ": a stop is [offset, colour]");
                }
                GradientStop s;
                s.offset = number(pair[0], where + ".stops");
                s.color = colorOf(pair[1], where + ".stops");
                g.stops.push_back(s);
            }
            return g;
        }

        SketchPaint paintOf(const json5::value& value, const std::string& where) {
            const std::string kind = stringOf(need(value, "kind", where), where + ".kind");
            if (kind == "solid") {
                return SketchPaint::solid(colorOf(need(value, "color", where), where + ".color"));
            }
            if (kind == "linear") {
                return SketchPaint::linear(gradientOf(need(value, "gradient", where), where + ".gradient"));
            }
            if (kind == "radial") {
                return SketchPaint::radial(gradientOf(need(value, "gradient", where), where + ".gradient"));
            }
            if (kind == "points") {
                SketchGradient g;
                const json5::array_view points(need(value, "points", where));
                if (!points.is_valid()) {
                    fail(where + ".points must be a list");
                }
                for (const json5::value& entry : points) {
                    const json5::array_view row(entry);
                    if (!row.is_valid() || row.size() != 4) {
                        fail(where + ": a point is [x, y, radius, colour]");
                    }
                    GradientPoint point;
                    point.x = number(row[0], where);
                    point.y = number(row[1], where);
                    point.radius = number(row[2], where);
                    point.color = colorOf(row[3], where);
                    g.points.push_back(point);
                }
                return SketchPaint::points(g);
            }
            if (kind == "pattern") {
                const double scale = numberOr(value, "scale", 1.0, where), angle = numberOr(value, "angle", 0.0, where);
                if (!(scale > 0.0)) {
                    return SketchPaint::none();
                }
                return SketchPaint::patternRef(index(need(value, "pattern", where), where + ".pattern"), scale, angle);
            }
            if (kind == "none") {
                return SketchPaint::none();
            }
            fail(where + ": unknown paint \"" + kind + "\"");
        }

        std::vector<AppearanceItem> itemsOf(const json5::value& value, const std::string& where) {
            const json5::array_view list(value);
            if (!list.is_valid()) {
                fail(where + " must be a list");
            }
            std::vector<AppearanceItem> items;
            for (const json5::value& row : list) {
                if (!row.is_object()) {
                    fail(where + ": an appearance row must be an object");
                }
                AppearanceItem item;
                const std::string kind = stringOf(need(row, "kind", where), where + ".kind");
                if (kind == "fill") {
                    item.kind = AppearanceKind::Fill;
                } else if (kind == "stroke") {
                    item.kind = AppearanceKind::Stroke;
                } else if (kind == "effect") {
                    item.kind = AppearanceKind::Effect;
                } else {
                    fail(where + ": unknown row kind \"" + kind + "\"");
                }
                item.visible = flagOr(row, "visible", true);
                item.blendMode = BlendMode(enumOr(row, "blend", 0, 12, where));
                item.opacity = numberOr(row, "opacity", 1.0, where);
                json5::value part;
                if (item.kind == AppearanceKind::Effect) {
                    item.paint = SketchPaint::none();
                    item.effect = EffectKind(enumOr(row, "effect", 0, 7, where));
                    if (find(row, "color", part)) {
                        item.color = colorOf(part, where + ".color");
                    }
                    item.softness = numberOr(row, "softness", item.softness, where);
                    if (find(row, "offset", part)) {
                        const std::vector<double> o = numbers(part, 2, where + ".offset");
                        item.offsetX = o[0];
                        item.offsetY = o[1];
                    }
                    item.radius = numberOr(row, "radius", item.radius, where);
                } else {
                    item.paint = paintOf(need(row, "paint", where), where + ".paint");
                }
                if (item.kind == AppearanceKind::Stroke) {
                    StrokeOptions& options = item.strokeOptions;
                    item.strokeWidth = numberOr(row, "width", 1.0, where);
                    options.cap = StrokeCap(enumOr(row, "cap", 0, 3, where));
                    options.join = StrokeJoin(enumOr(row, "join", 0, 3, where));
                    options.miterLimit = numberOr(row, "miterLimit", options.miterLimit, where);
                    if (find(row, "dashes", part)) {
                        const json5::array_view dashes(part);
                        if (!dashes.is_valid()) {
                            fail(where + ".dashes must be a list");
                        }
                        for (const json5::value& dash : dashes) {
                            options.dashes.push_back(number(dash, where + ".dashes"));
                        }
                    }
                    if (find(row, "widths", part)) {
                        const json5::array_view widths(part);
                        if (!widths.is_valid()) {
                            fail(where + ".widths must be a list");
                        }
                        for (const json5::value& w : widths) {
                            const std::vector<double> v = numbers(w, 3, where + ".widths");
                            WidthPoint point;
                            point.t = v[0];
                            point.left = v[1];
                            point.right = v[2];
                            options.widths.push_back(point);
                        }
                    }
                    if (find(row, "brush", part)) {
                        options.brushRef = index(part, where + ".brush");
                    }
                    options.align = StrokeAlign(enumOr(row, "align", 0, 3, where));
                    options.arrowStart = ArrowStyle(enumOr(row, "arrowStart", 0, 4, where));
                    options.arrowEnd = ArrowStyle(enumOr(row, "arrowEnd", 0, 4, where));
                    options.arrowScale = numberOr(row, "arrowScale", options.arrowScale, where);
                }
                items.push_back(item);
            }
            return items;
        }

        // What one object needs from the file, read and checked before the document is touched.
        struct ObjectEntry {
            SketchObjectType type = SketchObjectType::Path;
            std::string name;
            bool visible = true, locked = false;
            std::size_t layer = 0;
            std::vector<std::size_t> groups;
            double frameAngle = 0.0;
            bool hasPlaced = false;
            BLMatrix2D placed = BLMatrix2D::make_identity();
            bool hasWarp = false;
            PaintWarp warp;
            std::vector<AppearanceItem> items;
            double opacity = 1.0;
            BlendMode blend = BlendMode::Normal;
            std::size_t active[3] = {0, 0, 0};
            std::uint32_t node = 0;
            std::string scope;
            bool held = true;
            BLPath outline;
            std::string words, font;
            double fontSize = 24.0;
            BLMatrix2D textMatrix = BLMatrix2D::make_identity();
            TextStyle textStyle;
            TextWarp textWarp;
            std::size_t symbol = 0;
            bool hasImage = false;
            std::size_t image = 0;
        };

        void loadFile(const json5::value& root, SketchDocument& document, int depth);

        std::shared_ptr<SymbolDef> symbolOf(const json5::value& row, int depth, const std::string& where) {
            if (!row.is_object()) {
                fail(where + " must be an object");
            }
            auto def = std::make_shared<SymbolDef>();
            def->id = index(need(row, "id", where), where + ".id");
            def->name = stringOr(row, "name", "", where);
            const std::vector<double> box = numbers(need(row, "box", where), 4, where + ".box");
            if (!(box[2] > box[0]) || !(box[3] > box[1])) {
                fail(where + ".box must have a positive size");
            }
            def->box = BLBox(box[0], box[1], box[2], box[3]);
            def->content = std::make_shared<SketchDocument>();
            loadFile(need(row, "content", where), *def->content, depth + 1);
            return def;
        }

        void symbolsOf(const json5::value& resources, const char* key, int depth, std::vector<std::shared_ptr<const SymbolDef>>& out) {
            json5::value part;
            if (!find(resources, key, part)) {
                return;
            }
            const json5::array_view list(part);
            if (!list.is_valid()) {
                fail(std::string("resources.") + key + " must be a list");
            }
            for (const json5::value& row : list) {
                out.push_back(symbolOf(row, depth, std::string("resources.") + key));
            }
        }

    }

    // ---------------------------------------------------------------- the writer

    bool curvyFileSupports(const SketchDocument&) {
        return true;
    }

    namespace {

    Json fileJson(const SketchDocument& document);

    Json symbolJson(const SymbolDef& def) {
        Json row = Json::obj();
        row.set("id", Json::num(double(def.id))).set("name", Json::str(def.name));
        row.set("box", Json::arr().add(Json::num(def.box.x0)).add(Json::num(def.box.y0)).add(Json::num(def.box.x1)).add(Json::num(def.box.y1)));
        if (def.content) {
            row.set("content", fileJson(*def.content));
        }
        return row;
    }

    Json fileJson(const SketchDocument& document) {
        Json page = Json::obj();
        page.set("width", Json::real(document.pageWidth())).set("height", Json::real(document.pageHeight())).set("ppi", Json::real(document.ppi()));
        page.set("colorMode", Json::str(document.colorMode() == ColorSpace::CMYK ? "cmyk" : "rgb"));
        page.set("background", document.pageBackground() ? colorJson(*document.pageBackground()) : Json());

        Json guides = Json::arr();
        for (const Guide& guide : document.guides()) {
            guides.add(Json::arr().add(Json::str(guide.vertical ? "v" : "h")).add(Json::num(guide.position)));
        }

        Json layers = Json::arr();
        for (const Layer& layer : document.layers()) {
            Json row = Json::obj();
            row.set("id", Json::num(double(layer.id))).set("parent", Json::num(double(layer.parent))).set("name", Json::str(layer.name));
            if (!layer.visible) {
                row.set("visible", Json::boolean(false));
            }
            if (layer.locked) {
                row.set("locked", Json::boolean(true));
            }
            if (!layer.expanded) {
                row.set("expanded", Json::boolean(false));
            }
            if (layer.color != 0) {
                row.set("color", Json::num(double(layer.color)));
            }
            layers.add(std::move(row));
        }

        Json boards = Json::arr();
        for (const Artboard& board : document.artboards()) {
            boards.add(Json::obj().set("name", Json::str(board.name)).set("rect",
                Json::arr().add(Json::num(board.x)).add(Json::num(board.y)).add(Json::num(board.width)).add(Json::num(board.height))));
        }

        Json swatches = Json::arr();
        for (const std::string& swatch : document.swatches()) {
            swatches.add(Json::str(swatch));
        }

        Json styles = Json::arr();
        for (const GraphicStyle& style : document.graphicStyles()) {
            styles.add(Json::obj().set("name", Json::str(style.name)).set("items", itemsJson(style.items))
                .set("opacity", Json::num(style.opacity)).set("blend", Json::num(double(unsigned(style.blendMode)))));
        }

        // Objects, their looks, groups (props live on the group subnets) and outlines are all in the graph; only the pictures they use are saved here.
        std::set<std::string> imageKeys;
        std::vector<std::shared_ptr<const ImageData>> images;
        for (std::size_t i = 0; i < document.objectCount(); ++i) {
            const SketchObject& object = document.object(i);
            if (object.isImage() && object.image() && imageKeys.insert(object.image()->key).second) {
                images.push_back(object.image());
            }
        }

        Json header = Json::obj();
        header.set("page", std::move(page)).set("guides", std::move(guides)).set("layers", std::move(layers)).set("artboards",
            Json::obj().set("active", Json::num(double(document.activeArtboard()))).set("list", std::move(boards)));
        header.set("swatches", std::move(swatches)).set("graphicStyles", std::move(styles));

        json5::document graphDocument;
        json5::builder builder(graphDocument);
        const json5::value graph = jitnodes::saveNetworkValue(builder, document.network(), true, true);

        Json resources = Json::obj();
        for (const auto& library : {std::make_pair("symbols", &document.symbols()), std::make_pair("patterns", &document.patterns())}) {
            if (!library.second->empty()) {
                Json list = Json::arr();
                for (const std::shared_ptr<const SymbolDef>& def : *library.second) {
                    list.add(symbolJson(*def));
                }
                resources.set(library.first, std::move(list));
            }
        }
        if (!document.brushes().empty()) {
            Json list = Json::arr();
            for (const std::shared_ptr<const BrushDef>& def : document.brushes()) {
                Json row = Json::obj();
                row.set("id", Json::num(double(def->id))).set("name", Json::str(def->name)).set("kind", Json::num(double(unsigned(def->kind))));
                Json params = Json::arr();
                for (double value : def->params()) {
                    params.add(Json::num(value));
                }
                row.set("params", std::move(params));
                if (def->hasArt() && def->art && def->art->content) {
                    row.set("art", symbolJson(*def->art));
                }
                list.add(std::move(row));
            }
            resources.set("brushes", std::move(list));
        }
        if (!images.empty()) {
            Json list = Json::arr();
            for (const std::shared_ptr<const ImageData>& image : images) {
                list.add(Json::obj().set("name", Json::str(image->name)).set("key", Json::str(image->key))
                    .set("data", Json::str(base64Encode(image->bytes))));
            }
            resources.set("images", std::move(list));
        }

        Json file = Json::obj();
        file.set("format", Json::str("curvy")).set("version", Json::num(kVersion));
        file.set("document", std::move(header));
        if (!resources.members.empty()) {
            file.set("resources", std::move(resources));
        }
        file.set("graph", fromValue(graph));
        return file;
    }

    }

    std::string curvyFromDocument(const SketchDocument& document) {
        std::string out;
        emit(out, fileJson(document), 0);
        out += "\n";
        return out;
    }

    // ---------------------------------------------------------------- the reader

    bool isCurvyDocument(const std::string& text) {
        std::size_t at = 0;
        while (at < text.size()) {
            if (std::isspace(static_cast<unsigned char>(text[at]))) {
                ++at;
            } else if (text.compare(at, 2, "//") == 0) {
                at = text.find('\n', at);
                if (at == std::string::npos) {
                    return false;
                }
            } else {
                break;
            }
        }
        if (at >= text.size() || text[at] != '{') {
            return false;
        }
        const std::size_t key = text.find("format", at);
        return key != std::string::npos && key < at + 200 && text.find("curvy", key) < key + 40;
    }

    namespace {

        void loadFile(const json5::value& root, SketchDocument& document, int depth) {
            if (depth > 8) {
                fail("resources are nested too deeply");
            }
            SketchDocument::Batch batch(document);   // one sync at the end: after each object alone, the nodes of the ones not yet added look unused
            if (!root.is_object() || stringOr(root, "format", "", "file") != "curvy") {
                fail("not a curvy file (format is not \"curvy\")");
            }
            const double version = number(need(root, "version", "file"), "version");
            if (version != 1 && version != kVersion) {
                fail("unsupported version " + std::to_string(int(version)) + " (this build reads versions 1 and " + std::to_string(kVersion) + ")");
            }
            const bool objectTable = version == 1;
            const json5::value header = need(root, "document", "file");

            // The page.
            const json5::value page = need(header, "page", "document");
            const float pageWidth = float(numberOr(page, "width", 612.0, "page")), pageHeight = float(numberOr(page, "height", 792.0, "page")),
                ppi = float(numberOr(page, "ppi", 72.0, "page"));
            if (!(pageWidth > 0.0f) || !(pageHeight > 0.0f) || !(ppi > 0.0f) || !std::isfinite(pageWidth) || !std::isfinite(pageHeight)) {
                fail("page: the size and ppi must be positive");
            }
            const std::string mode = stringOr(page, "colorMode", "rgb", "page");
            if (mode != "rgb" && mode != "cmyk") {
                fail("page: colorMode must be \"rgb\" or \"cmyk\"");
            }
            std::optional<SketchColor> background;
            json5::value part;
            if (find(page, "background", part) && !part.is_null()) {
                background = colorOf(part, "page.background");
            }

            std::vector<Guide> guides;
            if (find(header, "guides", part)) {
                const json5::array_view list(part);
                if (!list.is_valid()) {
                    fail("guides must be a list");
                }
                for (const json5::value& entry : list) {
                    const json5::array_view pair(entry);
                    if (!pair.is_valid() || pair.size() != 2) {
                        fail("a guide is [\"h\" or \"v\", position]");
                    }
                    Guide guide;
                    guide.vertical = stringOf(pair[0], "guide") == "v";
                    guide.position = number(pair[1], "guide");
                    guides.push_back(guide);
                }
            }

            std::vector<Layer> layers;
            if (find(header, "layers", part)) {
                const json5::array_view list(part);
                if (!list.is_valid()) {
                    fail("layers must be a list");
                }
                for (const json5::value& entry : list) {
                    Layer layer;
                    layer.id = index(need(entry, "id", "layer"), "layer.id");
                    layer.parent = std::size_t(numberOr(entry, "parent", 0.0, "layer"));
                    layer.name = stringOr(entry, "name", "Layer " + std::to_string(layer.id), "layer");
                    layer.visible = flagOr(entry, "visible", true);
                    layer.locked = flagOr(entry, "locked", false);
                    layer.expanded = flagOr(entry, "expanded", true);
                    layer.color = std::uint32_t(numberOr(entry, "color", 0.0, "layer")) & 0xFFFFFFu;
                    if (layer.id != 0) {
                        layers.push_back(layer);
                    }
                }
                for (Layer& layer : layers) {
                    bool known = false;
                    for (const Layer& other : layers) {
                        known = known || (other.id == layer.parent && other.id != layer.id);
                    }
                    if (!known) {
                        layer.parent = 0;
                    }
                }
            }

            std::vector<Artboard> artboards;
            std::size_t artboardActive = 0;
            if (find(header, "artboards", part)) {
                artboardActive = std::size_t(numberOr(part, "active", 0.0, "artboards"));
                const json5::array_view list(need(part, "list", "artboards"));
                if (!list.is_valid()) {
                    fail("artboards.list must be a list");
                }
                for (const json5::value& entry : list) {
                    Artboard board;
                    board.name = stringOr(entry, "name", "", "artboard");
                    const std::vector<double> rect = numbers(need(entry, "rect", "artboard"), 4, "artboard.rect");
                    board.x = rect[0];
                    board.y = rect[1];
                    board.width = rect[2];
                    board.height = rect[3];
                    if (board.width >= 1.0 && board.height >= 1.0) {
                        artboards.push_back(board);
                    }
                }
            }

            std::vector<std::string> swatches = defaultSwatches();
            if (find(header, "swatches", part)) {
                const json5::array_view list(part);
                if (!list.is_valid()) {
                    fail("swatches must be a list");
                }
                swatches.clear();
                for (const json5::value& entry : list) {
                    swatches.push_back(stringOf(entry, "swatch"));
                }
            }

            std::vector<GraphicStyle> graphicStyles;
            if (find(header, "graphicStyles", part)) {
                const json5::array_view list(part);
                if (!list.is_valid()) {
                    fail("graphicStyles must be a list");
                }
                for (const json5::value& entry : list) {
                    GraphicStyle style;
                    style.name = stringOr(entry, "name", "", "graphic style");
                    style.items = itemsOf(need(entry, "items", "graphic style"), "graphic style.items");
                    style.opacity = numberOr(entry, "opacity", 1.0, "graphic style");
                    style.blendMode = BlendMode(enumOr(entry, "blend", 0, 12, "graphic style"));
                    graphicStyles.push_back(std::move(style));
                }
            }

            std::map<std::size_t, GroupProps> groupProps;
            std::set<std::size_t> clipGroups;
            if (find(header, "groups", part)) {
                const json5::array_view list(part);
                if (!list.is_valid()) {
                    fail("groups must be a list");
                }
                for (const json5::value& entry : list) {
                    const std::size_t id = index(need(entry, "id", "group"), "group.id");
                    GroupProps props;
                    props.opacity = numberOr(entry, "opacity", 1.0, "group");
                    props.blendMode = BlendMode(enumOr(entry, "blend", 0, 12, "group"));
                    if (!(props.opacity >= 0.0 && props.opacity <= 1.0)) {
                        fail("group: opacity must be between 0 and 1");
                    }
                    groupProps[id] = props;
                    if (flagOr(entry, "clip", false)) {
                        clipGroups.insert(id);
                    }
                }
            }

            // The libraries are built here, in documents of their own, so a bad one refuses the file before this document is touched.
            std::vector<std::shared_ptr<const SymbolDef>> symbolLibrary, patternLibrary;
            std::vector<std::shared_ptr<const BrushDef>> brushLibrary;
            std::vector<std::shared_ptr<const ImageData>> pictures;   // a picture this machine cannot decode is null
            std::map<std::string, std::shared_ptr<const ImageData>> picturesByKey;
            json5::value resources;
            if (find(root, "resources", resources)) {
                if (!resources.is_object()) {
                    fail("resources must be an object");
                }
                symbolsOf(resources, "symbols", depth, symbolLibrary);
                symbolsOf(resources, "patterns", depth, patternLibrary);
                if (find(resources, "brushes", part)) {
                    const json5::array_view list(part);
                    if (!list.is_valid()) {
                        fail("resources.brushes must be a list");
                    }
                    for (const json5::value& row : list) {
                        if (!row.is_object()) {
                            fail("a brush must be an object");
                        }
                        auto def = std::make_shared<BrushDef>();
                        def->id = index(need(row, "id", "brush"), "brush.id");
                        def->name = stringOr(row, "name", "", "brush");
                        def->kind = BrushKind(enumeration(need(row, "kind", "brush"), 3, "brush.kind"));
                        def->setParams(numbers(need(row, "params", "brush"), 7, "brush.params"));
                        if (def->hasArt()) {
                            const std::shared_ptr<SymbolDef> art = symbolOf(need(row, "art", "brush"), depth, "brush.art");
                            art->id = def->id;
                            art->name = def->name;
                            def->art = art;
                        }
                        if (def->id != 0) {
                            brushLibrary.push_back(def);
                        }
                    }
                }
                if (find(resources, "images", part)) {
                    const json5::array_view list(part);
                    if (!list.is_valid()) {
                        fail("resources.images must be a list");
                    }
                    for (const json5::value& row : list) {
                        std::string bytes;
                        if (!base64Decode(stringOf(need(row, "data", "image"), "image.data"), bytes)) {
                            fail("image.data is not base64");
                        }
                        pictures.push_back(decodeImage(bytes, stringOr(row, "name", "", "image")));
                    }
                    if (!objectTable) {
                        for (const std::shared_ptr<const ImageData>& picture : pictures) {
                            if (picture) {
                                picturesByKey.emplace(picture->key, picture);
                            }
                        }
                    }
                }
            }

            // The graph is checked on a scratch network first, so a bad one refuses the file before the document is touched.
            const json5::value graph = need(root, "graph", "file");
            jitnodes::Network scratch(1);
            try {
                jitnodes::loadNetworkValue(scratch, graph);
            } catch (const std::exception& problem) {
                fail(std::string("graph: ") + problem.what());
            }
            const std::vector<jitnodes::NodeId> nodeList = scratch.nodeIds();
            const std::set<jitnodes::NodeId> nodes(nodeList.begin(), nodeList.end());

            std::vector<ObjectEntry> entries;
            std::vector<json5::value> objectRows;
            if (objectTable) {
                const json5::value objectsValue = need(root, "objects", "file");
                const json5::array_view objectList(objectsValue);
                if (!objectList.is_valid()) {
                    fail("objects must be a list");
                }
                for (const json5::value& row : objectList) {
                    objectRows.push_back(row);
                }
            }
            for (const json5::value& row : objectRows) {
                if (!row.is_object()) {
                    fail("an object must be an object");
                }
                ObjectEntry e;
                e.type = SketchObjectType(enumeration(need(row, "type", "object"), unsigned(SketchObjectType::Image) + 1, "object.type"));
                if (find(row, "text", part)) {
                    e.words = stringOr(part, "words", "", "object.text");
                    e.font = stringOr(part, "font", "", "object.text");
                    e.fontSize = numberOr(part, "size", 24.0, "object.text");
                    if (!(e.fontSize > 0.0)) {
                        fail("object.text: size must be positive");
                    }
                    const std::vector<double> m = numbers(need(part, "matrix", "object.text"), 6, "object.text.matrix");
                    e.textMatrix = BLMatrix2D(m[0], m[1], m[2], m[3], m[4], m[5]);
                    json5::value style;
                    if (find(part, "style", style)) {
                        e.textStyle.bold = flagOr(style, "bold", false);
                        e.textStyle.italic = flagOr(style, "italic", false);
                        e.textStyle.align = int(enumOr(style, "align", 0, 3, "object.text.style"));
                        e.textStyle.tracking = numberOr(style, "tracking", 0.0, "object.text.style");
                        e.textStyle.leading = numberOr(style, "leading", 1.0, "object.text.style");
                        if (!(e.textStyle.leading > 0.0)) {
                            fail("object.text.style: leading must be positive");
                        }
                    }
                    json5::value warp;
                    if (find(part, "warp", warp)) {
                        e.textWarp.kind = TextWarp::Kind(enumeration(need(warp, "kind", "object.text.warp"), 3, "object.text.warp.kind"));
                        e.textWarp.flip = flagOr(warp, "flip", false);
                        e.textWarp.start = numberOr(warp, "start", 0.0, "object.text.warp");
                        pathOf(need(warp, "first", "object.text.warp"), e.textWarp.first, "object.text.warp.first");
                        pathOf(need(warp, "second", "object.text.warp"), e.textWarp.second, "object.text.warp.second");
                    }
                } else if (e.type == SketchObjectType::Text) {
                    fail("a text object has no \"text\"");
                }
                e.symbol = std::size_t(numberOr(row, "symbol", 0.0, "object"));
                if (find(row, "image", part)) {
                    e.hasImage = true;
                    e.image = index(part, "object.image");
                }
                e.name = stringOr(row, "name", "", "object");
                e.visible = flagOr(row, "visible", true);
                e.locked = flagOr(row, "locked", false);
                e.layer = std::size_t(numberOr(row, "layer", 0.0, "object"));
                e.frameAngle = numberOr(row, "frameAngle", 0.0, "object");
                if (find(row, "groups", part)) {
                    const json5::array_view list(part);
                    if (!list.is_valid()) {
                        fail("object.groups must be a list");
                    }
                    for (const json5::value& g : list) {
                        e.groups.push_back(index(g, "object.groups"));
                    }
                }
                if (find(row, "placed", part)) {
                    const std::vector<double> m = numbers(part, 6, "object.placed");
                    e.placed = BLMatrix2D(m[0], m[1], m[2], m[3], m[4], m[5]);
                    e.hasPlaced = true;
                } else {
                    e.placed = BLMatrix2D::make_rotation(e.frameAngle);
                }
                if (find(row, "warp", part)) {
                    const std::vector<double> m = numbers(part, 9, "object.warp");
                    for (std::size_t k = 0; k < 9; ++k) {
                        e.warp.m[k] = m[k];
                    }
                    e.hasWarp = true;
                }
                const json5::value look = need(row, "look", "object");
                e.items = itemsOf(need(look, "items", "object.look"), "object.look.items");
                e.opacity = numberOr(look, "opacity", 1.0, "object.look");
                if (!(e.opacity >= 0.0 && e.opacity <= 1.0)) {
                    fail("object.look: opacity must be between 0 and 1");
                }
                e.blend = BlendMode(enumOr(look, "blend", 0, 12, "object.look"));
                if (find(look, "active", part)) {
                    const std::vector<double> a = numbers(part, 3, "object.look.active");
                    for (std::size_t k = 0; k < 3; ++k) {
                        e.active[k] = std::size_t(std::max(0.0, a[k]));
                    }
                }
                e.node = std::uint32_t(index(need(row, "node", "object"), "object.node"));
                e.scope = stringOr(row, "scope", "", "object");
                e.held = !find(row, "outline", part);
                if (e.held) {
                    if (nodes.count(e.node) == 0 || !scratch.node(e.node).hasGeometryParam("curve") ||
                        scratch.node(e.node).geometryParam("curve")->primitiveCount() == 0) {
                        fail("object " + std::to_string(entries.size()) + ": its shape node " + std::to_string(e.node) + " is not in the graph");
                    }
                } else {
                    pathOf(part, e.outline, "object " + std::to_string(entries.size()) + ".outline");
                }
                entries.push_back(std::move(e));
            }

            // Accepted: install it. Group ids in the file are only labels; each gets a fresh id from the document.
            document.clearObjects();
            jitnodes::loadNetworkValue(document.network(), graph);   // it was loaded on the scratch network above, so this cannot fail
            document.graphReplaced();
            document.setPageSize(pageWidth, pageHeight);
            if (!artboards.empty()) {
                document.setArtboards(artboards, artboardActive);
            }
            document.setPpi(ppi);
            document.setPageBackground(background);
            document.setColorMode(mode == "cmyk" ? ColorSpace::CMYK : ColorSpace::RGB);   // converts the defaults and styles into the mode, so
            document.setGraphicStyles(graphicStyles);   // the saved styles go in after it, exactly as they were written
            document.setGuides(guides);
            document.setSwatches(swatches);
            document.setSymbols(symbolLibrary);
            document.setPatterns(patternLibrary);   // the paints that name a tile are bound to it once the objects are in
            document.setBrushes(brushLibrary);      // likewise the strokes that name a brush
            document.setLayers(layers);   // empty = the one default layer
            std::map<std::size_t, std::size_t> groupIds;
            if (!objectTable) {
                std::vector<std::string> notes;
                document.objectsFromGraph([&](const std::string& key) {
                    const auto found = picturesByKey.find(key);
                    return found != picturesByKey.end() ? found->second : nullptr;
                }, &notes);
            }
            for (ObjectEntry& e : entries) {
                std::unique_ptr<SketchObject> object(SketchObject::make(e.type, e.outline));
                if (e.type == SketchObjectType::Text) {
                    SketchObject::Prototype::setTextState(*object, e.words, e.font, e.fontSize, e.textMatrix);
                    SketchObject::Prototype::setTextStyle(*object, e.textStyle);
                    if (e.textWarp.active()) {
                        SketchObject::Prototype::setTextWarp(*object, e.textWarp);
                    }
                    SketchObject::Prototype::setOutline(*object, e.outline);   // the outline as saved, whatever the fonts here would make of the words
                }
                object->setAppearance(e.items);
                object->setActiveIndices(e.active[0], e.active[1], e.active[2]);
                object->setOpacity(e.opacity);
                object->setBlendMode(e.blend);
                object->setName(e.name);
                object->setVisible(e.visible);
                object->setLocked(e.locked);
                object->setFrameAngle(e.frameAngle);
                SketchObject::Prototype::setPaintMatrix(*object, e.placed);
                SketchObject::Prototype::setPaintWarp(*object, e.warp);
                if (e.held) {
                    BLPath held;
                    const jitnodes::Geometry& curve = *document.network().node(e.node).geometryParam("curve");
                    if (pathFromGeometry(*placeGeometry(curve, object->paintToPage()), held)) {
                        SketchObject::Prototype::setOutline(*object, held);
                    }
                }
                if (e.type == SketchObjectType::Image) {
                    if (e.hasImage && e.image < pictures.size() && pictures[e.image]) {
                        SketchObject::Prototype::setImage(*object, pictures[e.image]);
                    } else {
                        object.reset(SketchObject::make(SketchObjectType::Path, object->geometry()));   // its picture is missing: keep the outline
                    }
                } else if (e.type == SketchObjectType::Symbol) {
                    const std::shared_ptr<const SymbolDef> def = document.findSymbol(e.symbol);
                    if (def) {
                        SketchObject::Prototype::setSymbol(*object, def);
                    } else {
                        object.reset(SketchObject::make(SketchObjectType::Path, object->geometry()));   // its artwork is missing: keep the outline
                    }
                }
                object->setGraphNode(e.node);
                if (!e.scope.empty()) {
                    object->setGraphRef(NodeRef(document.scopeOfText(e.scope), e.node));
                }
                if (e.layer != 0) {
                    object->setLayer(e.layer);
                }
                std::vector<std::size_t> mapped;
                for (std::size_t g : e.groups) {
                    auto found = groupIds.find(g);
                    if (found == groupIds.end()) {
                        const std::size_t fresh = document.newGroupId();
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
        }

    }

    bool documentFromCurvy(const std::string& text, SketchDocument& document, std::string* error) {
        try {
            json5::document parsed;
            if (const json5::error problem = json5::from_string(text, parsed)) {
                fail(std::string("parse error at line ") + std::to_string(problem.line) + ", column " + std::to_string(problem.column) + ": " +
                     json5::error::type_string[problem.type]);
            }
            loadFile(parsed, document, 0);
            return true;
        } catch (const std::exception& problem) {
            if (error != nullptr) {
                *error = problem.what();
            }
            return false;
        }
    }

    std::string writeRecoveryCopy(SketchDocument& document, const std::string& folder, const std::string& stem) {
        namespace fs = std::filesystem;
        std::error_code ignored;
        fs::create_directories(fs::u8path(folder), ignored);
        const fs::path curvy = fs::u8path(folder) / fs::u8path(stem + ".curvy");
        const fs::path svg = fs::u8path(folder) / fs::u8path(stem + ".svg");
        // A copy that cannot be written in the native format still goes out as SVG, so a crash never finds nothing.
        if (document.saveCopyTo(curvy.u8string())) {
            fs::remove(svg, ignored);
            return curvy.u8string();
        }
        fs::remove(curvy, ignored);
        return document.saveCopyTo(svg.u8string()) ? svg.u8string() : std::string();
    }

    std::vector<std::string> recoveryCopies(const std::string& folder) {
        namespace fs = std::filesystem;
        std::error_code ignored;
        std::vector<fs::directory_entry> found;
        for (const fs::directory_entry& entry : fs::directory_iterator(fs::u8path(folder), ignored)) {
            const std::string extension = entry.path().extension().string();
            if (entry.is_regular_file(ignored) && (extension == ".curvy" || extension == ".svg")) {
                found.push_back(entry);
            }
        }
        std::sort(found.begin(), found.end(), [&](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.last_write_time(ignored) > b.last_write_time(ignored);
        });
        std::vector<std::string> paths;
        for (const fs::directory_entry& entry : found) {
            paths.push_back(entry.path().u8string());
        }
        return paths;
    }

}
