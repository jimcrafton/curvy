#include "curvy/shapeproperties.h"

#include "curvy/objectnode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace curvy {

    namespace {

        const float* column(const jitnodes::Geometry& geometry, const char* name) {
            const jitnodes::AttribArray* attrib = geometry.findAttrib(jitnodes::AttribClass::primitive, name);
            return attrib != nullptr ? attrib->floats().data() : nullptr;
        }

        std::string hex(float r, float g, float b) {
            const auto byte = [](float v) { return int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            char text[8];
            std::snprintf(text, sizeof(text), "#%02X%02X%02X", byte(r), byte(g), byte(b));
            return text;
        }

        // "#RRGGBB" of the colour the look nodes stamped on the shape's first primitive, or kNoValue when it is not drawn.
        std::string colourOf(const jitnodes::Geometry& geometry, std::uint32_t prim, const char* prefix) {
            const std::string base(prefix);
            const float* a = column(geometry, (base + ".a").c_str());
            const float* r = column(geometry, (base + ".r").c_str());
            const float* g = column(geometry, (base + ".g").c_str());
            const float* b = column(geometry, (base + ".b").c_str());
            if (a == nullptr || r == nullptr || g == nullptr || b == nullptr || a[prim] <= 0.0f) {
                return kNoValue;
            }
            return hex(r[prim], g[prim], b[prim]);
        }

        bool shownTag(const std::string& tag) {
            return !tag.empty() && tag.find(':') == std::string::npos && tag != layerTag && tag != groupTag;
        }

        void addTags(const jitnodes::Node& node, std::vector<std::string>& into) {
            for (const std::string& tag : node.tags()) {
                if (shownTag(tag) && std::find(into.begin(), into.end(), tag) == into.end()) {
                    into.push_back(tag);
                }
            }
        }

    }

    const char* shapePropertyName(ShapeProperty property) {
        switch (property) {
            case ShapeProperty::Tag: return "tag";
            case ShapeProperty::Fill: return "fill";
            case ShapeProperty::Stroke: return "stroke";
            case ShapeProperty::Width: return "width";
            case ShapeProperty::Type: return "type";
            case ShapeProperty::Layer: return "layer";
        }
        return "";
    }

    std::vector<ShapeValues> describeShapes(GraphDocument& document, const jitnodes::Geometry& geometry) {
        std::vector<ShapeValues> out;
        if (geometry.primitiveCount() == 0) {
            return out;
        }
        const float* strokeA = column(geometry, "stroke.a");
        const float* strokeW = column(geometry, "stroke.width");
        for (const jitnodes::ShapeRange& shape : geometry.shapes()) {
            if (shape.key.empty()) {
                continue;
            }
            ShapeValues values;
            values.key = std::string(shape.key);
            values.fill = colourOf(geometry, shape.first, "fill");
            values.stroke = colourOf(geometry, shape.first, "stroke");
            values.width = kNoValue;
            if (strokeA != nullptr && strokeW != nullptr && strokeA[shape.first] > 0.0f && strokeW[shape.first] > 0.0f) {
                char text[32];
                std::snprintf(text, sizeof(text), "%g px", std::round(double(strokeW[shape.first]) * 2.0) / 2.0);
                values.width = text;
            }
            values.type = kNoValue;
            values.layer = kNoValue;
            const NodeRef node = document.nodeForShapeKey(values.key);
            if (node.valid() && document.hasNode(node)) {
                values.type = document.node(node).typeName();
                bool layerFound = false;
                NodeRef at = node;
                for (;;) {
                    if (!document.hasNode(at)) {
                        break;
                    }
                    const jitnodes::Node& current = document.node(at);
                    addTags(current, values.tags);
                    if (!layerFound && current.hasTag(layerTag)) {
                        values.layer = current.name();
                        layerFound = true;
                    }
                    if (at.scope == 0) {
                        break;
                    }
                    at = document.parentOfScope(at.scope);
                    if (!at.valid()) {
                        break;
                    }
                }
            }
            out.push_back(std::move(values));
        }
        return out;
    }

    std::vector<std::string> valuesOf(const ShapeValues& shape, ShapeProperty property) {
        switch (property) {
            case ShapeProperty::Tag: return shape.tags.empty() ? std::vector<std::string>{kNoValue} : shape.tags;
            case ShapeProperty::Fill: return {shape.fill};
            case ShapeProperty::Stroke: return {shape.stroke};
            case ShapeProperty::Width: return {shape.width};
            case ShapeProperty::Type: return {shape.type};
            case ShapeProperty::Layer: return {shape.layer};
        }
        return {};
    }

    std::vector<ValueCount> countValues(const std::vector<ShapeValues>& shapes, ShapeProperty property) {
        std::map<std::string, std::size_t> counts;
        for (const ShapeValues& shape : shapes) {
            for (const std::string& value : valuesOf(shape, property)) {
                ++counts[value];
            }
        }
        std::vector<ValueCount> out;
        for (const auto& [text, count] : counts) {
            out.push_back({text, count});
        }
        std::stable_sort(out.begin(), out.end(), [](const ValueCount& a, const ValueCount& b) { return a.count > b.count; });
        return out;
    }

    std::vector<std::string> shapesWithValue(const std::vector<ShapeValues>& shapes, ShapeProperty property, const std::string& value) {
        std::vector<std::string> keys;
        for (const ShapeValues& shape : shapes) {
            const std::vector<std::string> values = valuesOf(shape, property);
            if (std::find(values.begin(), values.end(), value) != values.end()) {
                keys.push_back(shape.key);
            }
        }
        return keys;
    }

}
