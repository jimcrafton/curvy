#include "curvy/sketchdocument.h"

#include "curvy/appearancenode.h"
#include "curvy/graphbrush.h"
#include "curvy/pathnode.h"
#include "curvy/shapenode.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace curvy {

    namespace {
        // The brush node of a stroke node: an instance of a graph brush asset that takes its input from the stroke node.
        jitnodes::NodeId brushInstanceOf(const jitnodes::Network& net, jitnodes::NodeId stroke) {
            for (jitnodes::NodeId consumer : net.consumersOf(stroke)) {
                if (net.hasNode(consumer) && net.node(consumer).isAssetInstance() && findGraphBrush(net.node(consumer).typeName()) != nullptr) {
                    return consumer;
                }
            }
            return jitnodes::invalidNodeId;
        }

        // paramsOf(node) plus, for a stroke node, the brush.* numbers of its brush node: the settings that differ from the asset's own value.
        AppearanceParams paramsWithBrush(const jitnodes::Network& net, jitnodes::NodeId id) {
            AppearanceParams params = paramsOf(net.node(id));
            if (net.node(id).typeName() != kStrokeNodeType) {
                return params;
            }
            const jitnodes::NodeId instance = brushInstanceOf(net, id);
            const GraphBrushInfo* info = instance != jitnodes::invalidNodeId ? findGraphBrush(net.node(instance).typeName()) : nullptr;
            for (const GraphBrushSetting& setting : info != nullptr ? info->settings : std::vector<GraphBrushSetting>{}) {
                const jitnodes::Node& node = net.node(instance);
                if (node.hasFloat(setting.name) && node.baseFloat(setting.name) != float(setting.defaultValue)) {
                    params.numbers[kBrushSettingPrefix + setting.name] = node.baseFloat(setting.name);
                }
            }
            return params;
        }

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

        // A parametric shape stands in for the path node of a plain object.
        bool nodeTypeFits(const std::string& nodeType, const SketchObject& object) {
            const std::string want = shapeNodeType(object);
            return nodeType == want || (nodeType == kShapeNodeType && want == kPathNodeType);
        }

        // What a shape node draws: a shape cooks its outline, a path node with an input, a bypass or a frozen edit cooks what that gives, and
        // a plain path node holds its curve. Empty when the cook fails.
        jitnodes::GeometryHandle drawnGeometry(jitnodes::Network& net, jitnodes::NodeId id) {
            try {
                const jitnodes::Node& node = net.node(id);
                const bool path = node.typeName() == kPathNodeType;
                if (node.typeName() == kShapeNodeType || (path && (node.frozen() || node.bypassed() || net.inputOf(id, 0) != jitnodes::invalidNodeId))) {
                    return net.cook(id);
                }
                return node.geometryParam("curve");
            } catch (const std::exception&) {
                return std::make_shared<jitnodes::Geometry>();
            }
        }

        // The nodes feeding a path node's input, nearest first.
        std::vector<jitnodes::NodeId> upstreamShapes(const jitnodes::Network& net, jitnodes::NodeId shape) {
            std::vector<jitnodes::NodeId> up;
            for (jitnodes::NodeId at = shape; net.node(at).typeName() == kPathNodeType && up.size() < 1000;) {
                at = net.inputOf(at, 0);
                if (at == jitnodes::invalidNodeId || !isShapeNodeType(net.node(at).typeName())) {
                    break;
                }
                up.push_back(at);
            }
            return up;
        }

        // The shape `placed` draws for `object`, read in the frame before the object's transforms, which is where a curvyShape node holds it.
        bool recogniseBeforePlacement(const SketchObject& object, BLPath placed, LiveShape& out) {
            placed.transform(inverseOf(object.paintToPage()));
            std::unique_ptr<SketchObject> local(SketchObject::make(object.type(), placed));
            local->setFrameAngle(0.0);
            return recogniseLiveShape(*local, out);
        }

        // A parent link is a wire between two transform nodes, so it only counts inside one scope.
        const SketchObject* parentOf(const SketchDocument& document, const SketchObject& object) {
            const SketchObject* parent = object.parent() != 0 ? document.findById(object.parent()) : nullptr;
            return parent != nullptr && parent->graphScope() == object.graphScope() ? parent : nullptr;
        }

        // Where the object sits relative to its parent, or to the groups holding it when it has none: what its transform node holds.
        BLMatrix2D localPlacement(const SketchDocument& document, const SketchObject& object, const SketchObject* parent) {
            BLMatrix2D local = object.paintToPage();
            if (parent != nullptr) {
                local.post_transform(inverseOf(parent->paintToPage()));
            } else if (object.graphScope() != 0) {
                local.post_transform(inverseOf(document.scopeMatrix(object.graphScope())));
            }
            return local;
        }

        bool sameMatrix(const BLMatrix2D& a, const BLMatrix2D& b, double tolerance) {
            for (int i = 0; i < 6; ++i) {
                if (std::fabs(a.m[i] - b.m[i]) > tolerance * (1.0 + std::fabs(a.m[i]))) {
                    return false;
                }
            }
            return true;
        }

        // True when a node that deforms the shape (move points) sits between the shape and its placement node: the object then shows what the
        // chain cooks, and an edit of its outline cannot be written back to the shape.
        // Also true when any placement or deformation along the way (the parents' too) has an expression or keys, so the object follows the time.
        bool animatedNode(const jitnodes::Node& node) {
            for (const char* name : {"translateX", "translateY", "rotation", "scaleX", "scaleY", "shear", "pivotX", "pivotY", "every", "offset"}) {
                if (node.hasFloat(name) && node.isAnimated(name)) {
                    return true;
                }
            }
            return false;
        }

        bool drivenBy(const jitnodes::Network& net, jitnodes::NodeId place, jitnodes::NodeId shape) {
            int guard = 0;
            for (jitnodes::NodeId at = net.inputOf(place, 0); at != jitnodes::invalidNodeId && at != shape && guard < 1000; at = net.inputOf(at, 0), ++guard) {
                if (net.node(at).typeName() == kMovePointsNodeType || net.node(at).typeName() == kDeformAlongPathNodeType || animatedNode(net.node(at))) {
                    return true;
                }
            }
            for (jitnodes::NodeId at = place; at != jitnodes::invalidNodeId && guard < 2000; at = net.inputOf(at, 1), ++guard) {
                if (animatedNode(net.node(at))) {
                    return true;
                }
            }
            return false;
        }

        void writeNode(jitnodes::Node& node, SketchObject& object, bool outline = true) {
            if (outline && !object.boundToNodes()) {
                // A shape node, and a path node that follows an input, own their outline: the object takes it back from them.
                if (node.typeName() == kShapeNodeType) {
                    object.setBoundToNodes(true);
                    return;
                }
                // A node keeps what it holds beyond positions (per-anchor attributes) while the layout stays the same.
                const jitnodes::GeometryHandle held = node.frozen() ? node.frozenGeometry() : node.geometryParam("curve");
                const jitnodes::GeometryHandle fresh = geometryFromPath(drawnPath(object), held.get());
                if (!sameCurveShape(*held, *fresh, 1e-6)) {
                    if (node.frozen()) {
                        node.setFrozenGeometry(fresh);
                    } else {
                        node.setGeometryParam("curve", fresh);
                    }
                }
                object.setBoundToNodes(true);
            }
            for (const auto& [name, value] : shapeNodeParams(object)) {
                node.setString(name, value);
            }
        }

        // Files hold their numbers to a few digits, so a loaded object and its loaded node can differ in the last one: they agree when they are
        // that close.
        constexpr double kFileTolerance = 1e-4;
        constexpr const char* kBaseLayerNodeName = "baseLayer";

        // A node bound to an object that has not been written to it yet (a loaded file) stays as the file had it when it agrees with the
        // object to within what a file holds; otherwise the object wins and the node follows.
        bool bindLoadedNode(const jitnodes::Geometry& held, const BLMatrix2D* world, SketchObject& object) {
            if (world == nullptr || !sameMatrix(*world, object.paintToPage(), kFileTolerance) || held.primitiveCount() == 0 ||
                !sameCurveShape(held, *geometryFromPath(drawnPath(object)), kFileTolerance)) {
                return false;
            }
            object.setBoundToNodes(true);
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

        // What one pass over the objects needs to know of each scope's network, worked out the first time an object of that scope asks.
        class ScopeViews {
        public:
            using View = ScopeTables;   // net is null when the subnet holding the scope is gone

            ScopeViews(GraphDocument& document, std::unordered_map<std::uint32_t, ScopeTables>& kept) : document_(document), kept_(kept) {}

            // The appearance chain wiring, which only some passes need.
            const std::unordered_map<jitnodes::NodeId, jitnodes::NodeId>& nextOf(View& view) {
                if (!view.haveNext && view.net != nullptr) {
                    view.next = appearanceSuccessors(*view.net, view.net->nodeIds());
                    view.haveNext = true;
                }
                return view.next;
            }

            View& of(std::uint32_t scope) {
                View& view = kept_[scope];
                // Checked once per pass, so a pass that rewires a scope keeps the picture it started with, as before.
                if (!checked_.insert(scope).second) {
                    return view;
                }
                jitnodes::Network* net = document_.findNetwork(scope);
                if (net == nullptr) {
                    view = View();
                } else if (view.net != net || view.topology != net->topologyRevision()) {
                    view = View();
                    view.net = net;
                    view.topology = net->topologyRevision();
                    const std::vector<jitnodes::NodeId> ids = net->nodeIds();
                    view.existing.insert(ids.begin(), ids.end());
                    view.placed = placementNodes(*net, ids);
                }
                return view;
            }

        private:
            GraphDocument& document_;
            std::unordered_map<std::uint32_t, View>& kept_;
            std::unordered_set<std::uint32_t> checked_;
        };
    }

    BLMatrix2D SketchDocument::scopeMatrix(std::uint32_t scope) const {
        BLMatrix2D matrix = BLMatrix2D::make_identity();
        for (int guard = 0; scope != 0 && guard < 64; ++guard) {
            const NodeRef subnet = parentOfScope(scope);
            if (!hasNode(subnet)) {
                break;
            }
            matrix.post_transform(resolvedTransformOf(node(subnet)));
            scope = subnet.scope;
        }
        return matrix;
    }

    BLMatrix2D SketchDocument::worldOf(const NodeRef& place) const {
        BLMatrix2D world = worldTransformOf(networkOf(place.scope), place.id);
        if (place.scope != 0) {
            world.post_transform(scopeMatrix(place.scope));
        }
        return world;
    }

    bool SketchDocument::drivenPlace(const NodeRef& place) const {
        // What makes a place driven is in the graph, so it is worked out again only when some network's wiring or a node in it changed.
        // It is asked for once per object, and writing the objects' nodes touches them, so the check is made when the graph changed (graphChanged
        // and the like set drivenDirty_) and only for what was touched since the last time.
        const bool refresh = drivenDirty_;
        drivenDirty_ = false;
        for (std::uint32_t scope = 0; refresh && scope < scopeCount(); ++scope) {
            const jitnodes::Network* net = findNetwork(scope);
            if (net == nullptr) {
                continue;
            }
            DrivenMark& mark = drivenMarks_[scope];
            if (mark.net == net && mark.touched == net->touchRevision()) {
                continue;
            }
            // Only a transform node downstream of something touched can have changed (the test looks at its input chain and its parents).
            std::vector<jitnodes::NodeId> touched;
            std::vector<jitnodes::NodeId> candidates;
            if (mark.net == net && net->touchedSince(mark.touched, touched)) {
                for (jitnodes::NodeId id : touched) {
                    drivenPlaces_.erase(NodeRef(scope, id));   // gone, or about to be looked at again
                }
                for (jitnodes::NodeId id : net->dependentsOf(touched)) {
                    candidates.push_back(id);
                }
            } else {
                for (auto it = drivenPlaces_.begin(); it != drivenPlaces_.end();) {
                    it = it->scope == scope ? drivenPlaces_.erase(it) : std::next(it);
                }
                candidates = net->nodeIds();
            }
            for (jitnodes::NodeId id : candidates) {
                if (net->node(id).typeName() == kTransformNodeType && drivenBy(*net, id, shapeBehind(*net, net->inputOf(id, 0)))) {
                    drivenPlaces_.insert(NodeRef(scope, id));
                } else {
                    drivenPlaces_.erase(NodeRef(scope, id));
                }
            }
            mark.net = net;
            mark.touched = net->touchRevision();
        }
        return drivenPlaces_.count(place) != 0;
    }

    void SketchDocument::timeChanged(bool graphAffected) {
        if (graphAffected) {
            outlineCache_.clear();
        }
        ScopeViews views(*this, scopeTables_);
        bool moved = false;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const ScopeViews::View& view = views.of(object->graphScope());
            const auto driver = view.placed.find(object->graphNode());
            if (view.net != nullptr && driver != view.placed.end() && drivenPlace(NodeRef(object->graphScope(), driver->second))) {
                moved = adoptChain(*object, driver->second) || moved;
            }
        }
        if (moved) {
            changed();
        }
    }

    void SketchDocument::graphChanged() {
        drivenDirty_ = true;
        outlineCache_.clear();
        pullPathNodes();
        refreshLiveShapes();
        changed();
    }

    void SketchDocument::refreshLiveShapes() {
        ScopeViews views(*this, scopeTables_);
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const ScopeViews::View& view = views.of(object->graphScope());
            const jitnodes::NodeId node = object->graphNode();
            if (view.net == nullptr || node == jitnodes::invalidNodeId || view.existing.count(node) == 0 || isShapeNodeType(view.net->node(node).typeName())) {
                continue;
            }
            try {
                const jitnodes::GeometryHandle geometry = cook(object->graphRef());
                BLPath path = outlineOf(*geometry);
                if (path.size() > 0) {
                    object->setGeometry(path);
                }
            } catch (const std::exception&) {
            }
        }
    }

    void SketchDocument::pullPathNodes() {
        ScopeViews views(*this, scopeTables_);
        // Which nodes changed since the last pass, per scope; `all` when that cannot be told (the wiring changed, or too many edits ago).
        struct Changed {
            bool all = true;
            std::unordered_set<jitnodes::NodeId> nodes;
        };
        std::unordered_map<std::uint32_t, Changed> changedIn;
        bool everything = false;
        for (std::uint32_t scope = 0; scope < scopeCount(); ++scope) {
            jitnodes::Network* net = findNetwork(scope);
            const auto seen = pullScopes_.find(scope);
            // Wiring changes are in the log too (the nodes at both ends), so a topology change does not make everything stale.
            if (net == nullptr || seen == pullScopes_.end() || seen->second.net != net) {
                continue;
            }
            std::vector<jitnodes::NodeId> touched;
            if (!net->touchedSince(seen->second.touched, touched)) {
                continue;
            }
            Changed& changed = changedIn[scope];
            changed.all = false;
            changed.nodes = net->dependentsOf(touched);
            for (jitnodes::NodeId id : touched) {
                // A subnet's transform moves everything inside it, which lives in other networks.
                everything = everything || (net->hasNode(id) && net->node(id).childNetwork() != nullptr);
            }
        }
        const auto stampOf = [](const SketchObject& object) {
            PullStamp stamp;
            stamp.geometryRevision = object.geometryRevision();
            stamp.node = object.graphNode();
            stamp.bound = object.boundToNodes();
            const BLMatrix2D& m = object.paintToPage();
            const double values[6] = {m.m00, m.m01, m.m10, m.m11, m.m20, m.m21};
            std::copy(values, values + 6, stamp.matrix);
            return stamp;
        };
        const auto sameStamp = [](const PullStamp& a, const PullStamp& b) {
            return a.geometryRevision == b.geometryRevision && a.node == b.node && a.bound == b.bound && std::equal(a.matrix, a.matrix + 6, b.matrix);
        };
        for (const std::unique_ptr<SketchObject>& objectPtr : objects_) {
            SketchObject* const object = objectPtr.get();
            const std::uint32_t scope = object->graphScope();
            ScopeViews::View& view = views.of(scope);
            if (view.net == nullptr) {
                continue;
            }
            const PullStamp before = stampOf(*object);
            if (!everything) {
                const auto changed = changedIn.find(scope);
                const auto stamp = pullStamps_.find(object->id());
                if (changed != changedIn.end() && !changed->second.all && stamp != pullStamps_.end() && sameStamp(stamp->second, before)) {
                    const auto home = view.placed.find(object->graphNode());
                    const bool touchedNode = changed->second.nodes.count(object->graphNode()) != 0 ||
                        (home != view.placed.end() && (changed->second.nodes.count(home->second) != 0 || drivenPlace(NodeRef(scope, home->second))));
                    if (!touchedNode) {
                        continue;
                    }
                }
            }
            pullObject(*object, scope, view);
            pullStamps_[object->id()] = stampOf(*object);
        }
        for (std::uint32_t scope = 0; scope < scopeCount(); ++scope) {
            if (jitnodes::Network* net = findNetwork(scope)) {
                pullScopes_[scope] = PullScope{net, net->topologyRevision(), net->touchRevision()};
            }
        }
        pullAppearanceNodes();
    }

    void SketchDocument::pullObject(SketchObject& objectRef, std::uint32_t scope, ScopeTables& view) {
        SketchObject* const object = &objectRef;
        {
            jitnodes::Network& net = *view.net;
            const auto& existing = view.existing;
            const auto& placed = view.placed;
            const jitnodes::NodeId node = object->graphNode();
            if (node == jitnodes::invalidNodeId || existing.count(node) == 0 || !nodeTypeFits(net.node(node).typeName(), *object)) {
                return;
            }
            // An object whose chain deforms its shape shows what the chain cooks, whatever was drawn on it since.
            if (const auto driver = placed.find(node); driver != placed.end() && drivenPlace(NodeRef(scope, driver->second))) {
                adoptChain(*object, driver->second);
                return;
            }
            // An object edited since its last sync (or never synced, as after a load) is newer than its node and wins.
            if (!object->boundToNodes()) {
                return;
            }
            if (const auto home = placed.find(node); home != placed.end() && existing.count(home->second) != 0) {
                const jitnodes::Node& placement = net.node(home->second);
                if (placement.typeName() == kTransformNodeType && paintWarpDiffers(placement, object->paintWarp())) {
                    object->setPaintWarp(paintWarpOf(placement));
                }
            }
            jitnodes::Node& target = net.node(node);
            if (object->isText()) {
                // A text follows its placement node like any shape: the move is carried by the object and by the text's own matrix string.
                if (const auto home = placed.find(node); home != placed.end() && existing.count(home->second) != 0) {
                    const BLMatrix2D want = worldOf(NodeRef(scope, home->second));
                    if (!sameMatrix(want, object->paintToPage(), 1e-6)) {
                        BLMatrix2D delta = inverseOf(object->paintToPage());
                        delta.post_transform(want);
                        SketchObject::Prototype::transform(*object, delta);
                        object->setFrameAngle(std::atan2(want.m01, want.m00));
                        for (const auto& [name, value] : shapeNodeParams(*object)) {
                            target.setString(name, value);
                        }
                        object->setBoundToNodes(true);
                    }
                }
                // The outline of a text is made from its words, so those are what is adopted; which symbol or picture an object shows is
                // not, as that lives in the document and only the outline (the placement) can come back from the node.
                if (applyTextNodeParams(*object, target)) {
                    writeNode(target, *object);
                }
                return;
            }
            // What the graph says the object is: the drawn curve carried by the transform node.
            const jitnodes::GeometryHandle drawnHandle = drawnGeometry(net, node);
            const jitnodes::Geometry& drawn = *drawnHandle;
            const auto place = placed.find(node);
            const BLMatrix2D matrix = place != placed.end() ? worldOf(NodeRef(scope, place->second)) : object->paintToPage();
            BLPath path;
            BLPath back = object->geometry();
            back.transform(inverseOf(matrix));
            if (drawn.primitiveCount() > 0 && !sameCurveShape(drawn, *geometryFromPath(back), kFileTolerance / 10) &&
                pathFromGeometry(*placeGeometry(drawn, matrix), path)) {
                object->setGeometry(path);
                object->setPaintMatrix(matrix);
                object->setFrameAngle(std::atan2(matrix.m01, matrix.m00));
                object->setBoundToNodes(true);   // the node already holds it
            }
        }
    }

    void SketchDocument::pullAppearanceNodes() {
        ScopeViews views(*this, scopeTables_);
        const AppearanceLibrary library{[this](std::size_t id) { return findPattern(id); }, [this](std::size_t id) { return findBrush(id); }};
        // The shape nodes whose look chain has a node touched since the last pass; a scope that cannot tell (`all`) has every object looked at.
        struct Dirty {
            bool all = true;
            std::unordered_set<jitnodes::NodeId> shapes;
        };
        std::unordered_map<std::uint32_t, Dirty> dirty;
        for (std::uint32_t scope = 0; scope < scopeCount(); ++scope) {
            jitnodes::Network* net = findNetwork(scope);
            const auto seen = appearanceScopes_.find(scope);
            std::vector<jitnodes::NodeId> touched;
            if (net == nullptr || seen == appearanceScopes_.end() || seen->second.net != net || !net->touchedSince(seen->second.touched, touched)) {
                continue;
            }
            Dirty& d = dirty[scope];
            d.all = false;
            for (jitnodes::NodeId id : touched) {
                for (int guard = 0; id != jitnodes::invalidNodeId && net->hasNode(id) && guard < 1000; ++guard) {
                    // A brush node hangs off its stroke node, so an edit of it is an edit of the stroke's look.
                    const bool brushNode = net->node(id).isAssetInstance() && findGraphBrush(net->node(id).typeName()) != nullptr;
                    const jitnodes::NodeId up = isAppearanceNodeType(net->node(id).typeName()) || brushNode ? net->inputOf(id, 0) : jitnodes::invalidNodeId;
                    if (up == jitnodes::invalidNodeId) {
                        // The chain hangs off the shape's placement node, so the shape is the one behind it.
                        d.shapes.insert(id);
                        if (net->node(id).typeName() == kTransformNodeType) {
                            d.shapes.insert(shapeBehind(*net, net->inputOf(id, 0)));
                        }
                        break;
                    }
                    id = up;
                }
            }
        }
        for (std::uint32_t scope = 0; scope < scopeCount(); ++scope) {
            if (jitnodes::Network* net = findNetwork(scope)) {
                appearanceScopes_[scope] = PullScope{net, net->topologyRevision(), net->touchRevision()};
            }
        }
        // What a graph edit touched is looked at again by the next sync; the chains of the other objects are as they were.
        for (auto it = appearanceChains_.begin(); it != appearanceChains_.end();) {
            const auto d = dirty.find(it->first.scope);
            if (d == dirty.end() || d->second.all || d->second.shapes.count(it->first.id) != 0) {
                it = appearanceChains_.erase(it);
            } else {
                ++it;
            }
        }
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (const auto d = dirty.find(object->graphScope()); d != dirty.end() && !d->second.all && d->second.shapes.count(object->graphNode()) == 0) {
                continue;
            }
            ScopeViews::View& view = views.of(object->graphScope());
            const jitnodes::NodeId shape = object->graphNode();
            if (view.net == nullptr || shape == jitnodes::invalidNodeId || view.existing.count(shape) == 0) {
                continue;
            }
            const jitnodes::Network& net = *view.net;
            const std::vector<jitnodes::NodeId> chain = appearanceChain(views.nextOf(view), shape);
            std::vector<AppearanceItem> items = object->appearance();
            if (chain.size() != items.size()) {
                continue;
            }
            bool adopted = false;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const jitnodes::Node& node = net.node(chain[i]);
                const auto held = appearanceSynced_.find(NodeRef(object->graphScope(), chain[i]));
                if (held == appearanceSynced_.end() || node.typeName() != appearanceNodeType(items[i].kind)) {
                    continue;
                }
                const AppearanceParams have = paramsWithBrush(net, chain[i]), want = paramsOf(items[i]);
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
        ScopeViews views(*this, scopeTables_);
        // Look nodes are orphaned only by a removal or a bulk change (a load, a restore); adding one object never leaves any.
        scan = scan || appearanceObjectCount_ > objects_.size() || objects_.size() > appearanceObjectCount_ + 1;
        appearanceObjectCount_ = objects_.size();
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const std::uint32_t scope = object->graphScope();
            const jitnodes::NodeId shape = object->graphNode();
            const std::vector<AppearanceItem>& items = object->appearance();
            const auto cached = appearanceChains_.find(object->graphRef());
            if (cached != appearanceChains_.end() && cached->second.items == items) {
                continue;
            }
            ScopeViews::View& view = views.of(scope);
            if (view.net == nullptr || shape == jitnodes::invalidNodeId || view.existing.count(shape) == 0) {
                continue;
            }
            jitnodes::Network& net = *view.net;
            const auto& placed = view.placed;
            std::vector<jitnodes::NodeId> chain = appearanceChain(views.nextOf(view), shape);
            std::size_t keep = 0;
            while (keep < chain.size() && keep < items.size() && net.node(chain[keep]).typeName() == appearanceNodeType(items[keep].kind)) {
                ++keep;
            }
            while (chain.size() > keep) {
                if (const jitnodes::NodeId brush = brushInstanceOf(net, chain.back()); brush != jitnodes::invalidNodeId) {
                    net.removeNode(brush);
                }
                net.removeNode(chain.back());
                appearanceSynced_.erase(NodeRef(scope, chain.back()));
                chain.pop_back();
            }
            const auto place = placed.find(shape);
            while (chain.size() < items.size()) {
                const AppearanceKind kind = items[chain.size()].kind;
                const jitnodes::NodeId node = addOwnedNode(net, scope, shape, std::uint32_t(1 + chain.size()), appearanceNodeType(kind),
                    net.uniqueName(kind == AppearanceKind::Stroke ? "stroke" : kind == AppearanceKind::Effect ? "effect" : "fill"));
                net.connect(!chain.empty() ? chain.back() : place != placed.end() ? place->second : shape, node, 0);
                chain.push_back(node);
            }
            for (std::size_t i = 0; i < items.size(); ++i) {
                jitnodes::Node& node = net.node(chain[i]);
                const AppearanceParams want = paramsOf(items[i]);
                const auto held = appearanceSynced_.find(NodeRef(scope, chain[i]));
                if (held == appearanceSynced_.end() || held->second != want) {
                    if (paramsOf(node) != want) {
                        store(node, want);
                    }
                    appearanceSynced_[NodeRef(scope, chain[i])] = want;
                }
                // While a file is being read a brush is only an id, bound to the library afterwards; its node (from the file's graph) stays as it is.
                if (items[i].kind == AppearanceKind::Stroke && !(items[i].strokeOptions.brushRef != 0 && !items[i].strokeOptions.brush)) {
                    // A graph brush is an instance of its asset, wired to the stroke node; its parameters are this stroke's settings.
                    const StrokeOptions& options = items[i].strokeOptions;
                    const GraphBrushInfo* info = options.brush && options.brush->kind == BrushKind::Graph ? findGraphBrush(options.brush->graphType) : nullptr;
                    jitnodes::NodeId instance = brushInstanceOf(net, chain[i]);
                    if (instance != jitnodes::invalidNodeId && (info == nullptr || net.node(instance).typeName() != info->type)) {
                        net.removeNode(instance);
                        instance = jitnodes::invalidNodeId;
                    }
                    if (info != nullptr) {
                        if (instance == jitnodes::invalidNodeId) {
                            instance = addOwnedNode(net, scope, shape, std::uint32_t(0x1000 + i), info->type, net.uniqueName("brush"));
                            net.connect(chain[i], instance, 0);
                        }
                        for (const GraphBrushSetting& setting : info->settings) {
                            double value = setting.defaultValue;
                            for (const auto& [name, changed] : options.graphSettings) {
                                value = name == setting.name ? changed : value;
                            }
                            jitnodes::Node& brushNode = net.node(instance);
                            if (brushNode.hasFloat(setting.name) && brushNode.baseFloat(setting.name) != float(value)) {
                                brushNode.setFloat(setting.name, float(value));
                            }
                        }
                    }
                }
            }
            appearanceChains_[object->graphRef()] = AppearanceChain{chain, items};
        }
        if (!scan) {
            return;
        }
        std::unordered_set<NodeRef> shapes;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            shapes.insert(object->graphRef());
        }
        std::unordered_set<NodeRef> used;
        for (auto it = appearanceChains_.begin(); it != appearanceChains_.end();) {
            if (shapes.count(it->first) == 0) {
                it = appearanceChains_.erase(it);
            } else {
                for (std::uint32_t id : it->second.nodes) {
                    used.insert(NodeRef(it->first.scope, id));
                }
                ++it;
            }
        }
        for (std::uint32_t scope = 0; scope < scopeCount(); ++scope) {
            jitnodes::Network* net = findNetwork(scope);
            if (net == nullptr) {
                continue;
            }
            for (jitnodes::NodeId id : net->nodeIds()) {
                if (used.count(NodeRef(scope, id)) == 0 && isAppearanceNodeType(net->node(id).typeName())) {
                    net->removeNode(id);
                    appearanceSynced_.erase(NodeRef(scope, id));
                }
            }
            // A brush node whose stroke node is gone (or never was a look node of an object) goes with it.
            for (jitnodes::NodeId id : net->nodeIds()) {
                if (net->hasNode(id) && net->node(id).isAssetInstance() && findGraphBrush(net->node(id).typeName()) != nullptr) {
                    const jitnodes::NodeId input = net->inputOf(id, 0);
                    if (input == jitnodes::invalidNodeId || !net->hasNode(input) || net->node(input).typeName() != kStrokeNodeType) {
                        net->removeNode(id);
                    }
                }
            }
        }
    }

    void SketchDocument::placeObjects(const std::vector<GeometryState>& baseline, const BLMatrix2D& matrix, double angleScale, double angleOffset) {
        std::vector<ObjectPlacement> placements;
        placements.reserve(baseline.size());
        for (const GeometryState& entry : baseline) {
            placements.push_back({&entry, matrix, angleScale, angleOffset});
        }
        placeObjects(placements);
    }

    void SketchDocument::placeObjects(const std::vector<ObjectPlacement>& placements) {
        struct Placed {
            SketchObject* object;
            bool synced;
        };
        std::vector<Placed> placed;
        placed.reserve(placements.size());
        for (const ObjectPlacement& step : placements) {
            if (step.from == nullptr) {
                continue;
            }
            const GeometryState& entry = *step.from;
            if (SketchObject* object = findById(entry.id)) {
                // An object whose node is behind it (never synced, or driven) stays that way; the next changed() writes it.
                placed.push_back({object, object->boundToNodes()});
                object->setGeometry(entry.path);
                object->setPaintMatrix(entry.paintMatrix);
                object->setPaintWarp(entry.paintWarp);
                object->setTextMatrix(entry.textMatrix);
                object->transform(step.matrix);
                object->setFrameAngle(step.angleScale * entry.angle + step.angleOffset);
            }
        }
        // Parents and children move together, so every world matrix is final before any local one is worked out.
        for (const Placed& entry : placed) {
            if (entry.synced) {
                writeObjectNodes(*entry.object, false);   // the drawn curve is the same under any placement
            }
        }
    }

    void SketchDocument::warpObjects(const std::vector<GeometryState>& baseline, const std::function<BLPoint(const BLPoint&)>& map, const PaintWarp& page) {
        for (const GeometryState& entry : baseline) {
            if (SketchObject* object = findById(entry.id)) {
                object->setGeometry(entry.path);
                object->setPaintMatrix(entry.paintMatrix);
                object->setPaintWarp(entry.paintWarp);
                object->setTextMatrix(entry.textMatrix);
                object->warp(map, page);
                object->setFrameAngle(entry.angle);
                syncObject(*object);
            }
        }
    }

    void SketchDocument::setObjectOutline(SketchObject& object, const BLPath& placed) {
        object.setGeometry(placed);
        writeObjectNodes(object, true);
    }

    void SketchDocument::syncObject(SketchObject& object) {
        writeObjectNodes(object, true);
    }

    bool SketchDocument::writeObjectNodes(SketchObject& object, bool curve) {
        if (curve && !object.boundToNodes() && isParametric(object.id())) {
            // A hand edit the parameters cannot hold: the shape keeps feeding a frozen path node that takes the edit.
            jitnodes::Network& net = networkOf(object.graphScope());
            const jitnodes::GeometryHandle drawn = drawnGeometry(net, object.graphNode());
            if (!sameCurveShape(*drawn, *geometryFromPath(drawnPath(object)), kFileTolerance)) {
                freezeParametric(object.id());
            }
        }
        const NodeRef placeRef = transformRefOf(object.id());
        if (!placeRef.valid() || drivenPlace(placeRef)) {
            return false;   // not in the graph yet, or shown from what its chain cooks: the next changed() writes it
        }
        const jitnodes::NodeId place = placeRef.id;
        jitnodes::Network& net = networkOf(placeRef.scope);
        jitnodes::Node& shape = net.node(object.graphNode());
        if (shape.typeName() != shapeNodeType(object)) {
            return false;
        }
        if (curve) {
            writeNode(shape, object);
        } else {
            for (const auto& [name, value] : shapeNodeParams(object)) {
                shape.setString(name, value);
            }
        }
        storeTransform(net.node(place), localPlacement(*this, object, parentOf(*this, object)));
        storePaintWarp(net.node(place), object.paintWarp());
        object.setBoundToNodes(true);
        outlineCache_[object.id()] = PlacedOutline{object.geometryRevision(), object.geometry()};   // what the node would cook
        return true;
    }

    std::string SketchDocument::scopeText(std::uint32_t scope) const {
        std::string text;
        for (jitnodes::NodeId id : scopePath(scope)) {
            text += (text.empty() ? "" : "/") + std::to_string(id);
        }
        return text;
    }

    std::uint32_t SketchDocument::scopeOfText(const std::string& text) {
        std::vector<jitnodes::NodeId> path;
        for (const char* at = text.c_str(); *at != '\0';) {
            char* end = nullptr;
            const unsigned long id = std::strtoul(at, &end, 10);
            if (end == at) {
                break;
            }
            path.push_back(jitnodes::NodeId(id));
            at = *end == '/' ? end + 1 : end;
        }
        return scopeOf(path);
    }

    std::size_t SketchDocument::groupIdOfScope(std::uint32_t scope) const {
        if (scope == 0 || scope >= scopeCount()) {
            return 0;
        }
        const NodeRef subnet = parentOfScope(scope);
        if (!hasNode(subnet) || node(subnet).typeName() != kSubnetNodeType) {
            return 0;
        }
        return std::size_t(node(subnet).getFloat("groupId"));
    }

    std::size_t SketchDocument::layerIdOfScope(std::uint32_t scope) const {
        if (scope == 0 || scope >= scopeCount()) {
            return 0;
        }
        const NodeRef subnet = parentOfScope(scope);
        if (!hasNode(subnet) || node(subnet).typeName() != kSubnetNodeType) {
            return 0;
        }
        return std::size_t(node(subnet).getFloat("layerId"));
    }

    std::size_t SketchDocument::baseLayer() const {
        const std::vector<std::size_t> top = layerChildren(0);
        return top.empty() ? 0 : top.front();
    }

    // Outermost first, without the base layer (it is the root); a sublayer of it starts the path.
    std::vector<std::size_t> SketchDocument::layerPathOf(std::size_t layer) const {
        std::vector<std::size_t> path;
        const std::size_t base = baseLayer();
        for (std::size_t at = layer; at != 0 && at != base && path.size() < layers_.size(); ) {
            const Layer* found = findLayer(at);
            if (found == nullptr) {
                return {};
            }
            path.push_back(at);
            at = found->parent;
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    std::size_t SketchDocument::layerOfScope(std::uint32_t scope) const {
        for (; scope != 0 && scope < scopeCount(); scope = parentOfScope(scope).scope) {
            if (const std::size_t layer = layerIdOfScope(scope)) {
                return layer;
            }
        }
        return baseLayer();
    }

    std::size_t SketchDocument::layerOf(const SketchObject& object) const {
        if (!layersEdited_ && object.layer() == object.placedLayer() && object.graphNode() != jitnodes::invalidNodeId && findNetwork(object.graphScope()) != nullptr) {
            const std::size_t derived = layerOfScope(object.graphScope());
            if (findLayer(derived) != nullptr) {
                return derived;
            }
        }
        return object.layer();
    }

    void SketchDocument::setLayerOf(SketchObject& object, std::size_t layer) {
        object.setLayer(layer);
        if (layer == object.placedLayer()) {
            object.setPlacedLayer(std::size_t(-1));   // an explicit request wins over where the nodes happen to sit
        }
    }

    void SketchDocument::syncGroupScopes() {
        constexpr std::size_t layerKey = std::size_t(1) << (sizeof(std::size_t) * 8 - 1);
        const std::size_t base = baseLayer();
        const bool baseMoved = base != placedBaseLayer_;
        placedBaseLayer_ = base;
        bool work = false;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (isGrouped(*object) || object->graphScope() != 0 || object->layer() != base) {
                work = true;
                break;
            }
        }
        // A layer keeps its subnet while it is empty, so the graph lists every layer; one under the wrong parent counts as missing.
        std::vector<std::size_t> missing;
        {
            std::unordered_set<std::size_t> housed;
            for (std::uint32_t scope = 1; scope < scopeCount(); ++scope) {
                const std::size_t id = layerIdOfScope(scope);
                const std::vector<std::size_t> path = id != 0 ? layerPathOf(id) : std::vector<std::size_t>();
                if (!path.empty() && layerIdOfScope(parentOfScope(scope).scope) == (path.size() > 1 ? path[path.size() - 2] : 0)) {
                    housed.insert(id);
                }
            }
            for (const Layer& layer : layers_) {
                if (layer.id != base && housed.count(layer.id) == 0) {
                    missing.push_back(layer.id);
                }
            }
        }
        if (!work && missing.empty()) {
            return;
        }
        std::map<std::vector<std::size_t>, std::uint32_t> known;
        auto scopeFor = [&](const std::vector<std::size_t>& chain) {
            std::uint32_t scope = 0;
            std::vector<std::size_t> prefix;
            for (std::size_t group : chain) {
                prefix.push_back(group);
                const auto hit = known.find(prefix);
                if (hit != known.end()) {
                    scope = hit->second;
                    continue;
                }
                jitnodes::Network& net = networkOf(scope);
                jitnodes::NodeId found = jitnodes::invalidNodeId;
                const bool isLayer = (group & layerKey) != 0;
                const char* param = isLayer ? "layerId" : "groupId";
                const std::size_t id = group & ~layerKey;
                for (jitnodes::NodeId candidate : net.nodeIds()) {
                    if (net.node(candidate).typeName() == kSubnetNodeType && std::size_t(net.node(candidate).getFloat(param)) == id) {
                        found = candidate;
                        break;
                    }
                }
                if (found == jitnodes::invalidNodeId) {
                    found = addOwnedNode(net, scope, (std::uint64_t(1) << 62) | std::uint64_t(group), 0, kSubnetNodeType, net.uniqueName(isLayer ? "layer" : "group"));
                    net.node(found).setFloat(param, float(id));
                    if (isLayer) {
                        net.node(found).setString("role", "layer");
                    } else {
                        groupSubnets_[id] = NodeRef(scope, found);
                        if (const auto aside = groupProps_.find(id); aside != groupProps_.end()) {
                            const GroupProps props = aside->second;
                            groupProps_.erase(aside);
                            putGroupProps(id, props);
                        }
                    }
                }
                scope = scopeOfSubnet(NodeRef(scope, found));
                known.emplace(prefix, scope);
            }
            return scope;
        };

        struct Move {
            SketchObject* object;
            std::uint32_t to;
        };
        std::map<std::uint32_t, std::vector<Move>> bySource;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const std::uint32_t from = object->graphScope();
            std::vector<std::size_t> chain = groupsOf(*object);
            std::size_t layer = object->layer();
            const bool placed = findNetwork(from) != nullptr;
            if (chain == object->placedGroups() && placed) {
                std::vector<std::size_t> derived = groupChainOfScope(from);
                if (derived != chain) {
                    object->setGroups(derived);
                    chain = std::move(derived);
                }
            }
            if (layer == object->placedLayer() && placed && !baseMoved) {
                const std::size_t derived = layerOfScope(from);
                if (derived != layer && findLayer(derived) != nullptr) {
                    object->setLayer(derived);
                    layer = derived;
                }
            }
            object->setPlacedGroups(chain);
            object->setPlacedLayer(layer);
            std::vector<std::size_t> keys;
            for (std::size_t id : layerPathOf(layer)) {
                keys.push_back(id | layerKey);
            }
            keys.insert(keys.end(), chain.begin(), chain.end());
            std::uint32_t to = from;
            if (!keys.empty()) {
                to = scopeFor(keys);
            } else if (from != 0 && (groupIdOfScope(from) != 0 || layerIdOfScope(from) != 0 || !placed)) {
                to = 0;
            }
            if (to != from) {
                bySource[from].push_back({object.get(), to});
            }
        }
        for (std::size_t id : missing) {
            std::vector<std::size_t> keys;
            for (std::size_t at : layerPathOf(id)) {
                keys.push_back(at | layerKey);
            }
            if (!keys.empty()) {
                scopeFor(keys);
            }
        }
        if (bySource.empty()) {
            return;
        }
        for (auto& [from, moves] : bySource) {
            jitnodes::Network* source = findNetwork(from);
            std::unordered_map<jitnodes::NodeId, jitnodes::NodeId> placed, following;
            if (source != nullptr) {
                const std::vector<jitnodes::NodeId> ids = source->nodeIds();
                placed = placementNodes(*source, ids);
                following = appearanceSuccessors(*source, ids);
            }
            for (const Move& move : moves) {
                SketchObject& object = *move.object;
                const jitnodes::NodeId shape = object.graphNode();
                if (source == nullptr || shape == jitnodes::invalidNodeId || !source->hasNode(shape)) {
                    object.setGraphRef(NodeRef(move.to, jitnodes::invalidNodeId));
                    object.forgetNodes();
                    continue;
                }
                if (!isShapeNodeType(source->node(shape).typeName())) {
                    continue;   // a live shape keeps its inputs where they are
                }
                // The chain is shape, what deforms it, its placement, then the look: all of it moves, so expressions and keys on it survive.
                std::vector<jitnodes::NodeId> chain{shape};
                const std::vector<jitnodes::NodeId> feeding = upstreamShapes(*source, shape);
                chain.insert(chain.begin(), feeding.rbegin(), feeding.rend());
                if (const auto place = placed.find(shape); place != placed.end()) {
                    std::vector<jitnodes::NodeId> between;
                    for (jitnodes::NodeId at = source->inputOf(place->second, 0); at != jitnodes::invalidNodeId && at != shape && between.size() < 1000;
                         at = source->inputOf(at, 0)) {
                        between.push_back(at);
                    }
                    chain.insert(chain.end(), between.rbegin(), between.rend());
                    chain.push_back(place->second);
                }
                for (jitnodes::NodeId link : appearanceChain(following, shape)) {
                    chain.push_back(link);
                }
                std::vector<jitnodes::NodeClip> clips;
                for (jitnodes::NodeId id : chain) {
                    clips.push_back(jitnodes::copyNode(*source, id));
                }
                const std::vector<jitnodes::NodeId> made = jitnodes::pasteNodes(networkOf(move.to), clips);
                for (std::size_t i = 1; i < made.size(); ++i) {
                    networkOf(move.to).connect(made[i - 1], made[i], 0);
                }
                for (jitnodes::NodeId id : chain) {
                    appearanceSynced_.erase(NodeRef(from, id));
                    source->removeNode(id);
                }
                object.setGraphRef(NodeRef(move.to, made[feeding.size()]));
                if (object.everBound()) {
                    object.setBoundToNodes(false);   // its placement is rewritten relative to the new group
                }
            }
        }
        placedCache_.clear();
        appearanceChains_.clear();
        drivenDirty_ = true;
    }

    // Each scope's `paint` merge node lists what the scope paints, back to front, so the stacking order is readable in the graph.
    // objects_ stays the carrier: this only follows it, and a subnet takes the place of its first member.
    void SketchDocument::syncPaintOrder() {
        ScopeViews views(*this, scopeTables_);
        std::map<std::uint32_t, std::vector<jitnodes::NodeId>> order;
        std::unordered_set<std::uint32_t> listed;
        paintOwners_.clear();
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const std::uint32_t scope = object->graphScope();
            const jitnodes::NodeId shape = object->graphNode();
            const jitnodes::Network* net = findNetwork(scope);
            if (net == nullptr || shape == jitnodes::invalidNodeId || !net->hasNode(shape)) {
                continue;
            }
            // The chain cache syncAppearanceNodes just filled; only an object without a look needs the placement scan.
            const auto cached = appearanceChains_.find(object->graphRef());
            if (cached != appearanceChains_.end() && !cached->second.nodes.empty() && net->hasNode(cached->second.nodes.back())) {
                order[scope].push_back(cached->second.nodes.back());
            } else {
                ScopeViews::View& view = views.of(scope);
                const auto place = view.placed.find(shape);
                order[scope].push_back(place != view.placed.end() ? place->second : shape);
            }
            paintOwners_[NodeRef(scope, order[scope].back())] = object->id();
            for (std::uint32_t at = scope; at != 0 && listed.insert(at).second; at = parentOfScope(at).scope) {
                const NodeRef subnet = parentOfScope(at);
                order[subnet.scope].push_back(subnet.id);
            }
        }
        placedPaint_ = order;
        placedOrder_.clear();
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            placedOrder_.push_back(object->id());
        }
        for (std::uint32_t scope = 0; scope < scopeCount(); ++scope) {
            jitnodes::Network* net = findNetwork(scope);
            if (net == nullptr) {
                continue;
            }
            const auto wanted = order.find(scope);
            const auto held = paintNodes_.find(scope);
            jitnodes::NodeId merge = held != paintNodes_.end() && net->hasNode(held->second) && net->node(held->second).isMerge() ? held->second : jitnodes::invalidNodeId;
            if (wanted == order.end()) {
                if (merge != jitnodes::invalidNodeId) {
                    net->removeNode(merge);
                }
                paintNodes_.erase(scope);
                continue;
            }
            if (merge == jitnodes::invalidNodeId) {
                const jitnodes::NodeId named = net->findNode("paint");   // a restore or a load brings the node back without the cache
                merge = named != jitnodes::invalidNodeId && net->node(named).isMerge() ? named : addOwnedNode(*net, scope, std::uint64_t(1) << 63, 0, "merge", net->uniqueName("paint"));
                paintNodes_[scope] = merge;
            }
            std::vector<jitnodes::NodeId> have;
            int last = -1;
            for (int i = 0; i < net->node(merge).inputCount(); ++i) {
                if (const jitnodes::NodeId in = net->inputOf(merge, i); in != jitnodes::invalidNodeId) {
                    have.push_back(in);
                    last = i;
                }
            }
            const std::vector<jitnodes::NodeId>& want = wanted->second;
            if (have == want) {
                continue;
            }
            std::size_t keep = 0;
            if (have.size() < want.size() && std::equal(have.begin(), have.end(), want.begin())) {
                keep = have.size();   // an object added on top is the common edit
            } else {
                for (int i = last; i >= 0; --i) {
                    if (net->inputOf(merge, i) != jitnodes::invalidNodeId) {   // clearing the top slots shrinks the node past a hole
                        net->disconnect(merge, i);
                    }
                }
                last = -1;
            }
            for (std::size_t i = keep; i < want.size(); ++i) {
                net->connect(want[i], merge, ++last);
            }
        }
    }

    void SketchDocument::pullPaintOrder() {
        if (placedOrder_.size() != objects_.size()) {
            return;
        }
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            if (objects_[i]->id() != placedOrder_[i]) {
                return;
            }
        }
        bool edited = false;
        for (const auto& [scope, want] : placedPaint_) {
            const jitnodes::Network* net = findNetwork(scope);
            const auto held = paintNodes_.find(scope);
            if (net == nullptr || held == paintNodes_.end() || !net->hasNode(held->second)) {
                continue;
            }
            std::vector<jitnodes::NodeId> have;
            for (int i = 0; i < net->node(held->second).inputCount(); ++i) {
                if (const jitnodes::NodeId in = net->inputOf(held->second, i); in != jitnodes::invalidNodeId) {
                    have.push_back(in);
                }
            }
            if (have == want) {
                continue;
            }
            std::vector<jitnodes::NodeId> a = have, b = want;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            edited = edited || a == b;   // other changes to the inputs are rewritten by syncPaintOrder
        }
        if (!edited) {
            return;
        }
        std::vector<std::unique_ptr<SketchObject>> sorted;
        for (std::size_t id : stackingOrder()) {
            for (std::unique_ptr<SketchObject>& object : objects_) {
                if (object != nullptr && object->id() == id) {
                    sorted.push_back(std::move(object));
                    break;
                }
            }
        }
        objects_ = std::move(sorted);
    }

    std::vector<std::size_t> SketchDocument::stackingOrder() {
        std::vector<std::size_t> out;
        std::unordered_set<std::size_t> seen;
        std::unordered_set<std::uint32_t> walked;
        const auto walk = [&](auto&& self, std::uint32_t scope) -> void {
            const jitnodes::Network* net = findNetwork(scope);
            const auto held = paintNodes_.find(scope);
            if (net == nullptr || held == paintNodes_.end() || !net->hasNode(held->second) || !walked.insert(scope).second) {
                return;
            }
            const jitnodes::Node& merge = net->node(held->second);
            for (int i = 0; i < merge.inputCount(); ++i) {
                const jitnodes::NodeId in = net->inputOf(held->second, i);
                if (in == jitnodes::invalidNodeId || !net->hasNode(in)) {
                    continue;
                }
                if (const auto owner = paintOwners_.find(NodeRef(scope, in)); owner != paintOwners_.end()) {
                    if (seen.insert(owner->second).second) {
                        out.push_back(owner->second);
                    }
                } else if (net->node(in).typeName() == kSubnetNodeType) {
                    self(self, scopeOfSubnet(NodeRef(scope, in)));
                }
            }
        };
        walk(walk, 0);
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (seen.insert(object->id()).second) {
                out.push_back(object->id());
            }
        }
        return out;
    }

    void SketchDocument::pruneGroupScopes() {
        if (scopeCount() == 1) {
            return;
        }
        std::unordered_set<std::uint32_t> used;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            for (std::uint32_t scope = object->graphScope(); scope != 0 && used.insert(scope).second; scope = parentOfScope(scope).scope) {
            }
        }
        for (std::uint32_t scope = 1; scope < scopeCount(); ++scope) {
            const std::size_t group = groupIdOfScope(scope);
            if (group == 0) {
                const NodeRef layerSubnet = parentOfScope(scope);
                if (const std::size_t layerId = layerIdOfScope(scope); layerId != 0 && hasNode(layerSubnet)) {
                    const Layer* layer = findLayer(layerId);
                    const std::vector<std::size_t> path = layer != nullptr ? layerPathOf(layerId) : std::vector<std::size_t>();
                    const bool housed = !path.empty() && layerIdOfScope(layerSubnet.scope) == (path.size() > 1 ? path[path.size() - 2] : 0);
                    if (!housed && used.count(scope) == 0) {
                        networkOf(layerSubnet.scope).removeNode(layerSubnet.id);
                    } else if (layer != nullptr) {
                        putLayerProps(layerSubnet, *layer);
                    }
                }
                continue;
            }
            const NodeRef subnet = parentOfScope(scope);
            if (used.count(scope) == 0 && hasNode(subnet)) {
                // A regrouped or restored group keeps its look: the props wait aside until a subnet for the id is made again.
                const GroupProps props = groupProps(group);
                if (!props.isDefault()) {
                    groupProps_[group] = props;
                }
                groupSubnets_.erase(group);
                networkOf(subnet.scope).removeNode(subnet.id);
            }
        }
    }

    std::vector<std::size_t> SketchDocument::groupChainOfScope(std::uint32_t scope) const {
        std::vector<std::size_t> chain;
        for (; scope != 0 && scope < scopeCount(); scope = parentOfScope(scope).scope) {
            if (const std::size_t group = groupIdOfScope(scope)) {
                chain.push_back(group);
            }
        }
        std::reverse(chain.begin(), chain.end());
        return chain;
    }

    NodeRef SketchDocument::groupSubnetOf(std::size_t group) const {
        if (group == 0) {
            return NodeRef();
        }
        const auto isIt = [&](const NodeRef& ref) {
            return hasNode(ref) && node(ref).typeName() == kSubnetNodeType && std::size_t(node(ref).getFloat("groupId")) == group;
        };
        if (const auto hit = groupSubnets_.find(group); hit != groupSubnets_.end() && isIt(hit->second)) {
            return hit->second;
        }
        for (std::uint32_t scope = 1; scope < scopeCount(); ++scope) {
            const NodeRef ref = parentOfScope(scope);
            if (isIt(ref)) {
                groupSubnets_[group] = ref;
                return ref;
            }
        }
        return NodeRef();
    }

    // The layer record stays the carrier; the subnet node shows it, so the graph reads like the Layers panel.
    void SketchDocument::putLayerProps(const NodeRef& subnet, const Layer& layer) {
        jitnodes::Node& held = node(subnet);
        const auto setFloat = [&](const char* name, float value) {
            if (held.getFloat(name) != value) {
                held.setFloat(name, value);
            }
        };
        setFloat("visible", layer.visible ? 1.0f : 0.0f);
        setFloat("locked", layer.locked ? 1.0f : 0.0f);
        setFloat("color", float(layer.color));
        const std::vector<std::size_t> siblings = layerChildren(layer.parent);
        setFloat("layerOrder", float(std::find(siblings.begin(), siblings.end(), layer.id) - siblings.begin()));
        setFloat("layerParent", float(layer.parent));   // a sublayer of the base layer sits in the root beside the top-level subnets
        if (held.getString("label") != layer.name) {
            held.setString("label", layer.name);
        }
        std::string wanted;
        for (char c : layer.name) {
            wanted += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
        }
        if (wanted.empty() || std::isdigit(static_cast<unsigned char>(wanted[0]))) {
            wanted = "layer" + wanted;
        }
        jitnodes::Network& net = networkOf(subnet.scope);
        if (held.name() != wanted) {
            const jitnodes::NodeId clash = net.findNode(wanted);
            if (clash == jitnodes::invalidNodeId) {
                net.renameNode(subnet.id, wanted);
            } else if (clash != subnet.id && held.name().compare(0, wanted.size(), wanted) != 0) {
                net.renameNode(subnet.id, net.uniqueName(wanted));
            }
        }
    }

    void SketchDocument::writeLayerTable(std::vector<Layer> layers) {
        layers_ = std::move(layers);
        layersEdited_ = true;
    }

    void SketchDocument::pullLayerNodes() {
        if (layersEdited_) {
            return;
        }
        const auto adopt = [](Layer& layer, const jitnodes::Node& held) {
            layer.visible = held.getFloat("visible") != 0.0f;
            layer.locked = held.getFloat("locked") != 0.0f;
            layer.color = std::uint32_t(held.getFloat("color"));
            layer.name = held.getString("label");
        };
        const std::size_t base = baseLayer();
        std::unordered_map<std::size_t, float> orders;
        std::unordered_map<std::size_t, NodeRef> housing;
        // Walks the scope table, not every node: a full node scan on each edit costs seconds at a few thousand objects.
        for (std::uint32_t scope = 1; scope < scopeCount(); ++scope) {
            const std::size_t id = layerIdOfScope(scope);
            const NodeRef subnet = parentOfScope(scope);
            if (id != 0 && id != base && hasNode(subnet)) {
                housing.emplace(id, subnet);
            }
        }
        // A subnet the records do not know is a layer made on the graph side (a restored graph, a hand-built subnet).
        for (const auto& [id, subnet] : housing) {
            if (findLayer(id) == nullptr) {
                Layer made;
                made.id = id;
                made.name = node(subnet).getString("label").empty() ? "Layer " + std::to_string(id) : node(subnet).getString("label");
                layers_.push_back(made);
            }
        }
        // Every layer but the base layer keeps a subnet, so one without any was taken out of the graph.
        layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [&](const Layer& layer) { return layer.id != base && housing.count(layer.id) == 0; }), layers_.end());
        if (findLayer(activeLayer_) == nullptr) {
            activeLayer_ = base;
        }
        for (const auto& [id, subnet] : housing) {
            if (node(subnet).getString("label").empty()) {
                continue;   // never written: the record is the only source
            }
            for (Layer& layer : layers_) {
                if (layer.id == id) {
                    adopt(layer, node(subnet));
                    orders[id] = node(subnet).getFloat("layerOrder");
                    if (node(subnet).getFloat("layerParent") >= 0.0f) {
                        layer.parent = std::size_t(node(subnet).getFloat("layerParent"));
                    }
                }
            }
        }
        // A parent that is gone, or a loop, puts the layer back at the top.
        for (Layer& layer : layers_) {
            std::size_t at = layer.parent;
            for (std::size_t guard = 0; at != 0 && at != layer.id && guard <= layers_.size(); ++guard) {
                const Layer* up = findLayer(at);
                at = up != nullptr ? up->parent : std::size_t(-1);
                if (up == nullptr) {
                    break;
                }
            }
            if (layer.parent != 0 && (at == layer.id || at == std::size_t(-1))) {
                layer.parent = 0;
            }
        }
        const jitnodes::Network& root = network();
        const jitnodes::NodeId marker = root.findNode(kBaseLayerNodeName);
        for (Layer& layer : layers_) {
            if (layer.id != base) {
                continue;
            }
            if (marker != jitnodes::invalidNodeId && root.node(marker).typeName() == kBaseLayerNodeType && std::size_t(root.node(marker).getFloat("layerId")) == base) {
                adopt(layer, root.node(marker));
            } else if (marker == jitnodes::invalidNodeId && base == 1) {
                layer.name = "Layer 1";
                layer.visible = true;
                layer.locked = false;
                layer.color = 0;
            }
        }
        // Siblings follow their order parameters; the base layer stays first among the top-level ones.
        std::map<std::size_t, std::vector<std::size_t>> slots;
        for (std::size_t i = 0; i < layers_.size(); ++i) {
            slots[layers_[i].parent].push_back(i);
        }
        for (const auto& [parent, at] : slots) {
            std::vector<Layer> sorted;
            for (std::size_t i : at) {
                sorted.push_back(layers_[i]);
            }
            std::vector<float> key;
            for (std::size_t i = 0; i < sorted.size(); ++i) {
                const auto found = orders.find(sorted[i].id);
                key.push_back(sorted[i].id == base ? -1.0f : found != orders.end() ? found->second : float(i));
            }
            std::vector<std::size_t> rank(sorted.size());
            for (std::size_t i = 0; i < rank.size(); ++i) {
                rank[i] = i;
            }
            std::stable_sort(rank.begin(), rank.end(), [&](std::size_t a, std::size_t b) { return key[a] < key[b]; });
            for (std::size_t i = 0; i < at.size(); ++i) {
                layers_[at[i]] = sorted[rank[i]];
            }
        }
    }

    void SketchDocument::putBaseLayer() {
        const Layer* base = findLayer(baseLayer());
        jitnodes::Network& root = network();
        jitnodes::NodeId marker = root.findNode(kBaseLayerNodeName);
        if (marker != jitnodes::invalidNodeId && root.node(marker).typeName() != kBaseLayerNodeType) {
            marker = jitnodes::invalidNodeId;
        }
        const bool plain = base == nullptr || (base->id == 1 && base->name == "Layer 1" && base->visible && !base->locked && base->color == 0);
        if (plain) {
            if (marker != jitnodes::invalidNodeId) {
                root.removeNode(marker);
            }
            return;
        }
        if (marker == jitnodes::invalidNodeId) {
            marker = root.addNode(kBaseLayerNodeType, root.uniqueName(kBaseLayerNodeName));
        }
        jitnodes::Node& held = root.node(marker);
        const auto setFloat = [&](const char* name, float value) {
            if (held.getFloat(name) != value) {
                held.setFloat(name, value);
            }
        };
        setFloat("layerId", float(base->id));
        setFloat("visible", base->visible ? 1.0f : 0.0f);
        setFloat("locked", base->locked ? 1.0f : 0.0f);
        setFloat("color", float(base->color));
        if (held.getString("label") != base->name) {
            held.setString("label", base->name);
        }
    }

    void SketchDocument::putGroupProps(std::size_t group, const GroupProps& props) {
        const NodeRef subnet = groupSubnetOf(group);
        if (!subnet.valid()) {
            if (props.isDefault()) {
                groupProps_.erase(group);
            } else {
                groupProps_[group] = props;
            }
            return;
        }
        groupProps_.erase(group);
        jitnodes::Node& held = node(subnet);
        if (held.getFloat("opacity") != float(props.opacity)) {
            held.setFloat("opacity", float(props.opacity));
        }
        if (held.getFloat("clip") != (props.clip ? 1.0f : 0.0f)) {
            held.setFloat("clip", props.clip ? 1.0f : 0.0f);
        }
        if (held.getString("blend") != blendModeCss(props.blendMode)) {
            held.setString("blend", blendModeCss(props.blendMode));
        }
    }

    void SketchDocument::syncPathNodes() {
        std::map<std::uint32_t, std::vector<SketchObject*>> byScope;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            byScope[object->graphScope()].push_back(object.get());
        }
        bool removedShape = false;
        // Every scope is visited, also one whose objects are all gone, so its shape nodes go too.
        for (std::uint32_t scope = 0; scope < scopeCount(); ++scope) {
            const auto found = byScope.find(scope);
            static const std::vector<SketchObject*> none;
            removedShape = syncPathScope(scope, found != byScope.end() ? found->second : none) || removedShape;
        }
        syncAppearanceNodes(removedShape);
    }

    bool SketchDocument::syncPathScope(std::uint32_t scope, const std::vector<SketchObject*>& objects) {
        jitnodes::Network* netPtr = findNetwork(scope);
        if (netPtr == nullptr) {
            return false;
        }
        bool removedShape = false;
        jitnodes::Network& net = *netPtr;
        auto& placedCache = placedCache_[scope];
        struct Bound {
            SketchObject* object;
            jitnodes::NodeId node;
            bool known;
            bool settled = false;   // untouched since the last sync, with its placement
        };
        std::vector<Bound> bound;
        std::unordered_set<jitnodes::NodeId> created;
        std::vector<std::pair<SketchObject*, jitnodes::NodeId>> objectsNow;
        objectsNow.reserve(objects.size());
        for (SketchObject* object : objects) {
            objectsNow.emplace_back(object, object->graphNode());
        }
        const auto before = pathSync_.find(scope);
        const bool steady = before != pathSync_.end() && before->second.net == netPtr && before->second.topology == net.topologyRevision() &&
            !placedCache.empty() && before->second.objects == objectsNow;
        if (steady) {
            for (const auto& [object, node] : before->second.bound) {
                bound.push_back({object, node, true});
            }
        }
        if (!steady) {
        // Since the last sync of this scope, which nodes were touched: an object bound then to a node that, with its placement node, has not been
        // touched since needs none of the checks below.
        std::unordered_set<jitnodes::NodeId> touchedNow;
        std::unordered_map<SketchObject*, jitnodes::NodeId> prevNodeOf;
        bool incremental = false;
        if (before != pathSync_.end() && before->second.net == netPtr && !placedCache.empty()) {
            std::vector<jitnodes::NodeId> touchedIds;
            if (net.touchedSince(before->second.touched, touchedIds)) {
                incremental = true;
                touchedNow.insert(touchedIds.begin(), touchedIds.end());
                prevNodeOf.reserve(before->second.bound.size());
                for (const auto& [object, node] : before->second.bound) {
                    prevNodeOf.emplace(object, node);
                }
            }
        }
        const auto settledNode = [&](SketchObject* object) {
            if (!incremental) {
                return jitnodes::invalidNodeId;
            }
            const auto prev = prevNodeOf.find(object);
            if (prev == prevNodeOf.end() || prev->second != object->graphNode() || touchedNow.count(prev->second) != 0) {
                return jitnodes::invalidNodeId;
            }
            const auto place = placedCache.find(prev->second);
            return place != placedCache.end() && touchedNow.count(place->second) == 0 ? prev->second : jitnodes::invalidNodeId;
        };
        std::unordered_set<jitnodes::NodeId> used;
        std::unordered_set<jitnodes::NodeId> madeNow;   // shape nodes created in this call: nothing is wired to them yet
        std::vector<jitnodes::NodeId> settled(objects.size(), jitnodes::invalidNodeId);
        for (std::size_t i = 0; i < objects.size(); ++i) {
            settled[i] = settledNode(objects[i]);
            if (settled[i] != jitnodes::invalidNodeId) {
                used.insert(settled[i]);
            }
        }
        for (std::size_t index = 0; index < objects.size(); ++index) {
            SketchObject* object = objects[index];
            if (settled[index] != jitnodes::invalidNodeId) {
                bound.push_back({object, settled[index], true, true});
                continue;
            }
            const char* type = shapeNodeType(*object);
            jitnodes::NodeId node = object->graphNode();   // `type` may become kShapeNodeType below for a parametric object
            const bool known = node != jitnodes::invalidNodeId && net.hasNode(node);
            if (known && !isShapeNodeType(net.node(node).typeName())) {
                continue;   // a live shape: its outline comes from its own node
            }
            if (!known || used.count(node) != 0 || !nodeTypeFits(net.node(node).typeName(), *object)) {
                // The id an object had before it was deleted is free again when undo brings it back, so references to it survive.
                const bool reuse = node != jitnodes::invalidNodeId && used.count(node) == 0 && !net.hasNode(node);
                LiveShape live;
                if (parametric_.count(object->id()) != 0 && recogniseBeforePlacement(*object, object->geometry(), live)) {
                    type = kShapeNodeType;
                }
                node = reuse ? net.addNodeWithId(node, type, net.uniqueName("path" + std::to_string(node))) : net.addNode(type, net.uniqueName("path"));
                if (type == kShapeNodeType) {
                    storeShapeNode(net.node(node), live);
                }
                object->setGraphNode(node);
                object->forgetNodes();
                madeNow.insert(node);
            }
            used.insert(node);
            bound.push_back({object, node, known});
        }
        // Shape nodes no object uses any more: the ones its objects left behind or that were touched (added, restored) since the last sync;
        // without that history, all of them.
        std::vector<jitnodes::NodeId> orphans;
        if (incremental) {
            for (const auto& [object, node] : before->second.bound) {
                if (used.count(node) == 0 && net.hasNode(node)) {
                    orphans.push_back(node);
                }
            }
            for (jitnodes::NodeId id : touchedNow) {
                if (used.count(id) == 0 && net.hasNode(id)) {
                    orphans.push_back(id);
                }
            }
        } else {
            orphans = net.nodeIds();
        }
        if (!orphans.empty()) {
            std::unordered_set<jitnodes::NodeId> feeding;
            for (const Bound& entry : bound) {
                for (jitnodes::NodeId up : upstreamShapes(net, entry.node)) {
                    feeding.insert(up);
                }
            }
            // Removing a shape node can leave the shape upstream of it with no user either.
            for (std::size_t at = 0; at < orphans.size(); ++at) {
                const jitnodes::NodeId id = orphans[at];
                if (net.hasNode(id) && used.count(id) == 0 && feeding.count(id) == 0 && isShapeNodeType(net.node(id).typeName())) {
                    for (jitnodes::NodeId up : upstreamShapes(net, id)) {
                        orphans.push_back(up);
                    }
                    net.removeNode(id);
                    removedShape = true;
                }
            }
        }

        // Every shape node is followed by a transform node, which takes over the appearance nodes that were wired to the shape (a graph made
        // before there were transform nodes). The wiring is only searched for when a shape has none or one was left without its shape.
        auto cached = [&](jitnodes::NodeId shape) {
            const auto it = placedCache.find(shape);
            return it != placedCache.end() && net.hasNode(it->second) && net.node(it->second).typeName() == kTransformNodeType &&
                shapeBehind(net, net.inputOf(it->second, 0)) == shape;
        };
        bool rewire = placedCache.empty();
        std::vector<jitnodes::NodeId> bare;   // new shape nodes still without a placement
        for (const Bound& entry : bound) {
            if (!rewire && !entry.settled && !cached(entry.node)) {
                if (madeNow.count(entry.node) != 0) {
                    bare.push_back(entry.node);
                    continue;
                }
                // A shape node that came back with its placement (an undo) is found through its consumers rather than by scanning the graph.
                jitnodes::NodeId found = jitnodes::invalidNodeId;
                for (jitnodes::NodeId consumer : net.consumersOf(entry.node)) {
                    if (net.node(consumer).typeName() == kTransformNodeType && net.inputOf(consumer, 0) == entry.node) {
                        found = consumer;
                        break;
                    }
                }
                if (found != jitnodes::invalidNodeId) {
                    placedCache[entry.node] = found;
                } else {
                    rewire = true;
                }
            }
        }
        if (!rewire) {
            for (auto it = placedCache.begin(); removedShape && it != placedCache.end();) {
                if (used.count(it->first) == 0) {
                    if (net.hasNode(it->second)) {
                        net.removeNode(it->second);
                    }
                    it = placedCache.erase(it);
                } else {
                    ++it;
                }
            }
            for (jitnodes::NodeId shape : bare) {
                const jitnodes::NodeId place = addOwnedNode(net, scope, shape, 0, kTransformNodeType, net.uniqueName("transform"));
                net.connect(shape, place, 0);
                placedCache.emplace(shape, place);
                created.insert(shape);
            }
        }
        if (rewire) {
            const std::vector<jitnodes::NodeId> live = net.nodeIds();
            const auto following = appearanceSuccessors(net, live);
            placedCache = placementNodes(net, live);
            for (const Bound& entry : bound) {
                if (placedCache.count(entry.node) != 0) {
                    continue;
                }
                const jitnodes::NodeId place = addOwnedNode(net, scope, entry.node, 0, kTransformNodeType, net.uniqueName("transform"));
                net.connect(entry.node, place, 0);
                if (const auto first = following.find(entry.node); first != following.end()) {
                    net.connect(place, first->second, 0);
                }
                placedCache.emplace(entry.node, place);
                created.insert(entry.node);
            }
            for (auto it = placedCache.begin(); it != placedCache.end();) {
                if (used.count(it->first) == 0) {
                    net.removeNode(it->second);
                    it = placedCache.erase(it);
                } else {
                    ++it;
                }
            }
        }

        }
        // A child's transform node takes its parent's as input 1. A loaded graph that links nodes gives its objects that parent.
        std::unordered_set<jitnodes::NodeId> relinked;
        std::unordered_map<jitnodes::NodeId, SketchObject*> byPlace;
        for (const Bound& entry : bound) {
            const jitnodes::NodeId placeId = placedCache.at(entry.node);
            const jitnodes::NodeId linked = net.inputOf(placeId, 1);
            const SketchObject* parent = parentOf(*this, *entry.object);
            if (entry.object->parent() != 0 && parent == nullptr) {
                entry.object->setParent(0);
            }
            jitnodes::NodeId want = jitnodes::invalidNodeId;
            if (parent != nullptr && parent->graphNode() != jitnodes::invalidNodeId) {
                const auto it = placedCache.find(parent->graphNode());
                want = it != placedCache.end() ? it->second : jitnodes::invalidNodeId;
            }
            if (linked != jitnodes::invalidNodeId && entry.known && !entry.object->everBound() && entry.object->parent() == 0) {
                if (byPlace.empty()) {
                    for (const Bound& other : bound) {
                        byPlace.emplace(placedCache.at(other.node), other.object);
                    }
                }
                const auto owner = byPlace.find(linked);
                if (owner != byPlace.end()) {
                    entry.object->setParent(owner->second->id());
                    want = linked;
                }
            }
            if (want != linked) {
                if (want == jitnodes::invalidNodeId) {
                    net.disconnect(placeId, 1);
                } else {
                    net.connect(want, placeId, 1);
                }
                relinked.insert(entry.node);
            }
        }

        for (const Bound& entry : bound) {
            const SketchObject* parent = parentOf(*this, *entry.object);
            const bool stale = !entry.object->boundToNodes();
            const bool fresh = created.count(entry.node) != 0;
            const bool follows = relinked.count(entry.node) != 0 || (parent != nullptr && !parent->boundToNodes());
            jitnodes::Node* place = (stale || fresh || follows) ? &net.node(placedCache.at(entry.node)) : nullptr;
            const BLMatrix2D world = place != nullptr ? worldOf(NodeRef(scope, placedCache.at(entry.node))) : BLMatrix2D::make_identity();
            const bool driven = drivenPlace(NodeRef(scope, placedCache.at(entry.node)));
            const bool loaded = !driven && entry.known && !entry.object->everBound() && place != nullptr;
            if (!loaded || !bindLoadedNode(*drawnGeometry(net, entry.node), &world, *entry.object)) {
                writeNode(net.node(entry.node), *entry.object, !driven);
            }
            if (place != nullptr) {
                if (fresh) {
                    centerTransformPivot(*place, *drawnGeometry(net, entry.node));
                }
                storeTransform(*place, localPlacement(*this, *entry.object, parent));
                storePaintWarp(*place, entry.object->paintWarp());
            }
            if (driven) {
                adoptChain(*entry.object, placedCache.at(entry.node));
            }
        }
        PathSync& state = pathSync_[scope];
        state.net = netPtr;
        state.topology = net.topologyRevision();
        state.touched = net.touchRevision();
        state.objects.clear();
        for (SketchObject* object : objects) {
            state.objects.emplace_back(object, object->graphNode());   // binding may have given an object a new node
        }
        state.bound.clear();
        for (const Bound& entry : bound) {
            state.bound.emplace_back(entry.object, entry.node);
        }
        return removedShape;
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

    bool SketchDocument::makeParametric(std::size_t objectId) {
        const SketchObject* object = findById(objectId);
        jitnodes::Network* net = object != nullptr ? findNetwork(object->graphScope()) : nullptr;
        const jitnodes::NodeId old = object != nullptr ? object->graphNode() : jitnodes::invalidNodeId;
        if (net == nullptr || old == jitnodes::invalidNodeId || !net->hasNode(old) || net->node(old).typeName() != kPathNodeType ||
            net->inputOf(old, 0) != jitnodes::invalidNodeId) {
            return false;
        }
        // The node holds the shape before the object's placement, so it is recognised from the outline without it.
        std::unique_ptr<SketchObject> local(SketchObject::make(object->type(), drawnPath(*object)));
        local->setFrameAngle(0.0);
        LiveShape live;
        if (!recogniseLiveShape(*local, live)) {
            return false;
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        // The new node takes the old one's id and its wires, so the object, its placement and its look stay as they are and undo finds them again.
        std::vector<std::pair<jitnodes::NodeId, int>> consumers;
        for (jitnodes::NodeId id : net->nodeIds()) {
            for (int slot = 0; slot < net->node(id).inputCount(); ++slot) {
                if (net->inputOf(id, slot) == old) {
                    consumers.emplace_back(id, slot);
                }
            }
        }
        const std::string name = net->node(old).name();
        net->removeNode(old);
        const jitnodes::NodeId made = net->addNodeWithId(old, kShapeNodeType, name);
        storeShapeNode(net->node(made), live);
        for (const auto& [id, slot] : consumers) {
            net->connect(made, id, slot);
        }
        commitGraphEdit(before, "Make Parametric");
        return true;
    }

    jitnodes::NodeId SketchDocument::addOwnedNode(jitnodes::Network& net, std::uint32_t scope, std::uint64_t owner, std::uint32_t role, const std::string& type,
        const std::string& name) {
        jitnodes::NodeId& reserved = reservedIds_[ReservedKey(scope, owner, role)];
        const jitnodes::NodeId id = reserved != jitnodes::invalidNodeId && !net.hasNode(reserved) ? net.addNodeWithId(reserved, type, name) : net.addNode(type, name);
        reserved = id;
        return id;
    }

    SketchObject& SketchDocument::addParametric(SketchObject* object, const std::string& undoDescription) {
        parametric_.insert(nextId_);   // addUndoable gives the object this id
        return addUndoable(object, undoDescription);
    }

    bool SketchDocument::isParametric(std::size_t objectId) const {
        const SketchObject* object = findById(objectId);
        const jitnodes::Network* net = object != nullptr ? findNetwork(object->graphScope()) : nullptr;
        return net != nullptr && object->graphNode() != jitnodes::invalidNodeId && net->hasNode(object->graphNode()) &&
               net->node(object->graphNode()).typeName() == kShapeNodeType;
    }

    bool SketchDocument::setParametricOutline(SketchObject& object, const BLPath& placed) {
        LiveShape live;
        if (!isParametric(object.id()) || !recogniseBeforePlacement(object, placed, live)) {
            return false;
        }
        const NodeRef ref(object.graphScope(), object.graphNode());
        if (shapeNodeLive(node(ref)).kind != live.kind) {
            return false;
        }
        for (const auto& [name, value] : shapeNodeValues(live)) {
            // Only what the edit changed is written, so a keyed number takes a key just when it was edited.
            if (std::fabs(double(node(ref).getFloat(name)) - value) > 1e-4) {
                setGraphFloatLive(ref, name, float(value));
            }
        }
        return true;
    }

    bool SketchDocument::freezeParametric(std::size_t objectId) {
        SketchObject* object = findById(objectId);
        if (object == nullptr || !isParametric(objectId)) {
            return false;
        }
        jitnodes::Network& net = networkOf(object->graphScope());
        // The path node takes the id the object already has (so undo and redo find it, as in makeParametric) and a copy of the shape goes upstream.
        const jitnodes::NodeId id = object->graphNode();
        // The copy takes the node's own numbers: a LiveShape read from it does not hold the centre, nor an unused kind's values.
        static const char* const kShapeParams[] = {"centerX", "centerY", "points", "outer", "innerRatio", "width", "height", "radius", "angle", "start", "sweep"};
        const std::string kind = net.node(id).getString("kind");
        std::map<std::string, std::pair<float, std::vector<jitnodes::Keyframe>>> params;
        for (const char* name : kShapeParams) {
            params[name] = {net.node(id).baseFloat(name), net.node(id).keyframes(name)};
        }
        std::vector<std::pair<jitnodes::NodeId, int>> consumers;
        for (jitnodes::NodeId other : net.nodeIds()) {
            for (int slot = 0; slot < net.node(other).inputCount(); ++slot) {
                if (net.inputOf(other, slot) == id) {
                    consumers.emplace_back(other, slot);
                }
            }
        }
        const std::string name = net.node(id).name();
        net.removeNode(id);
        const jitnodes::NodeId shape = net.addNode(kShapeNodeType, net.uniqueName(name));
        net.node(shape).setString("kind", kind);
        for (auto& [param, held] : params) {
            net.node(shape).setFloat(param, held.first);
            if (!held.second.empty()) {
                net.node(shape).setKeyframes(param, std::move(held.second));
            }
        }
        const jitnodes::NodeId path = net.addNodeWithId(id, kPathNodeType, net.uniqueName("path" + std::to_string(id)));
        net.connect(shape, path, 0);
        for (const auto& [other, slot] : consumers) {
            net.connect(path, other, slot);
        }
        net.freezeNode(path);
        return true;
    }

    NodeRef SketchDocument::transformRefOf(std::size_t objectId) const {
        const SketchObject* object = findById(objectId);
        if (object == nullptr || object->graphNode() == jitnodes::invalidNodeId) {
            return kNoNode;
        }
        const auto scoped = placedCache_.find(object->graphScope());
        if (scoped == placedCache_.end()) {
            return kNoNode;
        }
        const auto it = scoped->second.find(object->graphNode());
        if (it == scoped->second.end()) {
            return kNoNode;
        }
        try {
            const jitnodes::Network* net = findNetwork(object->graphScope());
            if (net != nullptr && net->node(it->second).typeName() == kTransformNodeType && shapeBehind(*net, net->inputOf(it->second, 0)) == object->graphNode()) {
                return NodeRef(object->graphScope(), it->second);
            }
        } catch (const std::exception&) {
        }
        return kNoNode;
    }

    bool SketchDocument::pathHeldByNode(const SketchObject& object) const {
        if (object.isText() || object.graphNode() == jitnodes::invalidNodeId || !object.boundToNodes()) {
            return false;
        }
        const NodeRef place = transformRefOf(object.id());
        if (!place.valid() || drivenPlace(place)) {
            return false;
        }
        try {
            const jitnodes::Node& shape = node(object.graphRef());
            return shape.typeName() == shapeNodeType(object) && shape.hasGeometryParam("curve") && shape.geometryParam("curve")->primitiveCount() > 0;
        } catch (const std::exception&) {
            return false;
        }
    }

    namespace {
        // The tag nodes between an object's shape and its placement node, placement end first.
        std::vector<jitnodes::NodeId> tagsBetween(const jitnodes::Network& net, jitnodes::NodeId place, jitnodes::NodeId shape) {
            std::vector<jitnodes::NodeId> tags;
            int guard = 0;
            for (jitnodes::NodeId at = net.inputOf(place, 0); at != jitnodes::invalidNodeId && at != shape && guard < 1000; at = net.inputOf(at, 0), ++guard) {
                if (net.node(at).typeName() == kTagNodeType) {
                    tags.push_back(at);
                }
            }
            return tags;
        }
    }

    NodeRef SketchDocument::tagNodeOf(std::size_t objectId, const std::string& group, bool create) {
        const SketchObject* object = findById(objectId);
        const NodeRef placeRef = transformRefOf(objectId);
        if (object == nullptr || !placeRef.valid() || group.empty()) {
            return kNoNode;
        }
        const jitnodes::NodeId place = placeRef.id;
        jitnodes::Network& net = networkOf(placeRef.scope);
        for (jitnodes::NodeId tag : tagsBetween(net, place, object->graphNode())) {
            if (net.node(tag).getString("group") == group) {
                return NodeRef(placeRef.scope, tag);
            }
        }
        if (!create) {
            return kNoNode;
        }
        const jitnodes::NodeId tag = net.addNode(kTagNodeType, net.uniqueName("tag"));
        net.node(tag).setString("group", group);
        net.connect(net.inputOf(place, 0), tag, 0);
        net.connect(tag, place, 0);
        return NodeRef(placeRef.scope, tag);
    }

    std::vector<std::string> SketchDocument::tagNames(std::size_t objectId) const {
        std::vector<std::string> names;
        const SketchObject* object = findById(objectId);
        const NodeRef place = transformRefOf(objectId);
        if (object != nullptr && place.valid()) {
            const jitnodes::Network& net = networkOf(place.scope);
            for (jitnodes::NodeId tag : tagsBetween(net, place.id, object->graphNode())) {
                names.push_back(net.node(tag).getString("group"));
            }
        }
        return names;
    }

    std::vector<int> SketchDocument::taggedIds(std::size_t objectId, const std::string& group) const {
        const SketchObject* object = findById(objectId);
        const NodeRef place = transformRefOf(objectId);
        if (object == nullptr || !place.valid()) {
            return {};
        }
        const jitnodes::Network& net = networkOf(place.scope);
        for (jitnodes::NodeId tag : tagsBetween(net, place.id, object->graphNode())) {
            if (net.node(tag).getString("group") == group) {
                return pointIdList(net.node(tag).getString("ids"));
            }
        }
        return {};
    }

    bool SketchDocument::tagPoints(std::size_t objectId, const std::string& group, const std::vector<int>& ids, bool on) {
        std::vector<int> held = taggedIds(objectId, group);
        const std::vector<int> before = held;
        if (on) {
            held.insert(held.end(), ids.begin(), ids.end());
        } else {
            held.erase(std::remove_if(held.begin(), held.end(), [&ids](int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }), held.end());
        }
        const std::string text = pointIdText(held);
        if (text == pointIdText(before) || (!on && before.empty())) {
            return false;
        }
        const jitnodes::GraphSnapshot snapshot = snapshotGraph();
        const NodeRef tag = tagNodeOf(objectId, group, on);
        if (!tag.valid()) {
            return false;
        }
        networkOf(tag.scope).node(tag.id).setString("ids", text);
        commitGraphEdit(snapshot, on ? "Tag Points" : "Untag Points");
        return true;
    }

    bool SketchDocument::clearTag(std::size_t objectId, const std::string& group) {
        const NodeRef tagRef = tagNodeOf(objectId, group, false);
        if (!tagRef.valid()) {
            return false;
        }
        const jitnodes::NodeId tag = tagRef.id;
        const jitnodes::GraphSnapshot snapshot = snapshotGraph();
        const NodeRef placeRef = transformRefOf(objectId);
        jitnodes::Network& net = networkOf(placeRef.scope);
        // Whatever read the tag (the next node towards the placement node) reads what fed it.
        jitnodes::NodeId consumer = placeRef.id;
        for (jitnodes::NodeId at = net.inputOf(consumer, 0); at != tag && at != jitnodes::invalidNodeId; at = net.inputOf(at, 0)) {
            consumer = at;
        }
        net.connect(net.inputOf(tag, 0), consumer, 0);
        net.removeNode(tag);
        commitGraphEdit(snapshot, "Clear Tag");
        return true;
    }

    bool SketchDocument::adoptChain(SketchObject& object, jitnodes::NodeId place) {
        try {
            const NodeRef placeRef(object.graphScope(), place);
            jitnodes::GeometryHandle cooked = cook(placeRef);
            if (cooked && placeRef.scope != 0) {
                cooked = placeGeometry(*cooked, scopeMatrix(placeRef.scope));   // the cook is in the group's space
            }
            BLPath path;
            if (!cooked || cooked->primitiveCount() == 0 || !pathFromGeometry(*cooked, path)) {
                return false;
            }
            const BLMatrix2D world = worldOf(placeRef);
            if (!sameCurveShape(*cooked, *geometryFromPath(object.geometry()), kFileTolerance / 10)) {
                object.setGeometry(path);
            }
            object.setPaintMatrix(world);
            object.setBoundToNodes(true);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    NodeRef SketchDocument::addMovePoints(std::size_t objectId, const std::string& group) {
        const SketchObject* object = findById(objectId);
        const NodeRef placeRef = transformRefOf(objectId);
        if (object == nullptr || !placeRef.valid()) {
            return kNoNode;
        }
        const jitnodes::NodeId place = placeRef.id;
        const jitnodes::GraphSnapshot snapshot = snapshotGraph();
        jitnodes::Network& net = networkOf(placeRef.scope);
        const jitnodes::NodeId mover = net.addNode(kMovePointsNodeType, net.uniqueName("movePoints"));
        net.node(mover).setString("group", group);
        // Rotating and scaling hold still about the middle of the points it moves.
        const jitnodes::NodeId feeder = net.inputOf(place, 0);
        net.connect(feeder, mover, 0);
        net.connect(mover, place, 0);
        try {
            const jitnodes::GeometryHandle fed = cook(NodeRef(placeRef.scope, feeder));
            const jitnodes::AttribArray* flags = group.empty() || !fed ? nullptr : fed->findAttrib(jitnodes::AttribClass::point, "group." + group);
            if (fed && fed->pointCount() > 0) {
                float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
                for (std::size_t i = 0; i < fed->pointCount(); ++i) {
                    if (group.empty() || (flags != nullptr && flags->ints()[i] != 0)) {
                        x0 = std::min(x0, fed->x()[i]);
                        x1 = std::max(x1, fed->x()[i]);
                        y0 = std::min(y0, fed->y()[i]);
                        y1 = std::max(y1, fed->y()[i]);
                    }
                }
                if (x0 <= x1) {
                    net.node(mover).setFloat("pivotX", (x0 + x1) * 0.5f);
                    net.node(mover).setFloat("pivotY", (y0 + y1) * 0.5f);
                }
            }
        } catch (const std::exception&) {
        }
        commitGraphEdit(snapshot, "Move Points");
        return NodeRef(placeRef.scope, mover);
    }

    void SketchDocument::syncDeformWidths(std::size_t spineId) {
        const SketchObject* spine = findById(spineId);
        const NodeRef place = transformRefOf(spineId);
        if (spine == nullptr || !place.valid()) {
            return;
        }
        const std::string text = widthProfileText(spine->strokeOptions().widths);
        jitnodes::Network& net = networkOf(place.scope);
        for (jitnodes::NodeId id : net.nodeIds()) {
            jitnodes::Node& node = net.node(id);
            if (node.typeName() == kDeformAlongPathNodeType && net.inputOf(id, 1) == place.id && node.getString("widths") != text) {
                node.setString("widths", text);
            }
        }
    }

    NodeRef SketchDocument::addDeformAlongPath(std::size_t artId, std::size_t spineId) {
        const SketchObject* art = findById(artId);
        const SketchObject* spine = findById(spineId);
        const NodeRef artPlace = transformRefOf(artId), spinePlace = transformRefOf(spineId);
        // Both must be in the graph, in the same scope (a wire cannot leave its network), and not the same object.
        if (art == nullptr || spine == nullptr || artId == spineId || !artPlace.valid() || !spinePlace.valid() || artPlace.scope != spinePlace.scope ||
            art->isText() || art->isImage()) {
            return kNoNode;
        }
        const jitnodes::GraphSnapshot snapshot = snapshotGraph();
        jitnodes::Network& net = networkOf(artPlace.scope);
        const jitnodes::NodeId deform = net.addNode(kDeformAlongPathNodeType, net.uniqueName("deformAlongPath"));
        try {
            const jitnodes::NodeId feeder = net.inputOf(artPlace.id, 0);
            net.connect(feeder, deform, 0);
            net.connect(spinePlace.id, deform, 1);
            net.connect(deform, artPlace.id, 0);
            // The artwork's own placement comes after the deformation, so the spine is brought into the space before it: where the artwork
            // sits now, the result lands on the spine.
            const BLMatrix2D into = inverseOf(worldTransformOf(net, artPlace.id));
            for (int i = 0; i < 6; ++i) {
                net.node(deform).setFloat(std::string("frame") + std::to_string(i), float(into.m[i]));
            }
            net.node(deform).setString("widths", widthProfileText(spine->strokeOptions().widths));
            const jitnodes::GeometryHandle bent = cook(NodeRef(artPlace.scope, deform));
            if (!bent || bent->primitiveCount() == 0) {
                throw std::runtime_error("nothing to bend");
            }
        } catch (const std::exception&) {
            restoreGraph(snapshot);   // a wire that would loop, or nothing to bend: leave the graph as it was
            return kNoNode;
        }
        commitGraphEdit(snapshot, "Deform Along Path");
        return NodeRef(artPlace.scope, deform);
    }

    NodeRef SketchDocument::deformNodeOf(std::size_t objectId) const {
        const NodeRef place = transformRefOf(objectId);
        if (!place.valid()) {
            return kNoNode;
        }
        const jitnodes::Network& net = networkOf(place.scope);
        if (!net.hasNode(place.id)) {
            return kNoNode;
        }
        // A node with no inputs (a shape, a plain path) ends the walk; asking it for input 0 would throw.
        auto feeder = [&net](jitnodes::NodeId id) { return net.node(id).inputCount() > 0 ? net.inputOf(id, 0) : jitnodes::invalidNodeId; };
        int guard = 0;
        for (jitnodes::NodeId at = feeder(place.id); at != jitnodes::invalidNodeId && net.hasNode(at) && guard < 1000; at = feeder(at), ++guard) {
            if (net.node(at).typeName() == kDeformAlongPathNodeType) {
                return NodeRef(place.scope, at);
            }
        }
        return kNoNode;
    }

    bool SketchDocument::hasDeformAlongPath() const {
        return std::any_of(selection_.begin(), selection_.end(), [this](std::size_t id) { return deformNodeOf(id).valid(); });
    }

    bool SketchDocument::releaseDeformAlongPath() {
        const jitnodes::GraphSnapshot snapshot = snapshotGraph();
        bool released = false;
        for (std::size_t objectId : selection_) {
            if (!deformNodeOf(objectId).valid()) {
                continue;
            }
            try {
                // The object takes its outline from its own shape node, so the bent outline goes there and the deform node (and what sits
                // between it and the shape) goes away; a parametric shape is frozen first, as a hand edit does.
                if (isParametric(objectId)) {
                    freezeParametric(objectId);
                }
                const NodeRef found = deformNodeOf(objectId);
                const SketchObject* object = findById(objectId);
                if (!found.valid() || object == nullptr) {
                    throw std::runtime_error("no deform node");
                }
                jitnodes::Network& net = networkOf(found.scope);
                const jitnodes::NodeId deform = found.id, base = object->graphNode();
                auto feeder = [&net](jitnodes::NodeId id) { return net.node(id).inputCount() > 0 ? net.inputOf(id, 0) : jitnodes::invalidNodeId; };
                std::vector<jitnodes::NodeId> between;
                jitnodes::NodeId at = feeder(deform);
                for (int guard = 0; at != jitnodes::invalidNodeId && at != base && guard < 1000; at = feeder(at), ++guard) {
                    between.push_back(at);
                }
                const jitnodes::GeometryHandle bent = net.cook(deform);
                if (at != base || net.node(base).typeName() != kPathNodeType || !bent || bent->primitiveCount() == 0) {
                    throw std::runtime_error("nothing to release");
                }
                std::vector<std::pair<jitnodes::NodeId, int>> consumers;
                for (jitnodes::NodeId other : net.nodeIds()) {
                    for (int slot = 0; slot < net.node(other).inputCount(); ++slot) {
                        if (net.inputOf(other, slot) == deform) {
                            consumers.emplace_back(other, slot);
                        }
                    }
                }
                if (net.node(base).frozen() || net.inputOf(base, 0) != jitnodes::invalidNodeId) {
                    net.node(base).setFrozenGeometry(bent);
                } else {
                    net.node(base).setGeometryParam("curve", bent);
                }
                net.removeNode(deform);
                for (jitnodes::NodeId id : between) {
                    net.removeNode(id);
                }
                for (const auto& [other, slot] : consumers) {
                    net.connect(base, other, slot);
                }
                released = true;
            } catch (const std::exception&) {
                restoreGraph(snapshot);
                return false;
            }
        }
        if (released) {
            commitGraphEdit(snapshot, "Release Deform");
        }
        return released;
    }

    bool SketchDocument::deformSelectionAlongPath() {
        if (selection_.size() != 2) {
            return false;
        }
        const SketchObject* first = findById(selection_[0]);
        const SketchObject* second = findById(selection_[1]);
        if (first == nullptr || second == nullptr) {
            return false;
        }
        const bool firstSpine = !first->isClosed() && second->isClosed();   // an open path is the spine; of two alike the later pick is
        const std::size_t spine = firstSpine ? first->id() : second->id(), art = firstSpine ? second->id() : first->id();
        return addDeformAlongPath(art, spine).valid();
    }

    int SketchDocument::pointIdNear(std::size_t objectId, const BLPoint& world, double reach) const {
        const SketchObject* object = findById(objectId);
        const NodeRef place = transformRefOf(objectId);
        if (object == nullptr || !place.valid()) {
            return -1;
        }
        try {
            const jitnodes::Network& net = networkOf(place.scope);
            const jitnodes::GeometryHandle& curve = net.node(object->graphNode()).geometryParam("curve");
            const jitnodes::AttribArray* ids = curve ? curve->findAttrib(jitnodes::AttribClass::point, kPointIdAttrib) : nullptr;
            if (ids == nullptr || ids->type() != jitnodes::AttribType::int32) {
                return -1;
            }
            const BLMatrix2D matrix = worldOf(place);
            int best = -1;
            double nearest = reach;
            for (const jitnodes::Primitive& prim : curve->primitives()) {
                for (std::uint32_t k = 0; k < prim.count; ++k) {
                    if (prim.kind == jitnodes::PrimKind::cubic && k % 3 != 0) {
                        continue;
                    }
                    const std::size_t at = prim.start + k;
                    const BLPoint p = matrix.map_point(double(curve->x()[at]), double(curve->y()[at]));
                    const double distance = std::hypot(p.x - world.x, p.y - world.y);
                    if (distance <= nearest) {
                        nearest = distance;
                        best = ids->ints()[at];
                    }
                }
            }
            return best;
        } catch (const std::exception&) {
            return -1;
        }
    }

    std::vector<BLPoint> SketchDocument::taggedPositions(std::size_t objectId, const std::string& group) const {
        std::vector<BLPoint> spots;
        const std::vector<int> wanted = taggedIds(objectId, group);
        const SketchObject* object = findById(objectId);
        const NodeRef place = transformRefOf(objectId);
        if (wanted.empty() || object == nullptr || !place.valid()) {
            return spots;
        }
        try {
            const jitnodes::Network& net = networkOf(place.scope);
            const jitnodes::GeometryHandle& curve = net.node(object->graphNode()).geometryParam("curve");
            const jitnodes::AttribArray* ids = curve ? curve->findAttrib(jitnodes::AttribClass::point, kPointIdAttrib) : nullptr;
            if (ids == nullptr || ids->type() != jitnodes::AttribType::int32) {
                return spots;
            }
            const BLMatrix2D matrix = worldOf(place);
            for (const jitnodes::Primitive& prim : curve->primitives()) {
                for (std::uint32_t k = 0; k < prim.count; ++k) {
                    const std::size_t at = prim.start + k;
                    if ((prim.kind != jitnodes::PrimKind::cubic || k % 3 == 0) && std::binary_search(wanted.begin(), wanted.end(), int(ids->ints()[at]))) {
                        spots.push_back(matrix.map_point(double(curve->x()[at]), double(curve->y()[at])));
                    }
                }
            }
        } catch (const std::exception&) {
        }
        return spots;
    }

    BLPath SketchDocument::placedOutline(const SketchObject& object) const {
        // One edited since its node was written (a drag in progress) is newer than the node.
        if (!object.boundToNodes()) {
            return object.geometry();
        }
        const auto cached = outlineCache_.find(object.id());
        if (cached != outlineCache_.end() && cached->second.revision == object.geometryRevision()) {
            return cached->second.path;
        }
        const NodeRef place = transformRefOf(object.id());
        // A driven object shows what its chain cooks (adoptChain), which the object holds and its shape node does not.
        if (place.valid() && !drivenPlace(place)) {
            try {
                const jitnodes::GeometryHandle& curve = node(object.graphRef()).geometryParam("curve");
                BLPath path;
                if (curve && curve->primitiveCount() > 0 && pathFromGeometry(*placeGeometry(*curve, worldOf(place)), path)) {
                    outlineCache_[object.id()] = PlacedOutline{object.geometryRevision(), path};
                    return path;
                }
            } catch (const std::exception&) {
            }
        }
        return object.geometry();
    }

    std::size_t SketchDocument::objectOfNode(const NodeRef& ref) const {
        if (!ref.valid() || findNetwork(ref.scope) == nullptr) {
            return 0;
        }
        const jitnodes::Network& net = networkOf(ref.scope);
        std::unordered_map<jitnodes::NodeId, jitnodes::NodeId> next;
        bool haveChains = false;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (object->graphNode() == jitnodes::invalidNodeId || object->graphScope() != ref.scope) {
                continue;
            }
            if (object->graphNode() == ref.id || transformRefOf(object->id()) == ref) {
                return object->id();
            }
            if (!haveChains) {
                next = appearanceSuccessors(net, net.nodeIds());
                haveChains = true;
            }
            for (jitnodes::NodeId link : appearanceChain(next, object->graphNode())) {
                if (link == ref.id) {
                    return object->id();
                }
            }
        }
        return 0;
    }

    NodeRef SketchDocument::hitTestNode(const BLPoint& point, double tolerance) const {
        const SketchObject* hit = hitTest(point, tolerance);
        return hit != nullptr ? transformRefOf(hit->id()) : kNoNode;
    }

    std::vector<NodeRef> SketchDocument::selectedNodes() const {
        std::vector<NodeRef> nodes;
        for (std::size_t id : selection_) {
            const NodeRef place = transformRefOf(id);
            if (place.valid()) {
                nodes.push_back(place);
            }
        }
        return nodes;
    }

    void SketchDocument::setSelectedNodes(const std::vector<NodeRef>& nodes) {
        std::vector<std::size_t> ids;
        for (const NodeRef& node : nodes) {
            if (const std::size_t id = objectOfNode(node)) {
                ids.push_back(id);
            }
        }
        setSelection(ids);
    }

    std::vector<std::size_t> SketchDocument::withDescendants(const std::vector<std::size_t>& ids) const {
        std::vector<std::size_t> result = ids;
        std::unordered_set<std::size_t> seen(ids.begin(), ids.end());
        std::unordered_map<std::size_t, std::vector<std::size_t>> children;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (object->parent() != 0) {
                children[object->parent()].push_back(object->id());
            }
        }
        if (children.empty()) {
            return result;
        }
        for (std::size_t at = 0; at < result.size(); ++at) {
            const auto found = children.find(result[at]);
            if (found == children.end()) {
                continue;
            }
            for (std::size_t child : found->second) {
                if (seen.insert(child).second) {
                    result.push_back(child);
                }
            }
        }
        return result;
    }

    bool SketchDocument::setParent(std::size_t childId, std::size_t parentId) {
        SketchObject* child = findById(childId);
        if (child == nullptr || child->parent() == parentId || (parentId != 0 && findById(parentId) == nullptr)) {
            return false;
        }
        for (std::size_t up = parentId; up != 0;) {
            if (up == childId) {
                return false;   // a loop
            }
            const SketchObject* above = findById(up);
            up = above != nullptr ? above->parent() : 0;
        }
        const std::size_t previous = child->parent();
        auto apply = [this, childId](std::size_t parent) {
            if (SketchObject* object = findById(childId)) {
                object->setParent(parent);
            }
            changed();
        };
        undoStack_.push({"Parent", [apply, parentId]() { apply(parentId); }, [apply, previous]() { apply(previous); }});
        return true;
    }

    namespace {
        struct ParentChange {
            std::size_t child, before, after;
        };
    }

    bool SketchDocument::parentSelection() {
        if (!canParentSelection()) {
            return false;
        }
        const std::size_t parentId = selection_.back();
        std::vector<ParentChange> changes;
        for (std::size_t childId : selection_) {
            const SketchObject* child = findById(childId);
            if (childId == parentId || child == nullptr || child->parent() == parentId) {
                continue;
            }
            bool loop = false;
            for (std::size_t up = parentId; up != 0 && !loop;) {
                loop = up == childId;
                const SketchObject* above = findById(up);
                up = above != nullptr ? above->parent() : 0;
            }
            if (!loop) {
                changes.push_back({childId, child->parent(), parentId});
            }
        }
        if (changes.empty()) {
            return false;
        }
        auto apply = [this, changes](bool forward) {
            for (const ParentChange& change : changes) {
                if (SketchObject* object = findById(change.child)) {
                    object->setParent(forward ? change.after : change.before);
                }
            }
            changed();
        };
        undoStack_.push({"Parent", [apply]() { apply(true); }, [apply]() { apply(false); }});
        return true;
    }

    bool SketchDocument::canReleaseParent() const {
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object != nullptr && object->parent() != 0) {
                return true;
            }
        }
        return false;
    }

    bool SketchDocument::releaseParent() {
        std::vector<ParentChange> changes;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object != nullptr && object->parent() != 0) {
                changes.push_back({id, object->parent(), 0});
            }
        }
        if (changes.empty()) {
            return false;
        }
        auto apply = [this, changes](bool forward) {
            for (const ParentChange& change : changes) {
                if (SketchObject* object = findById(change.child)) {
                    object->setParent(forward ? change.after : change.before);
                }
            }
            changed();
        };
        undoStack_.push({"Release Parent", [apply]() { apply(true); }, [apply]() { apply(false); }});
        return true;
    }

}
