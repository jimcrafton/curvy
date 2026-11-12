#include "curvy/appearancenode.h"

#include <jitnodes/graph/network.h>

#include <memory>
#include <mutex>
#include <string>

namespace curvy {

    AppearanceNode::AppearanceNode(const char* type, Kind kind) : Node(type, 1), kind_(kind) {
        declareFloat("visible", 1.0f);
        declareFloat("r", 0.0f);
        declareFloat("g", 0.0f);
        declareFloat("b", 0.0f);
        declareFloat("alpha", 1.0f);
        declareFloat("opacity", 1.0f);
        for (const char* channel : {"r", "g", "b"}) {
            tagParam(channel, "group:color");
            tagParam(channel, "kind:color");
            tagParam(channel, std::string("channel:") + channel);
            tagParam(channel, "unit:0.01");
        }
        tagParam("alpha", "group:color");
        tagParam("alpha", "kind:color");
        tagParam("alpha", "channel:a");
        tagParam("alpha", "unit:0.01");
        tagParam("opacity", "unit:0.01");
        for (const char* unitRange : {"visible", "opacity", "alpha"}) {
            tagParam(unitRange, "min:0");
            tagParam(unitRange, "max:1");
        }
        if (kind_ == Kind::Fill) {
            declareFloat("evenOdd", 0.0f);
            tagParam("evenOdd", "min:0");
            tagParam("evenOdd", "max:1");
        } else {
            declareFloat("width", 1.0f);
            tagParam("width", "min:0");
            declareFloat("cap", 0.0f);
            declareFloat("join", 0.0f);
            declareFloat("miter", 4.0f);
        }
    }

    jitnodes::GeometryHandle AppearanceNode::cook(jitnodes::CookContext&, const std::vector<jitnodes::GeometryHandle>& inputs) {
        const jitnodes::GeometryHandle& input = inputs.front();
        if (getFloat("visible") == 0.0f || input->primitiveCount() == 0) {
            return input;
        }
        auto stamped = std::make_shared<jitnodes::Geometry>(*input);
        stamp(*stamped);
        return stamped;
    }

    void AppearanceNode::stamp(jitnodes::Geometry& geometry) const {
        const std::string prefix = kind_ == Kind::Fill ? "fill." : "stroke.";
        const float alpha = getFloat("alpha") * getFloat("opacity");
        const std::pair<const char*, float> values[] = {{"r", getFloat("r")}, {"g", getFloat("g")}, {"b", getFloat("b")}, {"a", alpha}};
        for (const auto& [name, value] : values) {
            fillColumn(geometry, prefix + name, value);
        }
        const char* extras[] = {"evenOdd", "width", "cap", "join", "miter"};
        for (const char* name : extras) {
            if (!hasFloat(name)) {
                continue;
            }
            fillColumn(geometry, prefix + name, getFloat(name));
        }
    }

    void AppearanceNode::fillColumn(jitnodes::Geometry& geometry, const std::string& name, float value) {
        jitnodes::AlignedArray<float>& column = geometry.attrib(jitnodes::AttribClass::primitive, name).floats();
        for (std::size_t i = 0; i < geometry.primitiveCount(); ++i) {
            column[i] = value;
        }
    }

    void registerAppearanceNodes() {
        static std::once_flag once;
        std::call_once(once, [] {
            auto& registry = jitnodes::NodeRegistry::instance();
            registry.registerType(fillNodeType, [] { return std::make_unique<AppearanceNode>(fillNodeType, AppearanceNode::Kind::Fill); });
            registry.registerType(strokeNodeType, [] { return std::make_unique<AppearanceNode>(strokeNodeType, AppearanceNode::Kind::Stroke); });
        });
    }

}
