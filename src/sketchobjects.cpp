#include "curvy/sketchdocument.h"

#include "curvy/appearancenode.h"
#include "curvy/objectnode.h"
#include "curvy/selectionmodel.h"

#include <jitnodes/graph/network.h>
#include <jitnodes/graph/serialize.h>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

namespace curvy {

    using jitnodes::Network;
    using jitnodes::NodeId;

    NodeRef SketchDocument::addLayer(const std::string& name) {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        Network& root = network();
        const NodeRef ref(0, ObjectTree::addObject(root, ObjectTree::ensureRootMerge(root), layerTag, name));
        commitGraphEdit(before, "Add Layer");
        return ref;
    }

    NodeRef SketchDocument::addGroup(const NodeRef& parent, const std::string& name) {
        ObjectNode* owner = hasNode(parent) ? dynamic_cast<ObjectNode*>(&node(parent)) : nullptr;
        if (owner == nullptr) {
            throw std::runtime_error("a group goes into a layer or another group");
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        const NodeRef ref(scopeOfSubnet(parent), ObjectTree::addObject(owner->network(), owner->mergeId(), groupTag, name));
        commitGraphEdit(before, "Add Group");
        return ref;
    }

    std::vector<NodeRef> SketchDocument::childrenOf(const NodeRef& parent) {
        std::vector<NodeRef> children;
        Network* net = nullptr;
        NodeId merge = jitnodes::invalidNodeId;
        std::uint32_t scope = 0;
        if (!parent.valid()) {
            net = &network();
            merge = ObjectTree::rootMergeOf(*net);
        } else if (hasNode(parent)) {
            if (auto* owner = dynamic_cast<ObjectNode*>(&node(parent))) {
                net = &owner->network();
                merge = owner->mergeId();
                scope = scopeOfSubnet(parent);
            }
        }
        if (net == nullptr || merge == jitnodes::invalidNodeId) {
            return children;
        }
        for (const NodeId id : ObjectTree::mergeInputs(*net, merge)) {
            if (ObjectTree::objectAt(*net, id) != nullptr) {
                children.emplace_back(scope, id);
            }
        }
        return children;
    }

    void SketchDocument::reorderChildren(const NodeRef& parent, const std::vector<NodeRef>& order) {
        const std::vector<NodeRef> current = childrenOf(parent);
        std::vector<NodeRef> a = current;
        std::vector<NodeRef> b = order;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b) {
            throw std::runtime_error("the new order is not a permutation of the children");
        }
        if (current == order) {
            return;
        }
        Network* net = &network();
        NodeId merge = ObjectTree::rootMergeOf(*net);
        if (parent.valid()) {
            ObjectNode& owner = static_cast<ObjectNode&>(node(parent));
            net = &owner.network();
            merge = owner.mergeId();
        }
        std::vector<NodeId> ids;
        for (const NodeRef& ref : order) {
            ids.push_back(ref.id);
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        ObjectTree::setMergeInputs(*net, merge, ids);
        commitGraphEdit(before, parent.valid() ? "Reorder Groups" : "Reorder Layers");
    }

    NodeRef SketchDocument::moveGroup(const NodeRef& group, const NodeRef& parent, std::size_t index) {
        if (!hasNode(group) || !node(group).hasTag(groupTag)) {
            throw std::runtime_error("only a group can be moved");
        }
        ObjectNode* target = hasNode(parent) ? dynamic_cast<ObjectNode*>(&node(parent)) : nullptr;
        if (target == nullptr) {
            throw std::runtime_error("a group goes into a layer or another group");
        }
        for (NodeRef up = parent; up.valid(); up = parentOfScope(up.scope)) {
            if (up == group) {
                throw std::runtime_error("a group cannot go into itself");
            }
        }

        std::vector<NodeRef> siblings = childrenOf(parent);
        const auto present = std::find(siblings.begin(), siblings.end(), group);
        if (present != siblings.end()) {
            siblings.erase(present);
            siblings.insert(siblings.begin() + std::ptrdiff_t((std::min)(index, siblings.size())), group);
            reorderChildren(parent, siblings);
            return group;
        }

        const jitnodes::GraphSnapshot before = snapshotGraph();
        const std::uint32_t targetScope = scopeOfSubnet(parent);
        Network& from = networkOf(group.scope);
        Network& into = target->network();
        const NodeId pasted = jitnodes::pasteNodes(into, {jitnodes::copyNode(from, group.id)}).front();
        from.removeNode(group.id);
        std::vector<NodeId> order = ObjectTree::mergeInputs(into, target->mergeId());
        order.insert(order.begin() + std::ptrdiff_t((std::min)(index, order.size())), pasted);
        ObjectTree::setMergeInputs(into, target->mergeId(), order);
        commitGraphEdit(before, "Move Group");
        return NodeRef(targetScope, pasted);
    }

    void SketchDocument::findObjectProblems(const Network& net, std::vector<NodeId>& path) {
        for (const NodeId id : net.nodeIds()) {
            const jitnodes::Node& node = net.node(id);
            if (const auto* object = dynamic_cast<const ObjectNode*>(&node)) {
                const std::string problem = object->validate();
                if (!problem.empty()) {
                    objectProblems_.push_back({NodeRef(scopeOf(path), id), node.name() + ": " + problem});
                }
            }
            if (const Network* child = node.childNetwork()) {
                if (!node.isAssetInstance()) {
                    path.push_back(id);
                    findObjectProblems(*child, path);
                    path.pop_back();
                }
            }
        }
    }

    void SketchDocument::repairObjectProblems() {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        for (const ObjectProblem& problem : objectProblems_) {
            if (hasNode(problem.object)) {
                if (auto* object = dynamic_cast<ObjectNode*>(&node(problem.object))) {
                    object->repair();
                }
            }
        }
        objectProblems_.clear();
        commitGraphEdit(before, "Repair Objects");
    }

    void SketchDocument::buildSampleLayers() {
        const jitnodes::GraphSnapshot before = snapshotGraph();
        Network& root = network();
        const NodeId rootMerge = ObjectTree::ensureRootMerge(root);
        float shift = 0.0f;
        const auto layer = [&](const char* name, std::initializer_list<const char*> groups) {
            ObjectNode& object = *ObjectTree::objectAt(root, ObjectTree::addObject(root, rootMerge, layerTag, name));
            for (const char* group : groups) {
                ObjectNode& child = *ObjectTree::objectAt(object.network(), ObjectTree::addObject(object.network(), object.mergeId(), groupTag, group));
                const float hue = shift / 5.0f;
                ObjectTree::addCircle(child.network(), child.mergeId(), 150.0f + shift * 60.0f, 200.0f + shift * 50.0f, 90.0f,
                                      newui::Color(0.9f - hue * 0.7f, 0.3f + hue * 0.4f, 0.2f + hue * 0.7f, 0.8f));
                shift += 1.0f;
            }
        };
        layer("Layer1", {"Group1", "Group2"});
        layer("Layer2", {"Group3"});
        layer("Layer3", {"GroupA"});
        commitGraphEdit(before, "Add Sample Layers");
    }

    NodeRef SketchDocument::addCircle(const NodeRef& object, float cx, float cy, float radius, const newui::Color& fill) {
        ObjectNode* owner = hasNode(object) ? dynamic_cast<ObjectNode*>(&node(object)) : nullptr;
        if (owner == nullptr) {
            throw std::runtime_error("a circle goes into a layer or a group");
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        const NodeId look = ObjectTree::addCircle(owner->network(), owner->mergeId(), cx, cy, radius, fill);
        commitGraphEdit(before, "Add Circle");
        return NodeRef(scopeOfSubnet(object), look);
    }

    namespace {

        const char* const objectTransformParams[] = {"tx", "ty", "rot", "sx", "sy", "skew", "px", "py"};

        NodeRef objectXformRef(SketchDocument& document, const NodeRef& object) {
            ObjectNode* owner = document.hasNode(object) ? dynamic_cast<ObjectNode*>(&document.node(object)) : nullptr;
            if (owner == nullptr) {
                throw std::runtime_error("a transform belongs to a layer or a group");
            }
            const NodeId xform = owner->xformId();
            if (xform == jitnodes::invalidNodeId) {
                throw std::runtime_error("the object has no transform node (repair it first)");
            }
            return NodeRef(document.scopeOfSubnet(object), xform);
        }

    }

    namespace {

        void checkTransformNames(const std::map<std::string, float>& values) {
            for (const auto& entry : values) {
                if (std::find_if(std::begin(objectTransformParams), std::end(objectTransformParams), [&](const char* name) { return entry.first == name; }) ==
                    std::end(objectTransformParams)) {
                    throw std::runtime_error("unknown transform parameter '" + entry.first + "'");
                }
            }
        }

        std::map<std::string, float> transformValues(const jitnodes::Node& xform) {
            std::map<std::string, float> values;
            for (const char* name : objectTransformParams) {
                values[name] = xform.getFloat(name);
            }
            return values;
        }

        // The generator of the shape `from` belongs to, which is where a local transform goes after.
        NodeRef shapeGeneratorOf(SketchDocument& document, const NodeRef& from) {
            if (!document.hasNode(from)) {
                throw std::runtime_error("no such node");
            }
            SelectionModel probe(&document);
            const NodeRef shape = probe.find(SelectionModel::Role::Shape, from);
            if (!shape.valid() || dynamic_cast<ObjectNode*>(&document.node(shape)) != nullptr) {
                throw std::runtime_error("the node is not part of a shape (a layer or group has its own transform)");
            }
            return shape;
        }

        NodeRef findLocalTransform(SketchDocument& document, const NodeRef& shape) {
            SelectionModel probe(&document);
            return probe.find(SelectionModel::Role::LocalTransform, shape);
        }

        // Puts a new node of `type` after `after`, taking over what `after` fed; the caller records the undo step.
        NodeRef insertNodeAfter(SketchDocument& document, const NodeRef& after, const char* type, const std::string& name) {
            jitnodes::Network& network = document.networkOf(after.scope);
            const std::vector<NodeId> consumers = network.consumersOf(after.id);   // a copy: the wiring changes below
            const NodeId made = network.addNode(type, network.uniqueName(name));
            for (const NodeId consumer : consumers) {
                for (int input = 0; input < int(network.node(consumer).inputCount()); ++input) {
                    if (network.inputOf(consumer, input) == after.id) {
                        network.connect(made, consumer, input);
                    }
                }
            }
            network.connect(after.id, made, 0);
            return NodeRef(after.scope, made);
        }

        NodeRef insertLocalTransform(SketchDocument& document, const NodeRef& shape) {
            return insertNodeAfter(document, shape, "transform", "localXform");
        }

        NodeRef findLook(SketchDocument& document, const NodeRef& shape, SketchDocument::LookKind kind) {
            SelectionModel probe(&document);
            return probe.find(kind == SketchDocument::LookKind::Fill ? SelectionModel::Role::Fill : SelectionModel::Role::Stroke, shape);
        }

        void checkLookNames(SketchDocument::LookKind kind, const std::map<std::string, float>& values) {
            const char* extra = kind == SketchDocument::LookKind::Fill ? "evenOdd" : "width";
            for (const auto& entry : values) {
                const std::string& name = entry.first;
                if (name != "visible" && name != "r" && name != "g" && name != "b" && name != "alpha" && name != "opacity" && name != extra) {
                    throw std::runtime_error("unknown " + std::string(kind == SketchDocument::LookKind::Fill ? "fill" : "stroke") + " parameter '" + name + "'");
                }
            }
        }

        // A fill goes after the shape's local transform (or the generator), a stroke after the fill (or what a fill would follow).
        NodeRef insertLook(SketchDocument& document, const NodeRef& shape, SketchDocument::LookKind kind) {
            NodeRef anchor = findLocalTransform(document, shape);
            if (!anchor.valid()) {
                anchor = shape;
            }
            if (kind == SketchDocument::LookKind::Stroke) {
                if (const NodeRef fill = findLook(document, shape, SketchDocument::LookKind::Fill); fill.valid()) {
                    anchor = fill;
                }
            }
            return kind == SketchDocument::LookKind::Fill ? insertNodeAfter(document, anchor, fillNodeType, "fill")
                                                          : insertNodeAfter(document, anchor, strokeNodeType, "stroke");
        }

    }

    std::map<std::string, float> SketchDocument::objectTransform(const NodeRef& object) {
        return transformValues(node(objectXformRef(*this, object)));
    }

    void SketchDocument::setObjectTransform(const NodeRef& object, const std::map<std::string, float>& values) {
        checkTransformNames(values);
        setGraphFloats(objectXformRef(*this, object), values, "Transform Object");
    }

    NodeRef SketchDocument::localTransformOf(const NodeRef& from) {
        return findLocalTransform(*this, shapeGeneratorOf(*this, from));
    }

    NodeRef SketchDocument::addLocalTransform(const NodeRef& from) {
        const NodeRef shape = shapeGeneratorOf(*this, from);
        if (const NodeRef existing = findLocalTransform(*this, shape); existing.valid()) {
            return existing;
        }
        const jitnodes::GraphSnapshot before = snapshotGraph();
        const NodeRef made = insertLocalTransform(*this, shape);
        commitGraphEdit(before, "Add Local Transform");
        return made;
    }

    std::map<std::string, float> SketchDocument::shapeTransform(const NodeRef& from) {
        const NodeRef local = localTransformOf(from);
        if (local.valid()) {
            return transformValues(node(local));
        }
        return {{"tx", 0.0f}, {"ty", 0.0f}, {"rot", 0.0f}, {"sx", 1.0f}, {"sy", 1.0f}, {"skew", 0.0f}, {"px", 0.0f}, {"py", 0.0f}};
    }

    std::optional<std::map<std::string, float>> SketchDocument::shapeLook(const NodeRef& from, LookKind kind) {
        const NodeRef look = findLook(*this, shapeGeneratorOf(*this, from), kind);
        if (!look.valid()) {
            return std::nullopt;
        }
        const jitnodes::Node& lookNode = node(look);
        std::map<std::string, float> values;
        for (const char* name : {"visible", "r", "g", "b", "alpha", "opacity", kind == LookKind::Fill ? "evenOdd" : "width"}) {
            values[name] = lookNode.getFloat(name);
        }
        return values;
    }

    void SketchDocument::setShapeLook(const NodeRef& from, LookKind kind, const std::map<std::string, float>& values) {
        checkLookNames(kind, values);
        const NodeRef shape = shapeGeneratorOf(*this, from);
        const jitnodes::GraphSnapshot before = snapshotGraph();
        NodeRef look = findLook(*this, shape, kind);
        if (!look.valid()) {
            look = insertLook(*this, shape, kind);
        }
        for (const auto& [name, value] : values) {
            node(look).setFloat(name, value);
        }
        commitGraphEdit(before, kind == LookKind::Fill ? "Change Fill" : "Change Stroke");
    }

    void SketchDocument::setShapeTransform(const NodeRef& from, const std::map<std::string, float>& values) {
        checkTransformNames(values);
        const NodeRef shape = shapeGeneratorOf(*this, from);
        const jitnodes::GraphSnapshot before = snapshotGraph();
        NodeRef local = findLocalTransform(*this, shape);
        if (!local.valid()) {
            local = insertLocalTransform(*this, shape);
        }
        for (const auto& [name, value] : values) {
            node(local).setFloat(name, value);
        }
        commitGraphEdit(before, "Transform Shape");
    }

    namespace {

        const char* const pathGeometryType = "geometry";
        const char* const pathGeometryParam = "geometry";

        void checkPathPrimitive(std::size_t count, bool closed, bool cubic) {
            jitnodes::Primitive prim;
            prim.count = std::uint32_t(count);
            prim.closed = closed;
            prim.kind = cubic ? jitnodes::PrimKind::cubic : jitnodes::PrimKind::polyline;
            if (cubic && jitnodes::cubicSegments(prim) == 0) {
                throw std::runtime_error(closed ? "a closed curve needs 3n points (anchor, out handle, in handle per segment)"
                                                : "an open curve needs 3n+1 points (anchor, out handle, in handle, anchor ...)");
            }
            if (!cubic && count < 2) {
                throw std::runtime_error("a path needs at least two points");
            }
        }

        jitnodes::GeometryHandle makePathGeometry(const std::vector<SketchDocument::PathPoint>& points, std::vector<jitnodes::Primitive> prims, const std::string& key) {
            auto geometry = std::make_shared<jitnodes::Geometry>();
            geometry->resizePoints(points.size());
            float* x = geometry->x();
            float* y = geometry->y();
            for (std::size_t i = 0; i < points.size(); ++i) {
                x[i] = points[i].x;
                y[i] = points[i].y;
            }
            for (const jitnodes::Primitive& prim : prims) {
                geometry->addShapePrimitive(prim, key);
            }
            return geometry;
        }

        const jitnodes::Geometry& pathGeometryOf(const jitnodes::Node& node) {
            if (node.typeName() != pathGeometryType) {
                throw std::runtime_error("not a path node (a 'geometry' node holds the points)");
            }
            const jitnodes::GeometryHandle& handle = node.geometryParam(pathGeometryParam);
            if (!handle) {
                throw std::runtime_error("the path node holds no geometry");
            }
            return *handle;
        }

        // The one primitive a closed/open or point-count change applies to.
        const jitnodes::Primitive& onlyPrimitive(const jitnodes::Geometry& geometry) {
            if (geometry.primitiveCount() != 1) {
                throw std::runtime_error("this path has several subpaths; only a change that keeps its point count is possible");
            }
            return geometry.primitives().front();
        }

    }

    NodeRef SketchDocument::addPath(const NodeRef& object, const std::vector<PathPoint>& points, bool closed, bool cubic, const PathLook& look, const std::string& name) {
        ObjectNode* owner = hasNode(object) ? dynamic_cast<ObjectNode*>(&node(object)) : nullptr;
        if (owner == nullptr) {
            throw std::runtime_error("a path goes into a layer or a group");
        }
        checkPathPrimitive(points.size(), closed, cubic);
        const jitnodes::GraphSnapshot before = snapshotGraph();
        Network& net = owner->network();
        const NodeId pathId = net.addNode(pathGeometryType, net.uniqueName(name.empty() ? "path" : name));
        jitnodes::Primitive prim;
        prim.count = std::uint32_t(points.size());
        prim.closed = closed;
        prim.kind = cubic ? jitnodes::PrimKind::cubic : jitnodes::PrimKind::polyline;
        net.node(pathId).setGeometryParam(pathGeometryParam, makePathGeometry(points, {prim}, net.keyPrefix() + net.node(pathId).uidText()));

        NodeId last = pathId;
        const auto appearance = [&](const char* type, const newui::Color& color) {
            const NodeId id = net.addNode(type, net.uniqueName(type == std::string(fillNodeType) ? "fill" : "stroke"));
            jitnodes::Node& appearanceNode = net.node(id);
            appearanceNode.setFloat("r", color.r);
            appearanceNode.setFloat("g", color.g);
            appearanceNode.setFloat("b", color.b);
            appearanceNode.setFloat("alpha", color.a);
            net.connect(last, id);
            last = id;
            return id;
        };
        if (look.hasFill) {
            appearance(fillNodeType, look.fill);
        }
        if (look.hasStroke) {
            const NodeId stroke = appearance(strokeNodeType, look.stroke);
            net.node(stroke).setFloat("width", look.strokeWidth);
        }
        std::vector<NodeId> order = ObjectTree::mergeInputs(net, owner->mergeId());
        order.push_back(last);
        ObjectTree::setMergeInputs(net, owner->mergeId(), order);
        commitGraphEdit(before, "Add Path");
        return NodeRef(scopeOfSubnet(object), last);
    }

    NodeRef SketchDocument::pathNodeOf(const NodeRef& start) const {
        const jitnodes::Network* net = findNetwork(start.scope);
        if (net == nullptr) {
            return kNoNode;
        }
        NodeId id = start.id;
        for (std::size_t steps = 0; steps <= net->nodeCount() && net->hasNode(id); ++steps) {
            if (net->node(id).typeName() == pathGeometryType) {
                return NodeRef(start.scope, id);
            }
            if (net->node(id).inputCount() == 0) {
                break;
            }
            id = net->inputOf(id, 0);
        }
        return kNoNode;
    }

    std::vector<SketchDocument::PathPoint> SketchDocument::pathPoints(const NodeRef& pathNode) const {
        const jitnodes::Geometry& geometry = pathGeometryOf(node(pathNode));
        std::vector<PathPoint> points(geometry.pointCount());
        if (!points.empty()) {
            const float* x = geometry.x();
            const float* y = geometry.y();
            for (std::size_t i = 0; i < points.size(); ++i) {
                points[i].x = x[i];
                points[i].y = y[i];
            }
        }
        return points;
    }

    void SketchDocument::setPathPoints(const NodeRef& pathNode, const std::vector<PathPoint>& points) {
        const jitnodes::Geometry& geometry = pathGeometryOf(node(pathNode));
        if (points.size() == geometry.pointCount()) {
            auto moved = std::make_shared<jitnodes::Geometry>(geometry);
            float* x = moved->x();
            float* y = moved->y();
            for (std::size_t i = 0; i < points.size(); ++i) {
                x[i] = points[i].x;
                y[i] = points[i].y;
            }
            setGraphGeometry(pathNode, pathGeometryParam, moved);
            return;
        }
        jitnodes::Primitive prim = onlyPrimitive(geometry);
        checkPathPrimitive(points.size(), prim.closed, prim.kind == jitnodes::PrimKind::cubic);
        prim.start = 0;
        prim.count = std::uint32_t(points.size());
        setGraphGeometry(pathNode, pathGeometryParam, makePathGeometry(points, {prim}, geometry.shapeKey(0)));
    }

    void SketchDocument::setPathClosed(const NodeRef& pathNode, bool closed) {
        const jitnodes::Geometry& geometry = pathGeometryOf(node(pathNode));
        jitnodes::Primitive prim = onlyPrimitive(geometry);
        if (prim.closed == closed) {
            return;
        }
        // Closing a cubic path drops its last anchor into the first one, and opening one adds it back: the layouts differ by one point.
        std::vector<PathPoint> points = pathPoints(pathNode);
        const bool cubic = prim.kind == jitnodes::PrimKind::cubic;
        if (cubic && closed && points.size() % 3 == 1) {
            points.pop_back();
        } else if (cubic && !closed && points.size() % 3 == 0) {
            points.push_back(points.front());
        }
        checkPathPrimitive(points.size(), closed, cubic);
        prim.count = std::uint32_t(points.size());
        prim.closed = closed;
        setGraphGeometry(pathNode, pathGeometryParam, makePathGeometry(points, {prim}, geometry.shapeKey(0)));
    }

    bool SketchDocument::pathClosed(const NodeRef& pathNode) const {
        return onlyPrimitive(pathGeometryOf(node(pathNode))).closed;
    }

    bool SketchDocument::pathCubic(const NodeRef& pathNode) const {
        return onlyPrimitive(pathGeometryOf(node(pathNode))).kind == jitnodes::PrimKind::cubic;
    }

    jitnodes::GeometryHandle SketchDocument::artwork() {
        NodeId end = network().displayNode();
        if (end == jitnodes::invalidNodeId || !network().hasNode(end)) {
            end = ObjectTree::rootRenderOf(network());
        }
        if (end == jitnodes::invalidNodeId) {
            end = ObjectTree::rootMergeOf(network());
        }
        if (end == jitnodes::invalidNodeId) {
            return nullptr;
        }
        try {
            return network().cook(end);
        } catch (const std::exception&) {
            return nullptr;
        }
    }

}
