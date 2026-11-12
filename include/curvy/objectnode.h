#pragma once

#include <jitnodes/graph/subnet.h>

#include <newui/color.h>

#include <string>
#include <vector>

namespace curvy {

    inline const char* const objectNodeType = "curvyObject";
    inline const char* const objectXformTag = "object:xform";
    inline const char* const objectMergeTag = "object:merge";
    inline const char* const layerTag = "layer";
    inline const char* const groupTag = "group";
    // On the root network's merge, which holds the layers in z-order and is the root's display node.
    inline const char* const rootMergeTag = "document:merge";

    // A layer or a group: a subnet whose child network is `merge` (the z-order of the contents) wired into `xform` (the object-space
    // transform, the child network's display node). The two are found by tag, never by a remembered id, so a loaded or pasted object works
    // whatever ids its children have.
    // @reflect ignore=true
    class ObjectNode : public jitnodes::SubnetNode {
    public:
        ObjectNode();

        jitnodes::NodeId xformId() const;
        jitnodes::NodeId mergeId() const;

        // Empty when the object is sound; otherwise what is wrong (a missing or doubled xform or merge, merge not feeding xform, xform not shown).
        std::string validate() const;
        // Makes validate() empty: adds what is missing, rewires merge into xform and shows xform. Returns whether anything changed.
        bool repair();

    private:
        jitnodes::NodeId findByTag(const char* tag, int& count) const;
    };

    // Helpers for the z-ordered merges that layers and groups hang off, so the document operations and tests share one way of reading and
    // writing them.
    // @reflect ignore=true
    class ObjectTree {
    public:
        static ObjectNode* objectAt(jitnodes::Network& network, jitnodes::NodeId id);
        // The wired inputs of a merge, bottom first (gaps skipped).
        static std::vector<jitnodes::NodeId> mergeInputs(const jitnodes::Network& network, jitnodes::NodeId merge);
        // Wires `order` into inputs 0.. of the merge, in that order, unwiring everything else.
        static void setMergeInputs(jitnodes::Network& network, jitnodes::NodeId merge, const std::vector<jitnodes::NodeId>& order);
        // The root network's layer merge; invalidNodeId before the first layer.
        static jitnodes::NodeId rootMergeOf(const jitnodes::Network& network);
        // The root network's render node, the display node after the merge; invalidNodeId in a file made before it existed.
        static jitnodes::NodeId rootRenderOf(const jitnodes::Network& network);
        // Makes the root merge and the render node that follows it (and shows it) when missing; returns the merge.
        static jitnodes::NodeId ensureRootMerge(jitnodes::Network& network);
        // Adds an object tagged `role` (layerTag or groupTag) on top of `merge`'s inputs. A blank name gets the next free "Layer1" / "Group1".
        // Adds a circle node feeding a fill node and wires the fill on top of `merge`'s inputs; returns the fill.
        static jitnodes::NodeId addCircle(jitnodes::Network& network, jitnodes::NodeId merge, float cx, float cy, float radius, const newui::Color& fill);
        static jitnodes::NodeId addObject(jitnodes::Network& network, jitnodes::NodeId merge, const char* role, const std::string& name);
    };

    void registerObjectNodes();

}
