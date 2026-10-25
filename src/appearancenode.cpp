#include "curvy/appearancenode.h"

#include "curvy/brush.h"

#include <jitnodes/graph/network.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <memory>

namespace curvy {

    namespace {
        using Names = std::initializer_list<const char*>;

        const Names kSolid = {"space", "c0", "c1", "c2", "c3", "alpha"};
        const Names kGradient = {"angle", "spread", "hasAxis", "x1", "y1", "x2", "y2", "aspect", "skew", "alongStroke"};
        const Names kPattern = {"patternId", "patternScale", "patternAngle"};
        const Names kStroke = {"width", "cap", "join", "miter", "align", "arrowStart", "arrowEnd", "arrowScale", "brush"};
        const Names kEffect = {"effect", "softness", "offsetX", "offsetY", "radius", "tintSpace", "tint0", "tint1", "tint2", "tint3", "tintAlpha"};

        struct Table {
            const char* name;
            std::vector<const char*> columns;
        };
        const Table kStops = {"stops", {"offset", "space", "c0", "c1", "c2", "c3", "alpha"}};
        const Table kDashes = {"dashes", {"value"}};
        const Table kWidths = {"widths", {"t", "left", "right"}};

        enum PaintCode { PaintNone, PaintSolid, PaintLinear, PaintRadial, PaintPattern };

        float flag(bool value) { return value ? 1.0f : 0.0f; }

        void putColor(std::map<std::string, float>& n, const SketchColor& color, const char* space, const std::array<const char*, 4>& parts, const char* alpha) {
            n[space] = float(int(color.space()));
            for (std::size_t i = 0; i < 4; ++i) {
                n[parts[i]] = color.component(i);
            }
            n[alpha] = color.alpha();
        }

        SketchColor colorOf(float space, const float c[4], float alpha) {
            switch (ColorSpace(int(space))) {
                case ColorSpace::CMYK: return SketchColor::cmyk(c[0], c[1], c[2], c[3], alpha);
                case ColorSpace::Gray: return SketchColor::gray(c[0], alpha);
                case ColorSpace::HSB: return SketchColor::hsb(c[0], c[1], c[2], alpha);
                default: return SketchColor::rgb(c[0], c[1], c[2], alpha);
            }
        }

        std::vector<float> colorRow(float offset, const SketchColor& color) {
            return {offset, float(int(color.space())), color.component(0), color.component(1), color.component(2), color.component(3), color.alpha()};
        }

        // Every parameter an item can hold, whatever its paint is.
        AppearanceParams full(const AppearanceItem& item) {
            AppearanceParams p;
            auto& n = p.numbers;
            n["visible"] = flag(item.visible);
            n["opacity"] = float(item.opacity);
            n["blend"] = float(int(item.blendMode));
            if (item.kind != AppearanceKind::Effect) {
                const SketchPaint& paint = item.paint;
                n["paint"] = float(paint.kind() == SketchPaint::Kind::None ? PaintNone : paint.kind() == SketchPaint::Kind::Solid ? PaintSolid
                    : paint.kind() == SketchPaint::Kind::Linear ? PaintLinear : paint.kind() == SketchPaint::Kind::Radial ? PaintRadial : PaintPattern);
                putColor(n, paint.color(), "space", {"c0", "c1", "c2", "c3"}, "alpha");
                const SketchGradient& g = paint.gradient();
                n["angle"] = float(g.angleDegrees);
                n["spread"] = float(int(g.spread));
                n["hasAxis"] = flag(g.hasAxis);
                n["x1"] = float(g.axisX1);
                n["y1"] = float(g.axisY1);
                n["x2"] = float(g.axisX2);
                n["y2"] = float(g.axisY2);
                n["aspect"] = float(g.axisAspect);
                n["skew"] = float(g.axisSkew);
                n["alongStroke"] = flag(g.alongStroke);
                std::vector<std::vector<float>>& stops = p.tables[kStops.name];
                for (const GradientStop& stop : g.stops) {
                    stops.push_back(colorRow(float(stop.offset), stop.color));
                }
                n["patternId"] = float(paint.patternId());
                n["patternScale"] = float(paint.patternScale());
                n["patternAngle"] = float(paint.patternAngle());
            }
            if (item.kind == AppearanceKind::Stroke) {
                const StrokeOptions& o = item.strokeOptions;
                n["width"] = float(item.strokeWidth);
                n["cap"] = float(int(o.cap));
                n["join"] = float(int(o.join));
                n["miter"] = float(o.miterLimit);
                n["align"] = float(int(o.align));
                n["arrowStart"] = float(int(o.arrowStart));
                n["arrowEnd"] = float(int(o.arrowEnd));
                n["arrowScale"] = float(o.arrowScale);
                n["brush"] = float(o.brush ? o.brush->id : o.brushRef);
                std::vector<std::vector<float>>& dashes = p.tables[kDashes.name];
                for (double dash : o.dashes) {
                    dashes.push_back({float(dash)});
                }
                std::vector<std::vector<float>>& widths = p.tables[kWidths.name];
                for (const WidthPoint& w : o.widths) {
                    widths.push_back({float(w.t), float(w.left), float(w.right)});
                }
            }
            if (item.kind == AppearanceKind::Effect) {
                n["effect"] = float(int(item.effect));
                n["softness"] = float(item.softness);
                n["offsetX"] = float(item.offsetX);
                n["offsetY"] = float(item.offsetY);
                n["radius"] = float(item.radius);
                putColor(n, item.color, "tintSpace", {"tint0", "tint1", "tint2", "tint3"}, "tintAlpha");
            }
            return p;
        }

        void drop(AppearanceParams& p, const Names& names) {
            for (const char* name : names) {
                p.numbers.erase(name);
            }
        }

        // Leaves what applies to a node of this kind with this paint.
        void prune(AppearanceKind kind, AppearanceParams& p) {
            const auto paint = p.numbers.find("paint");
            const int code = paint == p.numbers.end() ? PaintNone : int(paint->second);
            if (kind == AppearanceKind::Effect) {
                drop(p, kStroke);
            } else {
                drop(p, kEffect);
                if (code != PaintSolid) {
                    drop(p, kSolid);
                }
                if (code != PaintLinear && code != PaintRadial) {
                    drop(p, kGradient);
                    p.tables.erase(kStops.name);
                }
                if (code != PaintPattern) {
                    drop(p, kPattern);
                }
            }
            if (kind != AppearanceKind::Stroke) {
                drop(p, kStroke);
                p.tables.erase(kDashes.name);
                p.tables.erase(kWidths.name);
            }
        }

        const std::array<const Table*, 3> kTables = {&kStops, &kDashes, &kWidths};

        std::vector<std::vector<float>> readTable(const jitnodes::Node& node, const Table& table) {
            std::vector<std::vector<float>> rows;
            if (!node.hasGeometryParam(table.name)) {
                return rows;
            }
            const jitnodes::GeometryHandle held = node.geometryParam(table.name);
            rows.assign(held->pointCount(), std::vector<float>(table.columns.size(), 0.0f));
            for (std::size_t c = 0; c < table.columns.size(); ++c) {
                if (const auto* column = held->findPointAttrib(table.columns[c])) {
                    for (std::size_t r = 0; r < rows.size(); ++r) {
                        rows[r][c] = (*column)[r];
                    }
                }
            }
            return rows;
        }

        jitnodes::GeometryHandle makeTable(const Table& table, const std::vector<std::vector<float>>& rows) {
            auto geometry = std::make_shared<jitnodes::Geometry>();
            geometry->resizePoints(rows.size());
            for (std::size_t c = 0; c < table.columns.size(); ++c) {
                auto& column = geometry->pointAttrib(table.columns[c]);
                for (std::size_t r = 0; r < rows.size(); ++r) {
                    column[r] = rows[r][c];
                }
            }
            return geometry;
        }

        AppearanceKind kindOfType(const std::string& typeName) {
            return typeName == kStrokeNodeType ? AppearanceKind::Stroke : typeName == kEffectNodeType ? AppearanceKind::Effect : AppearanceKind::Fill;
        }

        AppearanceItem defaultItem(AppearanceKind kind) {
            switch (kind) {
                case AppearanceKind::Stroke: return AppearanceItem::makeStroke(SketchPaint::none());
                case AppearanceKind::Effect: return AppearanceItem::makeEffect(EffectKind::DropShadow);
                default: return AppearanceItem::makeFill(SketchPaint::none());
            }
        }

        class AppearanceNode : public jitnodes::Node {
        public:
            AppearanceNode(const char* type, AppearanceKind kind) : Node(type, 1) {
                // A solid paint is the shape of the node, so declare every group; which of them count is decided by "paint".
                AppearanceItem item = defaultItem(kind);
                if (kind != AppearanceKind::Effect) {
                    item.paint = SketchPaint::solid(SketchColor::gray(0.0f));
                }
                for (const auto& [name, value] : full(item).numbers) {
                    declareFloat(name, value);
                }
                if (kind != AppearanceKind::Effect) {
                    declareGeometry(kStops.name);
                }
                if (kind == AppearanceKind::Stroke) {
                    declareGeometry(kDashes.name);
                    declareGeometry(kWidths.name);
                }
            }

            jitnodes::GeometryHandle cook(jitnodes::CookContext&, const std::vector<jitnodes::GeometryHandle>& inputs) override {
                return !inputs.empty() && inputs.front() ? inputs.front() : std::make_shared<jitnodes::Geometry>();
            }
        };
    }

    bool isAppearanceNodeType(const std::string& typeName) {
        return typeName == kFillNodeType || typeName == kStrokeNodeType || typeName == kEffectNodeType;
    }

    const char* appearanceNodeType(AppearanceKind kind) {
        return kind == AppearanceKind::Stroke ? kStrokeNodeType : kind == AppearanceKind::Effect ? kEffectNodeType : kFillNodeType;
    }

    void registerAppearanceNodes() {
        auto& registry = jitnodes::NodeRegistry::instance();
        registry.registerType(kFillNodeType, [] { return std::make_unique<AppearanceNode>(kFillNodeType, AppearanceKind::Fill); });
        registry.registerType(kStrokeNodeType, [] { return std::make_unique<AppearanceNode>(kStrokeNodeType, AppearanceKind::Stroke); });
        registry.registerType(kEffectNodeType, [] { return std::make_unique<AppearanceNode>(kEffectNodeType, AppearanceKind::Effect); });
    }

    std::unordered_map<jitnodes::NodeId, jitnodes::NodeId> appearanceSuccessors(const jitnodes::Network& network, const std::vector<jitnodes::NodeId>& ids) {
        std::unordered_map<jitnodes::NodeId, jitnodes::NodeId> next;
        for (jitnodes::NodeId id : ids) {
            if (isAppearanceNodeType(network.node(id).typeName())) {
                next.emplace(network.inputOf(id, 0), id);
            }
        }
        return next;
    }

    std::vector<jitnodes::NodeId> appearanceChain(const std::unordered_map<jitnodes::NodeId, jitnodes::NodeId>& successors, jitnodes::NodeId shape) {
        std::vector<jitnodes::NodeId> chain;
        for (auto it = successors.find(shape); it != successors.end() && chain.size() < 1000; it = successors.find(chain.back())) {
            chain.push_back(it->second);
        }
        return chain;
    }

    AppearanceParams paramsOf(const AppearanceItem& item) {
        AppearanceParams p = full(item);
        prune(item.kind, p);
        return p;
    }

    AppearanceParams paramsOf(const jitnodes::Node& node) {
        AppearanceParams p;
        for (const std::string& name : node.floatNames()) {
            p.numbers[name] = node.baseFloat(name);
        }
        for (const Table* table : kTables) {
            if (node.hasGeometryParam(table->name)) {
                p.tables[table->name] = readTable(node, *table);
            }
        }
        prune(kindOfType(node.typeName()), p);
        return p;
    }

    void store(jitnodes::Node& node, const AppearanceParams& params) {
        for (const auto& [name, value] : params.numbers) {
            if (node.hasFloat(name) && node.baseFloat(name) != value) {
                node.setFloat(name, value);
            }
        }
        for (const Table* table : kTables) {
            const auto found = params.tables.find(table->name);
            if (found != params.tables.end() && node.hasGeometryParam(table->name) && readTable(node, *table) != found->second) {
                node.setGeometryParam(table->name, makeTable(*table, found->second));
            }
        }
    }

    AppearanceItem itemFrom(const AppearanceParams& params, const AppearanceParams& baseParams, const AppearanceItem& base, const AppearanceLibrary& library) {
        AppearanceItem item = base;
        auto differs = [&](const char* name) {
            const auto now = params.numbers.find(name);
            const auto then = baseParams.numbers.find(name);
            return now != params.numbers.end() && (then == baseParams.numbers.end() || then->second != now->second);
        };
        auto number = [&](const char* name, double current) { return differs(name) ? double(params.numbers.at(name)) : current; };
        auto whole = [&](const char* name, int current) { return differs(name) ? int(std::lround(params.numbers.at(name))) : current; };
        auto tableDiffers = [&](const Table& table) {
            const auto now = params.tables.find(table.name);
            const auto then = baseParams.tables.find(table.name);
            return now != params.tables.end() && (then == baseParams.tables.end() || then->second != now->second);
        };
        auto groupDiffers = [&](const Names& names) {
            return std::any_of(names.begin(), names.end(), [&](const char* name) { return differs(name); });
        };
        auto color = [&](const char* space, const std::array<const char*, 4>& parts, const char* alpha, const SketchColor& current) {
            if (!differs(space) && !differs(alpha) && !std::any_of(parts.begin(), parts.end(), [&](const char* part) { return differs(part); })) {
                return current;
            }
            const float c[4] = {params.numbers.at(parts[0]), params.numbers.at(parts[1]), params.numbers.at(parts[2]), params.numbers.at(parts[3])};
            return colorOf(params.numbers.at(space), c, params.numbers.at(alpha));
        };

        item.visible = differs("visible") ? params.numbers.at("visible") != 0.0f : item.visible;
        item.opacity = number("opacity", item.opacity);
        item.blendMode = BlendMode(whole("blend", int(item.blendMode)));

        if (item.kind != AppearanceKind::Effect && params.numbers.count("paint") != 0) {
            const int code = int(std::lround(params.numbers.at("paint")));
            const SketchPaint& was = base.paint;
            const bool sameKind = differs("paint") ? false : true;
            if (code == PaintSolid) {
                if (!sameKind || groupDiffers(kSolid)) {
                    item.paint = SketchPaint::solid(color("space", {"c0", "c1", "c2", "c3"}, "alpha", was.color()));
                }
            } else if (code == PaintLinear || code == PaintRadial) {
                if (!sameKind || groupDiffers(kGradient) || tableDiffers(kStops)) {
                    SketchGradient g = was.gradient();
                    g.angleDegrees = number("angle", g.angleDegrees);
                    g.spread = GradientSpread(whole("spread", int(g.spread)));
                    g.hasAxis = differs("hasAxis") ? params.numbers.at("hasAxis") != 0.0f : g.hasAxis;
                    g.axisX1 = number("x1", g.axisX1);
                    g.axisY1 = number("y1", g.axisY1);
                    g.axisX2 = number("x2", g.axisX2);
                    g.axisY2 = number("y2", g.axisY2);
                    g.axisAspect = number("aspect", g.axisAspect);
                    g.axisSkew = number("skew", g.axisSkew);
                    g.alongStroke = differs("alongStroke") ? params.numbers.at("alongStroke") != 0.0f : g.alongStroke;
                    if (!sameKind || tableDiffers(kStops)) {
                        g.stops.clear();
                        for (const std::vector<float>& row : params.tables.at(kStops.name)) {
                            const float c[4] = {row[2], row[3], row[4], row[5]};
                            g.stops.push_back({double(row[0]), colorOf(row[1], c, row[6])});
                        }
                    }
                    item.paint = code == PaintLinear ? SketchPaint::linear(g) : SketchPaint::radial(g);
                }
            } else if (code == PaintPattern) {
                if (!sameKind || groupDiffers(kPattern)) {
                    const std::size_t id = std::size_t(std::lround(params.numbers.at("patternId")));
                    const double scale = number("patternScale", was.patternScale()), angle = number("patternAngle", was.patternAngle());
                    std::shared_ptr<const SymbolDef> def = was.isPattern() && was.patternId() == id ? was.patternDef() : library.pattern ? library.pattern(id) : nullptr;
                    item.paint = def ? SketchPaint::pattern(def, scale, angle) : SketchPaint::patternRef(id, scale, angle);
                }
            } else if (!was.isNone()) {
                item.paint = SketchPaint::none();
            }
        }

        if (item.kind == AppearanceKind::Stroke) {
            StrokeOptions& o = item.strokeOptions;
            item.strokeWidth = number("width", item.strokeWidth);
            o.cap = StrokeCap(whole("cap", int(o.cap)));
            o.join = StrokeJoin(whole("join", int(o.join)));
            o.miterLimit = number("miter", o.miterLimit);
            o.align = StrokeAlign(whole("align", int(o.align)));
            o.arrowStart = ArrowStyle(whole("arrowStart", int(o.arrowStart)));
            o.arrowEnd = ArrowStyle(whole("arrowEnd", int(o.arrowEnd)));
            o.arrowScale = number("arrowScale", o.arrowScale);
            if (differs("brush")) {
                const std::size_t id = std::size_t(std::lround(params.numbers.at("brush")));
                o.brush = id != 0 && library.brush ? library.brush(id) : nullptr;
                o.brushRef = o.brush ? 0 : id;
            }
            if (tableDiffers(kDashes)) {
                o.dashes.clear();
                for (const std::vector<float>& row : params.tables.at(kDashes.name)) {
                    o.dashes.push_back(row[0]);
                }
            }
            if (tableDiffers(kWidths)) {
                o.widths.clear();
                for (const std::vector<float>& row : params.tables.at(kWidths.name)) {
                    o.widths.push_back({row[0], row[1], row[2]});
                }
            }
        }

        if (item.kind == AppearanceKind::Effect) {
            item.effect = EffectKind(whole("effect", int(item.effect)));
            item.softness = number("softness", item.softness);
            item.offsetX = number("offsetX", item.offsetX);
            item.offsetY = number("offsetY", item.offsetY);
            item.radius = number("radius", item.radius);
            item.color = color("tintSpace", {"tint0", "tint1", "tint2", "tint3"}, "tintAlpha", item.color);
        }
        return item;
    }

}
