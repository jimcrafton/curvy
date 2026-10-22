#include "curvy/sketchdocument.h"

#include <algorithm>
#include <memory>

namespace curvy {

    namespace {
        bool hasNode(const jitnodes::Network& net, jitnodes::NodeId id) {
            const std::vector<jitnodes::NodeId> ids = net.nodeIds();
            return std::find(ids.begin(), ids.end(), id) != ids.end();
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
        refreshLiveShapes();
        changed();
    }

    void SketchDocument::refreshLiveShapes() {
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const jitnodes::NodeId node = object->graphNode();
            if (node == jitnodes::invalidNodeId || !hasNode(network(), node)) {
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
