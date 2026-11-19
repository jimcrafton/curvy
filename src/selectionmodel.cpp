#include "curvy/selectionmodel.h"

#include "curvy/appearancenode.h"
#include "curvy/objectnode.h"

#include <algorithm>

namespace curvy {

    using newui::SyncReturn;

    namespace {
        constexpr const char* const mergeType = "merge";
        constexpr const char* const transformType = "transform";
    }

    SelectionModel::~SelectionModel() {
        disconnect();
    }

    // The view that owns a model can outlive the document, so the delegate is only touched while the document is still there.
    void SelectionModel::disconnect() {
        if (documentAlive()) {
            document_->onGraphChanged.remove(connection_);
        }
        connection_ = newui::Connection();
    }

    void SelectionModel::setDocument(GraphDocument* document) {
        disconnect();
        document_ = document;
        documentLife_ = document != nullptr ? document->lifetime() : std::weak_ptr<void>();
        if (document_ != nullptr) {
            connection_ = document_->onGraphChanged.add([this](GraphDocument&) {
                prune();
                return SyncReturn(SyncReturn::Ignored);
            });
        }
        clear();
    }

    bool SelectionModel::contains(const NodeRef& node) const {
        return std::find(items_.begin(), items_.end(), node) != items_.end();
    }

    bool SelectionModel::known(const NodeRef& node) const {
        return documentAlive() && node.valid() && document_->hasNode(node);
    }

    void SelectionModel::changed() {
        onChanged(*this);
    }

    bool SelectionModel::set(const NodeRef& node) {
        if (!known(node)) {
            return clear();
        }
        if (items_.size() == 1 && items_.front() == node && primary_ == node) {
            return false;
        }
        items_.assign(1, node);
        primary_ = node;
        changed();
        return true;
    }

    bool SelectionModel::selectShapeKey(const std::string& key, bool additive) {
        if (!documentAlive()) {
            return false;
        }
        const NodeRef node = document_->nodeForShapeKey(key);
        if (additive) {
            return node.valid() && (contains(node) ? setPrimary(node) : add(node));
        }
        return set(node);   // a key that names nothing (a click on empty canvas) clears
    }

    bool SelectionModel::selectShapeKeys(const std::vector<std::string>& keys, Combine how) {
        if (!documentAlive()) {
            return false;
        }
        std::vector<NodeRef> nodes;
        for (const std::string& key : keys) {
            const NodeRef node = document_->nodeForShapeKey(key);
            if (node.valid() && std::find(nodes.begin(), nodes.end(), node) == nodes.end()) {
                nodes.push_back(node);
            }
        }
        switch (how) {
            case Combine::Replace:
                return nodes.empty() ? clear() : replace(nodes);
            case Combine::Add:
                return addAll(nodes);
            case Combine::Subtract: {
                std::vector<NodeRef> kept;
                for (const NodeRef& item : items_) {
                    if (std::find(nodes.begin(), nodes.end(), item) == nodes.end()) {
                        kept.push_back(item);
                    }
                }
                return kept.size() == items_.size() ? false : (kept.empty() ? clear() : replace(kept));
            }
        }
        return false;
    }

    bool SelectionModel::stepUp() {
        if (!documentAlive() || !primary_.valid()) {
            return false;
        }
        const NodeRef up = enclosing(primary_);
        return up.valid() && set(up);
    }

    bool SelectionModel::isShapeKeySelected(const std::string& key) const {
        if (!documentAlive() || items_.empty()) {
            return false;
        }
        const std::vector<std::uint32_t> uids = GraphDocument::uidsOfShapeKey(key);
        for (const NodeRef& item : items_) {
            if (document_->hasNode(item) && std::find(uids.begin(), uids.end(), document_->node(item).uid()) != uids.end()) {
                return true;
            }
        }
        return false;
    }

    bool SelectionModel::clear() {
        if (items_.empty() && !primary_.valid()) {
            return false;
        }
        items_.clear();
        primary_ = NodeRef();
        changed();
        return true;
    }

    bool SelectionModel::add(const NodeRef& node) {
        if (!known(node)) {
            return false;
        }
        if (contains(node)) {
            return setPrimary(node);
        }
        items_.push_back(node);
        primary_ = node;
        changed();
        return true;
    }

    bool SelectionModel::addAll(const std::vector<NodeRef>& nodes) {
        bool any = false;
        for (const NodeRef& node : nodes) {
            if (known(node) && !contains(node)) {
                items_.push_back(node);
                primary_ = node;
                any = true;
            }
        }
        if (any) {
            changed();
        }
        return any;
    }

    bool SelectionModel::remove(const NodeRef& node) {
        const auto found = std::find(items_.begin(), items_.end(), node);
        if (found == items_.end()) {
            return false;
        }
        items_.erase(found);
        primary_ = items_.empty() ? NodeRef() : items_.back();
        changed();
        return true;
    }

    bool SelectionModel::toggle(const NodeRef& node) {
        return contains(node) ? remove(node) : add(node);
    }

    bool SelectionModel::setPrimary(const NodeRef& node) {
        if (!contains(node) || primary_ == node) {
            return false;
        }
        primary_ = node;
        changed();
        return true;
    }

    bool SelectionModel::replace(const std::vector<NodeRef>& nodes) {
        std::vector<NodeRef> next;
        for (const NodeRef& node : nodes) {
            if (known(node) && std::find(next.begin(), next.end(), node) == next.end()) {
                next.push_back(node);
            }
        }
        const NodeRef primary = next.empty() ? NodeRef() : next.back();
        if (next == items_ && primary == primary_) {
            return false;
        }
        items_ = std::move(next);
        primary_ = primary;
        changed();
        return true;
    }

    bool SelectionModel::prune() {
        const std::size_t before = items_.size();
        items_.erase(std::remove_if(items_.begin(), items_.end(), [this](const NodeRef& node) { return !known(node); }), items_.end());
        if (items_.size() == before) {
            return false;
        }
        if (!contains(primary_)) {
            primary_ = items_.empty() ? NodeRef() : items_.back();
        }
        changed();
        return true;
    }

    NodeRef SelectionModel::enclosing(const NodeRef& node) const {
        NodeRef current = node;
        while (current.scope != 0) {
            const NodeRef owner = document_->parentOfScope(current.scope);
            if (!known(owner)) {
                return kNoNode;
            }
            if (dynamic_cast<const ObjectNode*>(&document_->node(owner)) != nullptr) {
                return owner;
            }
            current = owner;
        }
        return kNoNode;
    }

    NodeRef SelectionModel::objectOf(const NodeRef& node) const {
        if (!known(node)) {
            return kNoNode;
        }
        if (dynamic_cast<const ObjectNode*>(&document_->node(node)) != nullptr) {
            return node;
        }
        return enclosing(node);
    }

    std::vector<NodeRef> SelectionModel::chainOf(const NodeRef& from) const {
        std::vector<NodeRef> chain;
        if (!known(from) || dynamic_cast<const ObjectNode*>(&document_->node(from)) != nullptr) {
            return chain;
        }
        const jitnodes::Network* network = document_->findNetwork(from.scope);
        if (network == nullptr) {
            return chain;
        }
        const auto stops = [network](jitnodes::NodeId id) {
            const jitnodes::Node& node = network->node(id);
            return node.typeName() == mergeType || dynamic_cast<const ObjectNode*>(&node) != nullptr;
        };
        std::vector<jitnodes::NodeId> ids{from.id};
        const std::size_t limit = network->nodeCount();
        jitnodes::NodeId current = from.id;
        for (std::size_t i = 0; i < limit && network->node(current).inputCount() > 0; ++i) {
            const jitnodes::NodeId input = network->inputOf(current, 0);
            if (input == jitnodes::invalidNodeId || stops(input) || std::find(ids.begin(), ids.end(), input) != ids.end()) {
                break;
            }
            ids.insert(ids.begin(), input);
            current = input;
        }
        current = from.id;
        for (std::size_t i = 0; i < limit; ++i) {
            jitnodes::NodeId next = jitnodes::invalidNodeId;
            for (jitnodes::NodeId consumer : network->consumersOf(current)) {
                if (!stops(consumer) && std::find(ids.begin(), ids.end(), consumer) == ids.end()) {
                    next = consumer;   // a branch that also feeds the merge is followed past it
                    break;
                }
            }
            if (next == jitnodes::invalidNodeId) {
                break;
            }
            ids.push_back(next);
            current = next;
        }
        for (jitnodes::NodeId id : ids) {
            chain.push_back(NodeRef(from.scope, id));
        }
        return chain;
    }

    NodeRef SelectionModel::find(Role role, const NodeRef& from) const {
        if (!known(from)) {
            return kNoNode;
        }
        switch (role) {
        case Role::Object:
            return objectOf(from);
        case Role::Layer: {
            for (NodeRef object = objectOf(from); known(object); object = enclosing(object)) {
                if (document_->node(object).hasTag(layerTag)) {
                    return object;
                }
            }
            return kNoNode;
        }
        case Role::ObjectXform: {
            const NodeRef object = objectOf(from);
            if (!known(object)) {
                return kNoNode;
            }
            const jitnodes::NodeId id = static_cast<const ObjectNode&>(document_->node(object)).xformId();
            const NodeRef xform(document_->scopeOfSubnet(object), id);
            return known(xform) ? xform : kNoNode;
        }
        default:
            break;
        }
        const std::vector<NodeRef> chain = chainOf(from);
        const auto typeOf = [this](const NodeRef& ref) -> const std::string& { return document_->node(ref).typeName(); };
        NodeRef found;
        for (const NodeRef& ref : chain) {
            const std::string& type = typeOf(ref);
            if ((role == Role::Fill && type == fillNodeType) || (role == Role::Stroke && type == strokeNodeType) ||
                (role == Role::LocalTransform && type == transformType)) {
                found = ref;   // the last one is nearest the merge
            }
        }
        if (role == Role::Shape && !chain.empty()) {
            const std::string& type = typeOf(chain.front());
            if (type != fillNodeType && type != strokeNodeType && type != transformType) {
                found = chain.front();
            }
        }
        return found;
    }

    int SelectionModel::count(Role role, const NodeRef& from) const {
        if (!known(from)) {
            return 0;
        }
        if (role == Role::Object || role == Role::Layer || role == Role::ObjectXform || role == Role::Shape) {
            return find(role, from).valid() ? 1 : 0;
        }
        int total = 0;
        for (const NodeRef& ref : chainOf(from)) {
            const std::string& type = document_->node(ref).typeName();
            if ((role == Role::Fill && type == fillNodeType) || (role == Role::Stroke && type == strokeNodeType) ||
                (role == Role::LocalTransform && type == transformType)) {
                ++total;
            }
        }
        return total;
    }

}
