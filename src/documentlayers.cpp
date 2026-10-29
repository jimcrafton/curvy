#include "curvy/sketchdocument.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>

// Layers: a tree of named buckets (see Layer in sketchdocument.h). The document keeps its flat object list sorted by layer after every
// change (syncLayers, called from changed()), so painting, hit testing and the file formats need not know about the tree beyond each
// object's layer id. Each tree edit is one undo step made from a snapshot of the tree.

namespace curvy {

    const Layer* SketchDocument::findLayer(std::size_t id) const {
        for (const Layer& layer : layers_) {
            if (layer.id == id) {
                return &layer;
            }
        }
        return nullptr;
    }

    std::vector<std::size_t> SketchDocument::layerChildren(std::size_t parent) const {
        std::vector<std::size_t> out;
        for (const Layer& layer : layers_) {
            if (layer.parent == parent) {
                out.push_back(layer.id);
            }
        }
        return out;
    }

    std::size_t SketchDocument::layerDepth(std::size_t id) const {
        std::size_t depth = 0;
        const Layer* layer = findLayer(id);
        while (layer != nullptr && layer->parent != 0 && depth < layers_.size()) {
            layer = findLayer(layer->parent);
            ++depth;
        }
        return depth;
    }

    bool SketchDocument::layerContains(std::size_t ancestor, std::size_t id) const {
        const Layer* layer = findLayer(id);
        for (std::size_t guard = 0; layer != nullptr && guard <= layers_.size(); ++guard) {
            if (layer->id == ancestor) {
                return true;
            }
            layer = layer->parent != 0 ? findLayer(layer->parent) : nullptr;
        }
        return false;
    }

    bool SketchDocument::layerShown(std::size_t id) const {
        const Layer* layer = findLayer(id);
        for (std::size_t guard = 0; layer != nullptr && guard <= layers_.size(); ++guard) {
            if (!layer->visible) {
                return false;
            }
            layer = layer->parent != 0 ? findLayer(layer->parent) : nullptr;
        }
        return true;
    }

    bool SketchDocument::layerEditable(std::size_t id) const {
        const Layer* layer = findLayer(id);
        for (std::size_t guard = 0; layer != nullptr && guard <= layers_.size(); ++guard) {
            if (layer->locked) {
                return false;
            }
            layer = layer->parent != 0 ? findLayer(layer->parent) : nullptr;
        }
        return true;
    }

    std::size_t SketchDocument::layerObjectCount(std::size_t id, bool withSublayers) const {
        std::size_t count = 0;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (withSublayers ? layerContains(id, layerOf(*object)) : layerOf(*object) == id) {
                ++count;
            }
        }
        return count;
    }

    std::vector<std::size_t> SketchDocument::objectsInLayer(std::size_t id) const {
        std::vector<std::size_t> out;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (layerContains(id, layerOf(*object))) {
                out.push_back(object->id());
            }
        }
        return out;
    }

    void SketchDocument::setActiveLayer(std::size_t id) {
        if (findLayer(id) != nullptr && id != activeLayer_) {
            activeLayer_ = id;
            onChanged(*this);
        }
    }

    std::size_t SketchDocument::selectionLayer() const {
        if (selection_.empty()) {
            return 0;
        }
        const SketchObject* object = findById(selection_.front());
        return object != nullptr ? layerOf(*object) : 0;
    }

    void SketchDocument::setLayerExpanded(std::size_t id, bool expanded) {
        for (Layer& layer : layers_) {
            if (layer.id == id && layer.expanded != expanded) {
                layer.expanded = expanded;
                onChanged(*this);
            }
        }
    }

    std::size_t SketchDocument::nextLayerId() const {
        std::size_t next = 1;
        for (const Layer& layer : layers_) {
            next = std::max(next, layer.id + 1);
        }
        return next;
    }

    // ---- order ----

    // Post-order of the tree: a layer's sublayers (back first) and then the layer itself, so its own objects lie above its sublayers.
    std::vector<int> SketchDocument::layerRanks() const {
        std::size_t top = 0;
        for (const Layer& layer : layers_) {
            top = std::max(top, layer.id);
        }
        std::vector<int> rank(top + 1, -1);
        int counter = 0;
        std::vector<std::pair<std::size_t, std::size_t>> stack;   // (layer, next child index)
        std::vector<std::vector<std::size_t>> kids;
        auto enter = [&](std::size_t id) {
            stack.push_back({id, 0});
            kids.push_back(layerChildren(id));
        };
        for (std::size_t root : layerChildren(0)) {
            enter(root);
            while (!stack.empty()) {
                auto& [id, next] = stack.back();
                if (next < kids.back().size()) {
                    std::size_t child = kids.back()[next++];
                    if (rank[child] < 0 && stack.size() <= layers_.size()) {
                        enter(child);
                    }
                } else {
                    rank[id] = counter++;
                    stack.pop_back();
                    kids.pop_back();
                }
            }
        }
        return rank;
    }

    void SketchDocument::syncLayers() {
        std::vector<int> rank = layerRanks();
        std::vector<char> hidden(rank.size(), 0), locked(rank.size(), 0);
        for (const Layer& layer : layers_) {
            hidden[layer.id] = layerShown(layer.id) ? 0 : 1;
            locked[layer.id] = layerEditable(layer.id) ? 0 : 1;
        }
        bool sorted = true;
        int last = -1;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            std::size_t layer = layerOf(*object);
            if (layer >= rank.size() || rank[layer] < 0) {
                layer = activeLayer_;
                setLayerOf(*object, layer);
            }
            object->setLayerState(hidden[layer] != 0, locked[layer] != 0);
            if (rank[layer] < last) {
                sorted = false;
            }
            last = rank[layer];
        }
        if (!sorted) {
            std::stable_sort(objects_.begin(), objects_.end(), [this, &rank](const std::unique_ptr<SketchObject>& a, const std::unique_ptr<SketchObject>& b) {
                return rank[layerOf(*a)] < rank[layerOf(*b)];
            });
        }
    }

    std::vector<std::size_t> SketchDocument::layerSorted(const std::vector<std::size_t>& ids) const {
        std::vector<int> rank = layerRanks();
        std::unordered_map<std::size_t, int> of;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            std::size_t layer = layerOf(*object);
            of[object->id()] = layer < rank.size() && rank[layer] >= 0 ? rank[layer] : 0;
        }
        std::vector<std::size_t> out = ids;
        std::stable_sort(out.begin(), out.end(), [&of](std::size_t a, std::size_t b) { return of[a] < of[b]; });
        return out;
    }

    // A hidden or locked object cannot stay selected.
    void SketchDocument::dropUnselectable() {
        syncLayers();
        std::vector<std::size_t> keep;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object != nullptr && object->isVisible() && !object->isLocked()) {
                keep.push_back(id);
            }
        }
        if (keep.size() != selection_.size()) {
            setSelection(keep);
        }
    }

    void SketchDocument::resetLayers() {
        layers_ = {Layer{1, 0, "Layer 1", true, false, true}};
        activeLayer_ = 1;
    }

    void SketchDocument::setLayers(std::vector<Layer> layers) {
        layers_ = std::move(layers);
        if (layers_.empty()) {
            resetLayers();
        }
        std::vector<std::size_t> top = layerChildren(0);
        activeLayer_ = top.empty() ? layers_.back().id : top.back();
        syncLayers();
        updateAllViews();
        onChanged(*this);
    }

    // ---- undo ----

    SketchDocument::LayerState SketchDocument::snapshotLayers(bool withObjects) const {
        LayerState state;
        state.layers = layers_;
        state.activeLayer = activeLayer_;
        if (withObjects) {
            for (const std::unique_ptr<SketchObject>& object : objects_) {
                state.objectLayers.push_back({object->id(), layerOf(*object)});
                state.order.push_back(object->id());
            }
        }
        return state;
    }

    void SketchDocument::applyLayers(const LayerState& state) {
        layers_ = state.layers;
        activeLayer_ = state.activeLayer;
        if (!state.objectLayers.empty()) {
            std::unordered_map<std::size_t, SketchObject*> byId;
            for (const std::unique_ptr<SketchObject>& object : objects_) {
                byId[object->id()] = object.get();
            }
            for (const auto& [id, layer] : state.objectLayers) {
                auto it = byId.find(id);
                if (it != byId.end()) {
                    setLayerOf(*it->second, layer);
                }
            }
        }
        dropUnselectable();
        if (!state.order.empty()) {
            reorder(state.order);
        } else {
            changed();
        }
    }

    void SketchDocument::pushLayerStep(const std::string& description, const LayerState& before, const LayerState& after) {
        undoStack_.push({description, [this, after]() { applyLayers(after); }, [this, before]() { applyLayers(before); }});
    }

    // ---- edits ----

    std::size_t SketchDocument::addLayer(std::size_t parent, const std::string& name) {
        if (parent != 0 && findLayer(parent) == nullptr) {
            return 0;
        }
        LayerState before = snapshotLayers(false), after = before;
        Layer layer;
        layer.id = nextLayerId();
        layer.parent = parent;
        layer.name = name.empty() ? "Layer " + std::to_string(layer.id) : name;
        after.layers.push_back(layer);
        for (Layer& p : after.layers) {
            if (p.id == parent) {
                p.expanded = true;
            }
        }
        after.activeLayer = layer.id;
        pushLayerStep(parent != 0 ? "New Sublayer" : "New Layer", before, after);
        return layer.id;
    }

    bool SketchDocument::renameLayer(std::size_t id, const std::string& name) {
        const Layer* layer = findLayer(id);
        if (layer == nullptr || name.empty() || layer->name == name) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        for (Layer& l : after.layers) {
            if (l.id == id) {
                l.name = name;
            }
        }
        pushLayerStep("Rename Layer", before, after);
        return true;
    }

    bool SketchDocument::setLayerVisible(std::size_t id, bool visible) {
        const Layer* layer = findLayer(id);
        if (layer == nullptr || layer->visible == visible) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        for (Layer& l : after.layers) {
            if (l.id == id) {
                l.visible = visible;
            }
        }
        pushLayerStep(visible ? "Show Layer" : "Hide Layer", before, after);
        return true;
    }

    bool SketchDocument::setLayerLocked(std::size_t id, bool locked) {
        const Layer* layer = findLayer(id);
        if (layer == nullptr || layer->locked == locked) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        for (Layer& l : after.layers) {
            if (l.id == id) {
                l.locked = locked;
            }
        }
        pushLayerStep(locked ? "Lock Layer" : "Unlock Layer", before, after);
        return true;
    }

    namespace {
        struct DeletedObject {
            std::size_t id = 0;
            std::size_t index = 0;
            std::unique_ptr<SketchObject> object;
        };
    }

    bool SketchDocument::deleteLayer(std::size_t id) {
        if (findLayer(id) == nullptr) {
            return false;
        }
        std::vector<Layer> remaining;
        for (const Layer& layer : layers_) {
            if (!layerContains(id, layer.id)) {
                remaining.push_back(layer);
            }
        }
        if (remaining.empty()) {
            return false;   // the last layer stays
        }
        std::vector<Layer> allBefore = layers_;
        std::size_t activeBefore = activeLayer_;
        std::size_t activeAfter = activeLayer_;
        if (layerContains(id, activeLayer_)) {
            activeAfter = remaining.back().id;
            for (const Layer& layer : remaining) {
                if (layer.parent == 0) {
                    activeAfter = layer.id;
                }
            }
        }
        std::vector<std::shared_ptr<DeletedObject>> slots;
        for (std::size_t objectId : objectsInLayer(id)) {
            auto slot = std::make_shared<DeletedObject>();
            slot->id = objectId;
            slots.push_back(slot);
        }
        undoStack_.push({"Delete Layer",
            [this, slots, remaining, activeAfter]() {
                for (std::size_t i = objects_.size(); i > 0; --i) {   // highest position first, so each recorded index is the original
                    std::size_t objectId = objects_[i - 1]->id();
                    for (const std::shared_ptr<DeletedObject>& slot : slots) {
                        if (slot->id == objectId) {
                            slot->object = detach(objectId, &slot->index);
                            break;
                        }
                    }
                }
                layers_ = remaining;
                activeLayer_ = activeAfter;
                dropUnselectable();
                changed();
            },
            [this, slots, allBefore, activeBefore]() {
                layers_ = allBefore;
                activeLayer_ = activeBefore;
                std::vector<std::shared_ptr<DeletedObject>> ordered = slots;
                std::sort(ordered.begin(), ordered.end(), [](const std::shared_ptr<DeletedObject>& a, const std::shared_ptr<DeletedObject>& b) {
                    return a->index < b->index;
                });
                for (const std::shared_ptr<DeletedObject>& slot : ordered) {
                    if (slot->object) {
                        attach(std::move(slot->object), slot->index);
                    }
                }
                changed();
            }});
        return true;
    }

    bool SketchDocument::moveLayer(std::size_t id, std::size_t parent, std::size_t position) {
        const Layer* found = findLayer(id);
        if (found == nullptr || (parent != 0 && (findLayer(parent) == nullptr || layerContains(id, parent)))) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        Layer moved = *found;
        moved.parent = parent;
        after.layers.erase(std::remove_if(after.layers.begin(), after.layers.end(), [id](const Layer& l) { return l.id == id; }), after.layers.end());
        std::vector<std::size_t> siblings;
        for (const Layer& l : after.layers) {
            if (l.parent == parent) {
                siblings.push_back(l.id);
            }
        }
        std::size_t at = after.layers.size();   // after everything: the front of its siblings
        if (position < siblings.size()) {
            for (std::size_t i = 0; i < after.layers.size(); ++i) {
                if (after.layers[i].id == siblings[position]) {
                    at = i;
                    break;
                }
            }
        }
        after.layers.insert(after.layers.begin() + std::ptrdiff_t(at), moved);
        for (Layer& l : after.layers) {
            if (l.id == parent) {
                l.expanded = true;
            }
        }
        bool same = after.layers.size() == before.layers.size();
        for (std::size_t i = 0; same && i < after.layers.size(); ++i) {
            same = after.layers[i].id == before.layers[i].id && after.layers[i].parent == before.layers[i].parent;
        }
        if (same) {
            return false;
        }
        pushLayerStep("Move Layer", before, after);
        return true;
    }

    bool SketchDocument::moveSelectionToLayer(std::size_t id) {
        if (findLayer(id) == nullptr || selection_.empty()) {
            return false;
        }
        bool any = false;
        for (std::size_t objectId : selection_) {
            const SketchObject* object = findById(objectId);
            any = any || (object != nullptr && layerOf(*object) != id);
        }
        if (!any) {
            return false;
        }
        LayerState before = snapshotLayers(true);
        std::vector<std::size_t> others, moved;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            (isSelected(object->id()) ? moved : others).push_back(object->id());
        }
        for (std::size_t objectId : moved) {
            setLayerOf(*findById(objectId), id);
        }
        others.insert(others.end(), moved.begin(), moved.end());   // the moved ones land on top of their new layer
        std::vector<std::size_t> selected = selection_;
        reorder(others);
        LayerState after = snapshotLayers(true);
        undoStack_.push({"Move to Layer",
            [this, after]() { applyLayers(after); },
            [this, before]() { applyLayers(before); }});
        setSelection(selected);
        dropUnselectable();
        return true;
    }

    void SketchDocument::selectLayerObjects(std::size_t id) {
        std::vector<std::size_t> ids;
        for (std::size_t objectId : objectsInLayer(id)) {
            const SketchObject* object = findById(objectId);
            if (object != nullptr && object->isVisible() && !object->isLocked()) {
                ids.push_back(objectId);
            }
        }
        setSelection(ids);
    }

    // ---- colour, duplicate, merge, flatten, visibility of many layers ----

    bool SketchDocument::setLayerColor(std::size_t id, std::uint32_t rgb) {
        const Layer* layer = findLayer(id);
        rgb &= 0xFFFFFFu;
        if (layer == nullptr || layer->color == rgb) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        for (Layer& l : after.layers) {
            if (l.id == id) {
                l.color = rgb;
            }
        }
        pushLayerStep("Layer Color", before, after);
        return true;
    }

    std::size_t SketchDocument::duplicateLayer(std::size_t id) {
        if (findLayer(id) == nullptr) {
            return 0;
        }
        // The layer and everything below it, in the order the list keeps them.
        std::vector<Layer> originals;
        std::size_t lastIndex = 0;
        for (std::size_t i = 0; i < layers_.size(); ++i) {
            if (layerContains(id, layers_[i].id)) {
                originals.push_back(layers_[i]);
                lastIndex = i;
            }
        }
        std::map<std::size_t, std::size_t> fresh;   // old layer id -> its copy
        std::size_t next = nextLayerId();
        for (const Layer& layer : originals) {
            fresh[layer.id] = next++;
        }
        std::vector<Layer> copies = originals;
        for (Layer& layer : copies) {
            const bool top = layer.id == id;
            const std::size_t parent = layer.parent;
            layer.id = fresh[layer.id];
            layer.parent = top ? parent : fresh[parent];
            if (top) {
                layer.name += " copy";
            }
        }
        std::map<std::size_t, std::size_t> groupOf;   // the copies get groups of their own
        std::vector<std::shared_ptr<DeletedObject>> slots;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            auto found = fresh.find(layerOf(*object));
            if (found == fresh.end()) {
                continue;
            }
            auto slot = std::make_shared<DeletedObject>();
            slot->object.reset(object->clone());
            slot->object->setId(nextId_++);
            setLayerOf(*slot->object, found->second);
            std::vector<std::size_t> groups = groupsOf(*slot->object);
            for (std::size_t& g : groups) {
                auto it = groupOf.find(g);
                if (it == groupOf.end()) {
                    it = groupOf.emplace(g, nextGroupId_++).first;
                    GroupProps props = groupProps(g);
                    if (!props.isDefault()) {
                        groupProps_[it->second] = props;
                    }
                }
                g = it->second;
            }
            setGroupsOf(*slot->object, groups);
            slot->id = slot->object->id();
            slots.push_back(slot);
        }
        std::vector<Layer> before = layers_, after = layers_;
        after.insert(after.begin() + std::ptrdiff_t(lastIndex + 1), copies.begin(), copies.end());
        const std::size_t activeBefore = activeLayer_, activeAfter = fresh[id];
        undoStack_.push({"Duplicate Layer",
            [this, slots, after, activeAfter]() {
                layers_ = after;
                activeLayer_ = activeAfter;
                for (const std::shared_ptr<DeletedObject>& slot : slots) {
                    if (slot->object) {
                        attach(std::move(slot->object), objects_.size());
                    }
                }
                changed();
            },
            [this, slots, before, activeBefore]() {
                for (const std::shared_ptr<DeletedObject>& slot : slots) {
                    slot->object = detach(slot->id, &slot->index);
                }
                layers_ = before;
                activeLayer_ = activeBefore;
                dropUnselectable();
                changed();
            }});
        return fresh[id];
    }

    bool SketchDocument::moveLayerStep(std::size_t id, bool front) {
        const Layer* layer = findLayer(id);
        if (layer == nullptr) {
            return false;
        }
        std::vector<std::size_t> siblings = layerChildren(layer->parent);
        const std::size_t at = std::size_t(std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
        if (front ? at + 1 >= siblings.size() : at == 0) {
            return false;
        }
        return moveLayer(id, layer->parent, front ? at + 1 : at - 1);   // `position` counts the siblings with the layer taken out: it goes in before that one
    }

    bool SketchDocument::showAllLayers() {
        LayerState before = snapshotLayers(false), after = before;
        bool any = false;
        for (Layer& l : after.layers) {
            any = any || !l.visible;
            l.visible = true;
        }
        if (!any) {
            return false;
        }
        pushLayerStep("Show All Layers", before, after);
        return true;
    }

    bool SketchDocument::unlockAllLayers() {
        LayerState before = snapshotLayers(false), after = before;
        bool any = false;
        for (Layer& l : after.layers) {
            any = any || l.locked;
            l.locked = false;
        }
        if (!any) {
            return false;
        }
        pushLayerStep("Unlock All Layers", before, after);
        return true;
    }

    namespace {
        std::size_t topLevelOf(const std::vector<Layer>& layers, std::size_t id) {
            for (std::size_t guard = 0; guard <= layers.size(); ++guard) {
                const Layer* found = nullptr;
                for (const Layer& l : layers) {
                    if (l.id == id) {
                        found = &l;
                    }
                }
                if (found == nullptr || found->parent == 0) {
                    return id;
                }
                id = found->parent;
            }
            return id;
        }
    }

    bool SketchDocument::hideOtherLayers(std::size_t id) {
        if (findLayer(id) == nullptr) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        const std::size_t keep = topLevelOf(after.layers, id);
        bool any = false;
        for (Layer& l : after.layers) {
            if (l.parent == 0 && l.id != keep && l.visible) {
                l.visible = false;
                any = true;
            }
        }
        if (!any) {
            return false;
        }
        pushLayerStep("Hide Other Layers", before, after);
        return true;
    }

    bool SketchDocument::lockOtherLayers(std::size_t id) {
        if (findLayer(id) == nullptr) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        const std::size_t keep = topLevelOf(after.layers, id);
        bool any = false;
        for (Layer& l : after.layers) {
            if (l.parent == 0 && l.id != keep && !l.locked) {
                l.locked = true;
                any = true;
            }
        }
        if (!any) {
            return false;
        }
        pushLayerStep("Lock Other Layers", before, after);
        return true;
    }

    bool SketchDocument::mergeLayerDown(std::size_t id) {
        const Layer* found = findLayer(id);
        if (found == nullptr) {
            return false;
        }
        std::vector<std::size_t> siblings = layerChildren(found->parent);
        const auto at = std::find(siblings.begin(), siblings.end(), id);
        if (at == siblings.begin()) {
            return false;
        }
        const std::size_t target = *(at - 1);
        LayerState before = snapshotLayers(true), after = before;
        for (auto& entry : after.objectLayers) {
            if (entry.second == id) {
                entry.second = target;
            }
        }
        for (Layer& l : after.layers) {
            if (l.parent == id) {
                l.parent = target;
            }
        }
        after.layers.erase(std::remove_if(after.layers.begin(), after.layers.end(), [id](const Layer& l) { return l.id == id; }), after.layers.end());
        if (after.activeLayer == id) {
            after.activeLayer = target;
        }
        pushLayerStep("Merge Layers", before, after);
        return true;
    }

    bool SketchDocument::flattenArtwork() {
        if (layers_.size() < 2) {
            return false;
        }
        std::vector<std::size_t> top = layerChildren(0);
        const std::size_t target = top.front();
        LayerState before = snapshotLayers(true), after = before;
        for (auto& entry : after.objectLayers) {
            entry.second = target;
        }
        Layer kept = *findLayer(target);
        kept.parent = 0;
        kept.visible = true;
        kept.locked = false;
        after.layers.assign(1, kept);
        after.activeLayer = target;
        pushLayerStep("Flatten Artwork", before, after);
        return true;
    }

    void SketchDocument::setAllLayersExpanded(bool expanded) {
        bool any = false;
        for (Layer& layer : layers_) {
            any = any || layer.expanded != expanded;
            layer.expanded = expanded;
        }
        if (any) {
            onChanged(*this);
        }
    }

    // ---- objects in the tree ----

    std::vector<std::size_t> SketchDocument::groupMembers(std::size_t group) const {
        std::vector<std::size_t> out;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (inGroup(*object, group)) {
                out.push_back(object->id());
            }
        }
        return out;
    }

    bool SketchDocument::moveObjects(const std::vector<std::size_t>& ids, std::size_t layer, const std::vector<std::size_t>& relativeTo, bool above,
        const std::optional<std::vector<std::size_t>>& groups, std::size_t keepFrom) {
        std::set<std::size_t> moving(ids.begin(), ids.end());
        for (std::size_t target : relativeTo) {
            if (moving.count(target) != 0) {
                return false;   // dropped onto itself
            }
        }
        std::size_t destination = layer;
        if (!relativeTo.empty()) {
            const SketchObject* anchor = findById(relativeTo.front());
            if (anchor == nullptr) {
                return false;
            }
            destination = layerOf(*anchor);
        }
        if (findLayer(destination) == nullptr) {
            return false;
        }
        using Chains = std::vector<std::pair<std::size_t, std::vector<std::size_t>>>;
        Chains groupsBefore, groupsAfter;
        std::vector<std::size_t> order, moved, others;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            order.push_back(object->id());
            (moving.count(object->id()) != 0 ? moved : others).push_back(object->id());
            if (moving.count(object->id()) != 0) {
                groupsBefore.push_back({object->id(), groupsOf(*object)});
                std::vector<std::size_t> chain = groupsOf(*object);
                if (groups) {
                    chain.erase(chain.begin(), chain.begin() + std::ptrdiff_t(std::min(keepFrom, chain.size())));
                    chain.insert(chain.begin(), groups->begin(), groups->end());
                }
                groupsAfter.push_back({object->id(), chain});
            }
        }
        if (moved.empty()) {
            return false;
        }
        std::size_t at = others.size();   // the top
        if (!relativeTo.empty()) {
            std::size_t low = others.size(), high = 0;
            for (std::size_t i = 0; i < others.size(); ++i) {
                if (std::find(relativeTo.begin(), relativeTo.end(), others[i]) != relativeTo.end()) {
                    low = std::min(low, i);
                    high = std::max(high, i);
                }
            }
            if (low == others.size()) {
                return false;
            }
            at = above ? high + 1 : low;
        }
        std::vector<std::size_t> placed = others;
        placed.insert(placed.begin() + std::ptrdiff_t(at), moved.begin(), moved.end());

        LayerState before = snapshotLayers(true);
        bool layersChange = false;
        for (std::size_t id : moved) {
            layersChange = layersChange || layerOf(*findById(id)) != destination;
        }
        if (placed == order && !layersChange && groupsBefore == groupsAfter) {
            return false;
        }
        LayerState after = before;
        for (auto& entry : after.objectLayers) {
            if (moving.count(entry.first) != 0) {
                entry.second = destination;
            }
        }
        after.order = placed;
        auto applyGroups = [this](const Chains& chains) {
            for (const auto& [id, chain] : chains) {
                if (SketchObject* object = findById(id)) {
                    setGroupsOf(*object, chain);
                }
            }
        };
        std::vector<std::size_t> selected = selection_;
        undoStack_.push({"Move in Layers",
            [this, after, groupsAfter, applyGroups]() { applyGroups(groupsAfter); applyLayers(after); },
            [this, before, groupsBefore, applyGroups]() { applyGroups(groupsBefore); applyLayers(before); }});
        setSelection(selected);
        return true;
    }

    bool SketchDocument::setObjectsVisible(const std::vector<std::size_t>& ids, bool visible) {
        std::vector<FlagState> before, after;
        for (std::size_t id : ids) {
            if (const SketchObject* object = findById(id)) {
                if (object->ownVisible() != visible) {
                    before.push_back({id, object->ownVisible(), object->ownLocked()});
                    after.push_back({id, visible, object->ownLocked()});
                }
            }
        }
        return applyFlags(before, after, visible ? "Show" : "Hide");
    }

    bool SketchDocument::setObjectsLocked(const std::vector<std::size_t>& ids, bool locked) {
        std::vector<FlagState> before, after;
        for (std::size_t id : ids) {
            if (const SketchObject* object = findById(id)) {
                if (object->ownLocked() != locked) {
                    before.push_back({id, object->ownVisible(), object->ownLocked()});
                    after.push_back({id, object->ownVisible(), locked});
                }
            }
        }
        return applyFlags(before, after, locked ? "Lock" : "Unlock");
    }

    bool SketchDocument::renameObject(std::size_t id, const std::string& name) {
        const SketchObject* object = findById(id);
        if (object == nullptr || object->name() == name) {
            return false;
        }
        const std::string old = object->name();
        undoStack_.push({"Rename Object",
            [this, id, name]() { if (SketchObject* o = findById(id)) { o->setName(name); } changed(); },
            [this, id, old]() { if (SketchObject* o = findById(id)) { o->setName(old); } changed(); }});
        return true;
    }

}
