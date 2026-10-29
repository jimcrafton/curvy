#pragma once

#include <jitnodes/graph/node.h>

#include <cstddef>
#include <cstdint>
#include <functional>

namespace curvy {

    // Node ids are per network, so a node of the document is named by the scope of its network (GraphDocument interns scope numbers, 0 = root) and its id.
    struct NodeRef {
        std::uint32_t scope = 0;
        jitnodes::NodeId id = jitnodes::invalidNodeId;

        NodeRef() = default;
        explicit NodeRef(jitnodes::NodeId nodeId) : id(nodeId) {}
        NodeRef(std::uint32_t nodeScope, jitnodes::NodeId nodeId) : scope(nodeScope), id(nodeId) {}

        bool valid() const { return id != jitnodes::invalidNodeId; }
        std::uint64_t key() const { return (static_cast<std::uint64_t>(scope) << 32) | id; }
        bool operator==(const NodeRef& other) const { return scope == other.scope && id == other.id; }
        bool operator!=(const NodeRef& other) const { return !(*this == other); }
        bool operator<(const NodeRef& other) const { return key() < other.key(); }
    };

    inline const NodeRef kNoNode{};

}

namespace std {
    template <>
    struct hash<curvy::NodeRef> {
        std::size_t operator()(const curvy::NodeRef& ref) const noexcept { return std::hash<std::uint64_t>()(ref.key()); }
    };
}
