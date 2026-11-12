#include "curvy/rendernode.h"

#include <jitnodes/graph/network.h>

#include <memory>
#include <mutex>

namespace curvy {

    RenderNode::RenderNode() : Node(renderNodeType, 1) {}

    jitnodes::GeometryHandle RenderNode::cook(jitnodes::CookContext&, const std::vector<jitnodes::GeometryHandle>& inputs) {
        return inputs.front();
    }

    void registerRenderNode() {
        static std::once_flag once;
        std::call_once(once, [] {
            jitnodes::NodeRegistry::instance().registerType(renderNodeType, [] { return std::make_unique<RenderNode>(); });
        });
    }

}
