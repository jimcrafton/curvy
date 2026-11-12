#pragma once

#include <jitnodes/graph/node.h>

#include <string>
#include <vector>

namespace curvy {

    inline const char* const fillNodeType = "curvyFill";
    inline const char* const strokeNodeType = "curvyStroke";

    // A fill or stroke of the shapes that reach it. It passes the geometry on with the look stamped onto every primitive as attributes, so
    // the cooked result describes itself and the nodes after it (transforms, warps, merges) carry the look along. An invisible one passes its
    // input on untouched. Stamped, per primitive:
    //   fill   - fill.r fill.g fill.b fill.a (alpha already times opacity, sRGB 0..1), fill.evenOdd
    //   stroke - stroke.r stroke.g stroke.b stroke.a, stroke.width, stroke.cap, stroke.join, stroke.miter
    // @reflect ignore=true
    class AppearanceNode : public jitnodes::Node {
    public:
        enum class Kind { Fill, Stroke };

        AppearanceNode(const char* type, Kind kind);

        jitnodes::GeometryHandle cook(jitnodes::CookContext& context, const std::vector<jitnodes::GeometryHandle>& inputs) override;

    private:
        void stamp(jitnodes::Geometry& geometry) const;
        static void fillColumn(jitnodes::Geometry& geometry, const std::string& name, float value);

        Kind kind_;
    };

    void registerAppearanceNodes();

}
