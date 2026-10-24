#include "curvy/sketchdocument.h"

#include "curvy/pathnode.h"

#include <algorithm>
#include <memory>
#include <unordered_set>

namespace curvy {

    namespace {
        bool hasNode(const jitnodes::Network& net, jitnodes::NodeId id) {
            const std::vector<jitnodes::NodeId> ids = net.nodeIds();
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        // Brings a shape node up to its object: the outline when the object was edited since the node was written, and the settings that
        // shape it (setString leaves a node alone whose text is already right).
        void writeNode(jitnodes::Node& node, SketchObject& object) {
            if (object.nodeRevision() != object.geometryRevision()) {
                // A node keeps what it holds beyond positions (per-anchor attributes) while the layout stays the same.
                const jitnodes::GeometryHandle& held = node.geometryParam("curve");
                const jitnodes::GeometryHandle fresh = geometryFromPath(object.geometry(), held.get());
                if (!sameCurveShape(*held, *fresh)) {
                    node.setGeometryParam("curve", fresh);
                }
                object.setNodeRevision(object.geometryRevision());
            }
            for (const auto& [name, value] : shapeNodeParams(object)) {
                node.setString(name, value);
            }
        }

        // Files hold their numbers to a few digits, so a loaded object and its loaded node can differ in the last one: they agree when they are
        // that close.
        constexpr double kFileTolerance = 1e-4;

        // A node bound to an object that has not been written to it yet (a loaded file) stays as the file had it when it agrees with the
        // object to within what a file holds; otherwise the object wins and the node follows.
        bool bindLoadedNode(jitnodes::Node& node, SketchObject& object) {
            const jitnodes::Geometry& held = *node.geometryParam("curve");
            if (held.primitiveCount() == 0 || !sameCurveShape(held, *geometryFromPath(object.geometry()), kFileTolerance)) {
                return false;
            }
            object.setNodeRevision(object.geometryRevision());
            return true;
        }

        // The first primitive of the geometry as a closed outline; empty when there is none.
        BLPath outlineOf(const jitnodes::Geometry& geometry) {
            BLPath path;
            if (geometry.primitiveCount() == 0) {
                return path;
            }
            const jitnodes::Primitive& prim = geometry.primitives().front();
            if (prim.count < 2) {
                return path;
            }
            const float* x = geometry.x();
            const float* y = geometry.y();
            path.move_to(x[prim.start], y[prim.start]);
            for (uint32_t i = 1; i < prim.count; ++i) {
                path.line_to(x[prim.start + i], y[prim.start + i]);
            }
            if (prim.closed) {
                path.close();
            }
            return path;
        }
    }

    void SketchDocument::graphChanged() {
        pullPathNodes();
        refreshLiveShapes();
        changed();
    }

    void SketchDocument::refreshLiveShapes() {
        const std::vector<jitnodes::NodeId> ids = network().nodeIds();
        const std::unordered_set<jitnodes::NodeId> existing(ids.begin(), ids.end());
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const jitnodes::NodeId node = object->graphNode();
            if (node == jitnodes::invalidNodeId || existing.count(node) == 0 || isShapeNodeType(network().node(node).typeName())) {
                continue;
            }
            try {
                const jitnodes::GeometryHandle geometry = cook(node);
                BLPath path = outlineOf(*geometry);
                if (path.size() > 0) {
                    object->setGeometry(path);
                }
            } catch (const std::exception&) {
            }
        }
    }

    void SketchDocument::pullPathNodes() {
        jitnodes::Network& net = network();
        const std::vector<jitnodes::NodeId> ids = net.nodeIds();
        const std::unordered_set<jitnodes::NodeId> existing(ids.begin(), ids.end());
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const jitnodes::NodeId node = object->graphNode();
            // An object edited since its last sync (or never synced, as after a load) is newer than its node and wins.
            if (node == jitnodes::invalidNodeId || existing.count(node) == 0 || net.node(node).typeName() != shapeNodeType(*object) ||
                object->nodeRevision() != object->geometryRevision()) {
                continue;
            }
            jitnodes::Node& target = net.node(node);
            if (object->isText()) {
                // The outline of a text is made from its words, so those are what is adopted; which symbol or picture an object shows is
                // not, as that lives in the document and only the outline (the placement) can come back from the node.
                if (applyTextNodeParams(*object, target)) {
                    writeNode(target, *object);
                }
                continue;
            }
            const jitnodes::Geometry& held = *target.geometryParam("curve");
            BLPath path;
            if (held.primitiveCount() > 0 && !sameCurveShape(held, *geometryFromPath(object->geometry()), kFileTolerance / 10) && pathFromGeometry(held, path)) {
                object->setGeometry(path);
                object->setNodeRevision(object->geometryRevision());   // the node already holds it
            }
        }
    }

    void SketchDocument::writePathNodes() {
        jitnodes::Network& net = network();
        const std::vector<jitnodes::NodeId> ids = net.nodeIds();
        const std::unordered_set<jitnodes::NodeId> existing(ids.begin(), ids.end());
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const jitnodes::NodeId node = object->graphNode();
            if (node != jitnodes::invalidNodeId && existing.count(node) != 0 && net.node(node).typeName() == shapeNodeType(*object)) {
                writeNode(net.node(node), *object);
            }
        }
    }

    void SketchDocument::syncPathNodes() {
        jitnodes::Network& net = network();
        const std::vector<jitnodes::NodeId> ids = net.nodeIds();
        const std::unordered_set<jitnodes::NodeId> existing(ids.begin(), ids.end());
        std::unordered_set<jitnodes::NodeId> used;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const char* type = shapeNodeType(*object);
            jitnodes::NodeId node = object->graphNode();
            const bool known = node != jitnodes::invalidNodeId && existing.count(node) != 0;
            if (known && !isShapeNodeType(net.node(node).typeName())) {
                continue;   // a live shape: its outline comes from its own node
            }
            if (!known || used.count(node) != 0 || net.node(node).typeName() != type) {
                // The id an object had before it was deleted is free again when undo brings it back, so references to it survive.
                const bool reuse = node != jitnodes::invalidNodeId && used.count(node) == 0 && existing.count(node) == 0;
                node = reuse ? net.addNodeWithId(node, type, net.uniqueName("path" + std::to_string(node))) : net.addNode(type, net.uniqueName("path"));
                object->setGraphNode(node);
                object->setNodeRevision(0);
            }
            used.insert(node);
            if (!(known && object->nodeRevision() == 0 && bindLoadedNode(net.node(node), *object))) {
                writeNode(net.node(node), *object);
            }
        }
        for (jitnodes::NodeId id : ids) {
            if (used.count(id) == 0 && isShapeNodeType(net.node(id).typeName())) {
                net.removeNode(id);
            }
        }
    }

    SketchObject* SketchDocument::addLiveShape(const std::string& nodeType, const std::map<std::string, float>& params, const std::string& description) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        const jitnodes::NodeId node = network().addNode(nodeType);
        BLPath path;
        try {
            for (const auto& [name, value] : params) {
                network().node(node).setFloat(name, value);
            }
            path = outlineOf(*cook(node));
        } catch (...) {
            restoreGraph(before);
            throw;
        }
        if (path.size() == 0) {
            restoreGraph(before);
            return nullptr;
        }
        auto after = std::make_shared<const jitnodes::GraphSnapshot>(jitnodes::snapshotNetwork(network(), &before));
        auto prior = std::make_shared<const jitnodes::GraphSnapshot>(before);

        struct Slot {
            std::unique_ptr<SketchObject> object;
            std::size_t id = 0;
            std::size_t index = 0;
        };
        auto slot = std::make_shared<Slot>();
        slot->object.reset(SketchObject::make(SketchObjectType::Polygon, path));
        slot->object->setGraphNode(node);
        slot->object->setId(nextId_++);
        slot->id = slot->object->id();
        slot->index = objects_.size();
        SketchObject* raw = slot->object.get();
        auto applied = std::make_shared<bool>(true);   // the graph already holds the node when push() runs doIt
        undoStack_.push({description,
            [this, slot, after, prior, applied]() {
                if (*applied) {
                    *applied = false;
                } else {
                    jitnodes::restoreSnapshot(network(), *after, prior.get());
                }
                attach(std::move(slot->object), slot->index);
                setSelection({slot->id});
                graphChanged();
            },
            [this, slot, after, prior]() {
                slot->object = detach(slot->id, &slot->index);
                jitnodes::restoreSnapshot(network(), *prior, after.get());
                graphChanged();
            }});
        return raw;
    }

    bool SketchDocument::setLiveShapeParam(std::size_t objectId, const std::string& name, float value) {
        const SketchObject* object = findById(objectId);
        if (object == nullptr || object->graphNode() == jitnodes::invalidNodeId) {
            return false;
        }
        setGraphParam(object->graphNode(), name, value);
        return true;
    }

}
