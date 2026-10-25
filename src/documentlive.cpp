#include "curvy/sketchdocument.h"

#include "curvy/appearancenode.h"
#include "curvy/pathnode.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace curvy {

    namespace {
        bool hasNode(const jitnodes::Network& net, jitnodes::NodeId id) {
            const std::vector<jitnodes::NodeId> ids = net.nodeIds();
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        // Brings a shape node up to its object: the outline when the object was edited since the node was written, and the settings that
        // shape it (setString leaves a node alone whose text is already right).
        BLMatrix2D inverseOf(const BLMatrix2D& matrix) {
            BLMatrix2D back = matrix;
            return back.invert() == BL_SUCCESS ? back : BLMatrix2D::make_identity();
        }

        // The shape node holds the outline as it was before the object's transforms; the transform node after it holds those.
        BLPath drawnPath(const SketchObject& object) {
            BLPath path = object.geometry();
            path.transform(inverseOf(object.paintToPage()));
            return path;
        }

        bool sameMatrix(const BLMatrix2D& a, const BLMatrix2D& b, double tolerance) {
            for (int i = 0; i < 6; ++i) {
                if (std::fabs(a.m[i] - b.m[i]) > tolerance * (1.0 + std::fabs(a.m[i]))) {
                    return false;
                }
            }
            return true;
        }

        void writeNode(jitnodes::Node& node, SketchObject& object) {
            if (object.nodeRevision() != object.geometryRevision()) {
                // A node keeps what it holds beyond positions (per-anchor attributes) while the layout stays the same.
                const jitnodes::GeometryHandle& held = node.geometryParam("curve");
                const jitnodes::GeometryHandle fresh = geometryFromPath(drawnPath(object), held.get());
                if (!sameCurveShape(*held, *fresh, 1e-6)) {
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
        bool bindLoadedNode(jitnodes::Node& node, const jitnodes::Node* place, SketchObject& object) {
            const jitnodes::Geometry& held = *node.geometryParam("curve");
            if (place == nullptr || !sameMatrix(transformOf(*place), object.paintToPage(), kFileTolerance) || held.primitiveCount() == 0 ||
                !sameCurveShape(held, *geometryFromPath(drawnPath(object)), kFileTolerance)) {
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
        placedCache_.clear();
        appearanceChains_.clear();
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
        const auto placed = placementNodes(net, ids);
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
            // What the graph says the object is: the drawn curve carried by the transform node.
            const jitnodes::Geometry& drawn = *target.geometryParam("curve");
            const auto place = placed.find(node);
            const BLMatrix2D matrix = place != placed.end() ? transformOf(net.node(place->second)) : object->paintToPage();
            BLPath path;
            BLPath back = object->geometry();
            back.transform(inverseOf(matrix));
            if (drawn.primitiveCount() > 0 && !sameCurveShape(drawn, *geometryFromPath(back), kFileTolerance / 10) &&
                pathFromGeometry(*placeGeometry(drawn, matrix), path)) {
                object->setGeometry(path);
                object->setPaintMatrix(matrix);
                object->setNodeRevision(object->geometryRevision());   // the node already holds it
            }
        }
        pullAppearanceNodes();
    }

    void SketchDocument::pullAppearanceNodes() {
        const jitnodes::Network& net = network();
        const std::vector<jitnodes::NodeId> ids = net.nodeIds();
        const std::unordered_set<jitnodes::NodeId> existing(ids.begin(), ids.end());
        const auto next = appearanceSuccessors(net, ids);
        const AppearanceLibrary library{[this](std::size_t id) { return findPattern(id); }, [this](std::size_t id) { return findBrush(id); }};
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const jitnodes::NodeId shape = object->graphNode();
            if (shape == jitnodes::invalidNodeId || existing.count(shape) == 0) {
                continue;
            }
            const std::vector<jitnodes::NodeId> chain = appearanceChain(next, shape);
            std::vector<AppearanceItem> items = object->appearance();
            if (chain.size() != items.size()) {
                continue;
            }
            bool adopted = false;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const jitnodes::Node& node = net.node(chain[i]);
                const auto held = appearanceSynced_.find(chain[i]);
                if (held == appearanceSynced_.end() || node.typeName() != appearanceNodeType(items[i].kind)) {
                    continue;
                }
                const AppearanceParams have = paramsOf(node), want = paramsOf(items[i]);
                if (have != held->second && want == held->second) {
                    items[i] = itemFrom(have, want, items[i], library);
                    held->second = paramsOf(items[i]);
                    adopted = true;
                }
            }
            if (adopted) {
                const std::size_t fill = object->activeFillIndex(), stroke = object->activeStrokeIndex(), effect = object->activeEffectIndex();
                object->setAppearance(items);
                object->setActiveIndices(fill, stroke, effect);
            }
        }
    }

    void SketchDocument::syncAppearanceNodes(bool scan) {
        jitnodes::Network& net = network();
        std::vector<jitnodes::NodeId> ids;
        std::unordered_set<jitnodes::NodeId> existing;
        std::unordered_map<jitnodes::NodeId, jitnodes::NodeId> next, placed;
        bool loaded = false;
        auto load = [&] {
            if (!loaded) {
                loaded = true;
                ids = net.nodeIds();
                existing.insert(ids.begin(), ids.end());
                next = appearanceSuccessors(net, ids);
                placed = placementNodes(net, ids);
            }
        };
        scan = scan || appearanceObjectCount_ != objects_.size();
        appearanceObjectCount_ = objects_.size();
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const jitnodes::NodeId shape = object->graphNode();
            const std::vector<AppearanceItem>& items = object->appearance();
            const auto cached = appearanceChains_.find(shape);
            if (cached != appearanceChains_.end() && cached->second.items == items) {
                continue;
            }
            load();
            if (shape == jitnodes::invalidNodeId || existing.count(shape) == 0) {
                continue;
            }
            scan = true;
            std::vector<jitnodes::NodeId> chain = appearanceChain(next, shape);
            std::size_t keep = 0;
            while (keep < chain.size() && keep < items.size() && net.node(chain[keep]).typeName() == appearanceNodeType(items[keep].kind)) {
                ++keep;
            }
            while (chain.size() > keep) {
                net.removeNode(chain.back());
                appearanceSynced_.erase(chain.back());
                chain.pop_back();
            }
            const auto place = placed.find(shape);
            while (chain.size() < items.size()) {
                const AppearanceKind kind = items[chain.size()].kind;
                const jitnodes::NodeId node = net.addNode(appearanceNodeType(kind), net.uniqueName(kind == AppearanceKind::Stroke ? "stroke" : kind == AppearanceKind::Effect ? "effect" : "fill"));
                net.connect(!chain.empty() ? chain.back() : place != placed.end() ? place->second : shape, node, 0);
                chain.push_back(node);
            }
            for (std::size_t i = 0; i < items.size(); ++i) {
                jitnodes::Node& node = net.node(chain[i]);
                const AppearanceParams want = paramsOf(items[i]);
                const auto held = appearanceSynced_.find(chain[i]);
                if (held == appearanceSynced_.end() || held->second != want) {
                    if (paramsOf(node) != want) {
                        store(node, want);
                    }
                    appearanceSynced_[chain[i]] = want;
                }
            }
            appearanceChains_[shape] = AppearanceChain{chain, items};
        }
        if (!scan) {
            return;
        }
        std::unordered_set<jitnodes::NodeId> shapes;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            shapes.insert(object->graphNode());
        }
        std::unordered_set<jitnodes::NodeId> used;
        for (auto it = appearanceChains_.begin(); it != appearanceChains_.end();) {
            if (shapes.count(it->first) == 0) {
                it = appearanceChains_.erase(it);
            } else {
                used.insert(it->second.nodes.begin(), it->second.nodes.end());
                ++it;
            }
        }
        for (jitnodes::NodeId id : net.nodeIds()) {
            if (used.count(id) == 0 && isAppearanceNodeType(net.node(id).typeName())) {
                net.removeNode(id);
                appearanceSynced_.erase(id);
            }
        }
    }

    void SketchDocument::writePathNodes() {
        jitnodes::Network& net = network();
        const std::vector<jitnodes::NodeId> ids = net.nodeIds();
        const std::unordered_set<jitnodes::NodeId> existing(ids.begin(), ids.end());
        if (placedCache_.empty()) {
            placedCache_ = placementNodes(net, ids);
        }
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const jitnodes::NodeId node = object->graphNode();
            if (node != jitnodes::invalidNodeId && existing.count(node) != 0 && net.node(node).typeName() == shapeNodeType(*object)) {
                const bool stale = object->nodeRevision() != object->geometryRevision();
                writeNode(net.node(node), *object);
                const auto place = stale ? placedCache_.find(node) : placedCache_.end();
                if (place != placedCache_.end() && existing.count(place->second) != 0 && net.node(place->second).typeName() == kTransformNodeType) {
                    storeTransform(net.node(place->second), object->paintToPage());
                }
            }
        }
    }

    void SketchDocument::syncPathNodes() {
        bool removedShape = false;
        jitnodes::Network& net = network();
        const std::vector<jitnodes::NodeId> ids = net.nodeIds();
        const std::unordered_set<jitnodes::NodeId> existing(ids.begin(), ids.end());
        std::unordered_set<jitnodes::NodeId> used;
        struct Bound {
            SketchObject* object;
            jitnodes::NodeId node;
            bool known;
        };
        std::vector<Bound> bound;
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
            bound.push_back({object.get(), node, known});
        }
        for (jitnodes::NodeId id : ids) {
            if (used.count(id) == 0 && isShapeNodeType(net.node(id).typeName())) {
                net.removeNode(id);
                removedShape = true;
            }
        }

        // Every shape node is followed by a transform node, which takes over the appearance nodes that were wired to the shape (a graph made
        // before there were transform nodes). The wiring is only searched for when a shape has none or one was left without its shape.
        auto cached = [&](jitnodes::NodeId shape) {
            const auto it = placedCache_.find(shape);
            return it != placedCache_.end() && existing.count(it->second) != 0 && net.node(it->second).typeName() == kTransformNodeType &&
                net.inputOf(it->second, 0) == shape;
        };
        bool rewire = placedCache_.empty() || removedShape;
        for (const Bound& entry : bound) {
            rewire = rewire || !cached(entry.node);
        }
        std::unordered_set<jitnodes::NodeId> created;
        if (rewire) {
            const std::vector<jitnodes::NodeId> live = net.nodeIds();
            const auto following = appearanceSuccessors(net, live);
            placedCache_ = placementNodes(net, live);
            for (const Bound& entry : bound) {
                if (placedCache_.count(entry.node) != 0) {
                    continue;
                }
                const jitnodes::NodeId place = net.addNode(kTransformNodeType, net.uniqueName("transform"));
                net.connect(entry.node, place, 0);
                if (const auto first = following.find(entry.node); first != following.end()) {
                    net.connect(place, first->second, 0);
                }
                placedCache_.emplace(entry.node, place);
                created.insert(entry.node);
            }
            for (auto it = placedCache_.begin(); it != placedCache_.end();) {
                if (used.count(it->first) == 0) {
                    net.removeNode(it->second);
                    it = placedCache_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (const Bound& entry : bound) {
            const bool stale = entry.object->nodeRevision() != entry.object->geometryRevision();
            const bool fresh = created.count(entry.node) != 0;
            jitnodes::Node* place = (stale || fresh) ? &net.node(placedCache_.at(entry.node)) : nullptr;
            if (!(entry.known && entry.object->nodeRevision() == 0 && place != nullptr && bindLoadedNode(net.node(entry.node), place, *entry.object))) {
                writeNode(net.node(entry.node), *entry.object);
            }
            if (place != nullptr) {
                storeTransform(*place, entry.object->paintToPage());
            }
        }
        syncAppearanceNodes(removedShape);
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
