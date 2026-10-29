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
                if (net.node(at).typeName() == kMovePointsNodeType || animatedNode(net.node(at))) {
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
                // A node keeps what it holds beyond positions (per-anchor attributes) while the layout stays the same.
                const jitnodes::GeometryHandle& held = node.geometryParam("curve");
                const jitnodes::GeometryHandle fresh = geometryFromPath(drawnPath(object), held.get());
                if (!sameCurveShape(*held, *fresh, 1e-6)) {
                    node.setGeometryParam("curve", fresh);
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

        // A node bound to an object that has not been written to it yet (a loaded file) stays as the file had it when it agrees with the
        // object to within what a file holds; otherwise the object wins and the node follows.
        bool bindLoadedNode(jitnodes::Node& node, const BLMatrix2D* world, SketchObject& object) {
            const jitnodes::Geometry& held = *node.geometryParam("curve");
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
            struct View {
                jitnodes::Network* net = nullptr;   // null when the subnet holding the scope is gone
                std::unordered_set<jitnodes::NodeId> existing;
                std::unordered_map<jitnodes::NodeId, jitnodes::NodeId> placed, next;
                bool haveNext = false;
            };

            explicit ScopeViews(GraphDocument& document) : document_(document) {}

            // The appearance chain wiring, which only some passes need.
            const std::unordered_map<jitnodes::NodeId, jitnodes::NodeId>& nextOf(View& view) {
                if (!view.haveNext && view.net != nullptr) {
                    view.next = appearanceSuccessors(*view.net, view.net->nodeIds());
                    view.haveNext = true;
                }
                return view.next;
            }

            View& of(std::uint32_t scope) {
                auto [it, fresh] = views_.try_emplace(scope);
                if (fresh) {
                    it->second.net = document_.findNetwork(scope);
                    if (it->second.net != nullptr) {
                        const std::vector<jitnodes::NodeId> ids = it->second.net->nodeIds();
                        it->second.existing.insert(ids.begin(), ids.end());
                        it->second.placed = placementNodes(*it->second.net, ids);
                    }
                }
                return it->second;
            }

        private:
            GraphDocument& document_;
            std::unordered_map<std::uint32_t, View> views_;
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
        if (drivenDirty_) {
            drivenPlaces_.clear();
            for (std::uint32_t scope = 0; scope < scopeCount(); ++scope) {
                const jitnodes::Network* net = findNetwork(scope);
                if (net == nullptr) {
                    continue;
                }
                for (jitnodes::NodeId id : net->nodeIds()) {
                    if (net->node(id).typeName() == kTransformNodeType && drivenBy(*net, id, shapeBehind(*net, net->inputOf(id, 0)))) {
                        drivenPlaces_.insert(NodeRef(scope, id));
                    }
                }
            }
            drivenDirty_ = false;
        }
        return drivenPlaces_.count(place) != 0;
    }

    void SketchDocument::timeChanged(bool graphAffected) {
        if (graphAffected) {
            outlineCache_.clear();
        }
        ScopeViews views(*this);
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
        placedCache_.clear();
        outlineCache_.clear();
        appearanceChains_.clear();
        pullPathNodes();
        refreshLiveShapes();
        changed();
    }

    void SketchDocument::refreshLiveShapes() {
        ScopeViews views(*this);
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
        ScopeViews views(*this);
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            const std::uint32_t scope = object->graphScope();
            ScopeViews::View& view = views.of(scope);
            if (view.net == nullptr) {
                continue;
            }
            jitnodes::Network& net = *view.net;
            const auto& existing = view.existing;
            const auto& placed = view.placed;
            const jitnodes::NodeId node = object->graphNode();
            if (node == jitnodes::invalidNodeId || existing.count(node) == 0 || net.node(node).typeName() != shapeNodeType(*object)) {
                continue;
            }
            // An object whose chain deforms its shape shows what the chain cooks, whatever was drawn on it since.
            if (const auto driver = placed.find(node); driver != placed.end() && drivenPlace(NodeRef(scope, driver->second))) {
                adoptChain(*object, driver->second);
                continue;
            }
            // An object edited since its last sync (or never synced, as after a load) is newer than its node and wins.
            if (!object->boundToNodes()) {
                continue;
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
                continue;
            }
            // What the graph says the object is: the drawn curve carried by the transform node.
            const jitnodes::Geometry& drawn = *target.geometryParam("curve");
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
        pullAppearanceNodes();
    }

    void SketchDocument::pullAppearanceNodes() {
        ScopeViews views(*this);
        const AppearanceLibrary library{[this](std::size_t id) { return findPattern(id); }, [this](std::size_t id) { return findBrush(id); }};
        for (const std::unique_ptr<SketchObject>& object : objects_) {
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
        ScopeViews views(*this);
        scan = scan || appearanceObjectCount_ != objects_.size();
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
            scan = true;
            std::vector<jitnodes::NodeId> chain = appearanceChain(views.nextOf(view), shape);
            std::size_t keep = 0;
            while (keep < chain.size() && keep < items.size() && net.node(chain[keep]).typeName() == appearanceNodeType(items[keep].kind)) {
                ++keep;
            }
            while (chain.size() > keep) {
                net.removeNode(chain.back());
                appearanceSynced_.erase(NodeRef(scope, chain.back()));
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
                const auto held = appearanceSynced_.find(NodeRef(scope, chain[i]));
                if (held == appearanceSynced_.end() || held->second != want) {
                    if (paramsOf(node) != want) {
                        store(node, want);
                    }
                    appearanceSynced_[NodeRef(scope, chain[i])] = want;
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

    void SketchDocument::syncGroupScopes() {
        bool work = false;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (isGrouped(*object) || object->graphScope() != 0) {
                work = true;
                break;
            }
        }
        if (!work) {
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
                for (jitnodes::NodeId id : net.nodeIds()) {
                    if (net.node(id).typeName() == kSubnetNodeType && std::size_t(net.node(id).getFloat("groupId")) == group) {
                        found = id;
                        break;
                    }
                }
                if (found == jitnodes::invalidNodeId) {
                    found = net.addNode(kSubnetNodeType, net.uniqueName("group"));
                    net.node(found).setFloat("groupId", float(group));
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
            const std::vector<std::size_t>& chain = groupsOf(*object);
            std::uint32_t to = from;
            if (!chain.empty()) {
                to = scopeFor(chain);
            } else if (from != 0 && (groupIdOfScope(from) != 0 || findNetwork(from) == nullptr)) {
                to = 0;
            }
            if (to != from) {
                bySource[from].push_back({object.get(), to});
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
                object.setGraphRef(NodeRef(move.to, made.front()));
                if (object.everBound()) {
                    object.setBoundToNodes(false);   // its placement is rewritten relative to the new group
                }
            }
        }
        placedCache_.clear();
        appearanceChains_.clear();
        drivenDirty_ = true;
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
                continue;
            }
            const NodeRef subnet = parentOfScope(scope);
            if (used.count(scope) == 0) {
                networkOf(subnet.scope).removeNode(subnet.id);
                continue;
            }
            jitnodes::Node& held = node(subnet);
            const GroupProps props = groupProps(group);
            if (held.getFloat("opacity") != float(props.opacity)) {
                held.setFloat("opacity", float(props.opacity));
            }
            if (held.getFloat("clip") != (props.clip ? 1.0f : 0.0f)) {
                held.setFloat("clip", props.clip ? 1.0f : 0.0f);
            }
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
        const std::vector<jitnodes::NodeId> ids = net.nodeIds();
        const std::unordered_set<jitnodes::NodeId> existing(ids.begin(), ids.end());
        std::unordered_set<jitnodes::NodeId> used;
        struct Bound {
            SketchObject* object;
            jitnodes::NodeId node;
            bool known;
        };
        std::vector<Bound> bound;
        for (SketchObject* object : objects) {
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
                object->forgetNodes();
            }
            used.insert(node);
            bound.push_back({object, node, known});
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
            const auto it = placedCache.find(shape);
            return it != placedCache.end() && existing.count(it->second) != 0 && net.node(it->second).typeName() == kTransformNodeType &&
                shapeBehind(net, net.inputOf(it->second, 0)) == shape;
        };
        bool rewire = placedCache.empty() || removedShape;
        for (const Bound& entry : bound) {
            rewire = rewire || !cached(entry.node);
        }
        std::unordered_set<jitnodes::NodeId> created;
        if (rewire) {
            const std::vector<jitnodes::NodeId> live = net.nodeIds();
            const auto following = appearanceSuccessors(net, live);
            placedCache = placementNodes(net, live);
            for (const Bound& entry : bound) {
                if (placedCache.count(entry.node) != 0) {
                    continue;
                }
                const jitnodes::NodeId place = net.addNode(kTransformNodeType, net.uniqueName("transform"));
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
            if (driven || !(entry.known && !entry.object->everBound() && place != nullptr && bindLoadedNode(net.node(entry.node), &world, *entry.object))) {
                writeNode(net.node(entry.node), *entry.object, !driven);
            }
            if (place != nullptr) {
                if (fresh) {
                    centerTransformPivot(*place, *net.node(entry.node).geometryParam("curve"));
                }
                storeTransform(*place, localPlacement(*this, *entry.object, parent));
                storePaintWarp(*place, entry.object->paintWarp());
            }
            if (driven) {
                adoptChain(*entry.object, placedCache.at(entry.node));
            }
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

    jitnodes::NodeId SketchDocument::transformNodeOf(std::size_t objectId) const {
        const NodeRef ref = transformRefOf(objectId);
        return ref.scope == 0 ? ref.id : jitnodes::invalidNodeId;
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

    jitnodes::NodeId SketchDocument::tagNodeOf(std::size_t objectId, const std::string& group, bool create) {
        const SketchObject* object = findById(objectId);
        const NodeRef placeRef = transformRefOf(objectId);
        if (object == nullptr || !placeRef.valid() || group.empty()) {
            return jitnodes::invalidNodeId;
        }
        const jitnodes::NodeId place = placeRef.id;
        jitnodes::Network& net = networkOf(placeRef.scope);
        for (jitnodes::NodeId tag : tagsBetween(net, place, object->graphNode())) {
            if (net.node(tag).getString("group") == group) {
                return tag;
            }
        }
        if (!create) {
            return jitnodes::invalidNodeId;
        }
        const jitnodes::NodeId tag = net.addNode(kTagNodeType, net.uniqueName("tag"));
        net.node(tag).setString("group", group);
        net.connect(net.inputOf(place, 0), tag, 0);
        net.connect(tag, place, 0);
        return tag;
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
        const jitnodes::NodeId tag = tagNodeOf(objectId, group, on);
        if (tag == jitnodes::invalidNodeId) {
            return false;
        }
        networkOf(findById(objectId)->graphScope()).node(tag).setString("ids", text);
        commitGraphEdit(snapshot, on ? "Tag Points" : "Untag Points");
        return true;
    }

    bool SketchDocument::clearTag(std::size_t objectId, const std::string& group) {
        const jitnodes::NodeId tag = tagNodeOf(objectId, group, false);
        if (tag == jitnodes::invalidNodeId) {
            return false;
        }
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

    jitnodes::NodeId SketchDocument::addMovePoints(std::size_t objectId, const std::string& group) {
        const SketchObject* object = findById(objectId);
        const NodeRef placeRef = transformRefOf(objectId);
        if (object == nullptr || !placeRef.valid()) {
            return jitnodes::invalidNodeId;
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
        return mover;
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
