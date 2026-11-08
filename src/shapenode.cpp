#include "curvy/shapenode.h"

#include "curvy/pathnode.h"

#include <jitnodes/graph/network.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace curvy {

    namespace {
        constexpr double kRadians = 0.017453292519943295;

        const char* kindName(LiveKind kind) {
            switch (kind) {
                case LiveKind::Polygon: return "polygon";
                case LiveKind::Star: return "star";
                case LiveKind::RoundRect: return "roundRect";
                case LiveKind::Arc: return "arc";
                default: return "";
            }
        }

        LiveKind kindFrom(const std::string& name) {
            for (LiveKind kind : {LiveKind::Polygon, LiveKind::Star, LiveKind::RoundRect, LiveKind::Arc}) {
                if (name == kindName(kind)) {
                    return kind;
                }
            }
            return LiveKind::None;
        }

        // The outline is made around the origin and moved to the centre, so a rounded rectangle turns about its own middle.
        BLPath outlineOf(const LiveShape& shape, const BLPoint& center) {
            BLPath path = liveShapePath(shape);
            path.translate(center);
            return path;
        }

        class ShapeNode : public jitnodes::Node {
        public:
            ShapeNode() : Node(kShapeNodeType, 0) {
                declareString("label", "");
                declareFloat("visible", 1.0f);
                declareFloat("locked", 0.0f);
                declareFloat("opacity", 1.0f);
                declareString("blend", "normal");
                declareFloat("activeFill", 0.0f);
                declareFloat("activeStroke", 0.0f);
                declareFloat("activeEffect", 0.0f);
                declareFloat("frameAngle", 0.0f);
                declareString("type", "path");
                declareString("kind", kindName(LiveKind::Star));
                declareFloat("centerX", 0.0f);
                declareFloat("centerY", 0.0f);
                declareFloat("points", 5.0f);
                declareFloat("outer", 50.0f);
                declareFloat("innerRatio", 0.5f);
                declareFloat("width", 100.0f);
                declareFloat("height", 100.0f);
                declareFloat("radius", 0.0f);
                declareFloat("angle", 0.0f);
                declareFloat("start", 0.0f);
                declareFloat("sweep", 90.0f);
            }

            jitnodes::GeometryHandle cook(jitnodes::CookContext&, const std::vector<jitnodes::GeometryHandle>&) override {
                const LiveShape shape = shapeNodeLive(*this);
                if (shape.kind == LiveKind::None) {
                    return std::make_shared<jitnodes::Geometry>();
                }
                return geometryFromPath(outlineOf(shape, BLPoint(double(getFloat("centerX")), double(getFloat("centerY")))));
            }
        };
    }

    LiveShape shapeNodeLive(const jitnodes::Node& node) {
        LiveShape shape;
        shape.kind = kindFrom(node.getString("kind"));
        auto value = [&](const char* name) { return double(node.getFloat(name)); };
        switch (shape.kind) {
            case LiveKind::Polygon:
            case LiveKind::Star:
                shape.regular.star = shape.kind == LiveKind::Star;
                shape.regular.center = BLPoint(0.0, 0.0);   // the cook moves the outline to centerX / centerY, so the shape is built around the origin
                shape.regular.points = std::max(3, int(std::lround(value("points"))));
                shape.regular.outer = std::max(0.0, value("outer"));
                shape.regular.inner = shape.regular.outer * std::max(0.0, value("innerRatio"));
                shape.regular.firstAngle = value("angle") * kRadians;
                if (shape.regular.outer <= 0.0) {
                    shape.kind = LiveKind::None;
                }
                break;
            case LiveKind::RoundRect: {
                const double w = std::max(0.0, value("width")), h = std::max(0.0, value("height"));
                shape.rect.box = BLBox(-w / 2.0, -h / 2.0, w / 2.0, h / 2.0);
                shape.rect.radius = std::max(0.0, value("radius"));
                shape.rect.angle = value("angle") * kRadians;
                if (w <= 0.0 || h <= 0.0) {
                    shape.kind = LiveKind::None;
                }
                break;
            }
            case LiveKind::Arc:
                shape.arc.center = BLPoint(0.0, 0.0);
                shape.arc.radius = std::max(0.0, value("outer"));
                shape.arc.start = value("start") * kRadians;
                shape.arc.sweep = value("sweep") * kRadians;
                if (shape.arc.radius <= 0.0) {
                    shape.kind = LiveKind::None;
                }
                break;
            default: break;
        }
        return shape;
    }

    void storeShapeNode(jitnodes::Node& node, const LiveShape& shape) {
        node.setString("kind", kindName(shape.kind));
        for (const auto& [name, value] : shapeNodeValues(shape)) {
            node.setFloat(name, float(value));
        }
    }

    std::map<std::string, double> shapeNodeValues(const LiveShape& shape) {
        std::map<std::string, double> values;
        auto set = [&](const char* name, double value) { values[name] = value; };
        switch (shape.kind) {
            case LiveKind::Polygon:
            case LiveKind::Star:
                set("centerX", shape.regular.center.x);
                set("centerY", shape.regular.center.y);
                set("points", shape.regular.points);
                set("outer", shape.regular.outer);
                if (shape.kind == LiveKind::Star) {
                    set("innerRatio", shape.regular.ratio());
                }
                set("angle", shape.regular.firstAngle / kRadians);
                break;
            case LiveKind::RoundRect: {
                // The recognised box lies in the frame turned by `angle`; the node's rectangle is centred on the origin and turned with it.
                const BLPoint middle = BLMatrix2D::make_rotation(shape.rect.angle).map_point(
                    BLPoint((shape.rect.box.x0 + shape.rect.box.x1) / 2.0, (shape.rect.box.y0 + shape.rect.box.y1) / 2.0));
                set("centerX", middle.x);
                set("centerY", middle.y);
                set("width", shape.rect.box.x1 - shape.rect.box.x0);
                set("height", shape.rect.box.y1 - shape.rect.box.y0);
                set("radius", shape.rect.radius);
                set("angle", shape.rect.angle / kRadians);
                break;
            }
            case LiveKind::Arc:
                set("centerX", shape.arc.center.x);
                set("centerY", shape.arc.center.y);
                set("outer", shape.arc.radius);
                set("start", shape.arc.start / kRadians);
                set("sweep", shape.arc.sweep / kRadians);
                break;
            default: break;
        }
        return values;
    }

    void registerShapeNode() {
        jitnodes::NodeRegistry::instance().registerType(kShapeNodeType, [] { return std::make_unique<ShapeNode>(); });
    }

}
