#pragma once

#include <jitnodes/graph/node.h>

#include <vector>

namespace curvy {

    inline const char* const renderNodeType = "curvyRender";
    // On the root network's render node, the document's display node: the end of the graph, after the layers' merge.
    inline const char* const rootRenderTag = "document:render";

    // The end of the graph. It hands its input on as it is (the same geometry, no copy), so the canvas draws vector geometry carrying its
    // fill and stroke attributes. Nothing is rasterised here; effects and an image output would be added at this node.
    // @reflect ignore=true
    class RenderNode : public jitnodes::Node {
    public:
        RenderNode();

        jitnodes::GeometryHandle cook(jitnodes::CookContext& context, const std::vector<jitnodes::GeometryHandle>& inputs) override;
    };

    void registerRenderNode();

}
