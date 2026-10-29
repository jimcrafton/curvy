#include "curvy/layerspanel.h"

#include <newui/models.h>
#include <newui/utils.h>

#include <algorithm>
#include <limits>

namespace curvy {

    namespace {
        const char* const kColorNames[9] = {"Light Blue", "Red", "Green", "Blue", "Yellow", "Violet", "Orange", "Dark Green", "Magenta"};

        const char* typeName(SketchObjectType type) {
            switch (type) {
                case SketchObjectType::Line: return "Line";
                case SketchObjectType::Rect: return "Rectangle";
                case SketchObjectType::RoundRect: return "Rounded Rectangle";
                case SketchObjectType::Ellipse: return "Ellipse";
                case SketchObjectType::Polygon: return "Polygon";
                case SketchObjectType::Polyline: return "Polyline";
                case SketchObjectType::Path: return "Path";
                case SketchObjectType::Text: return "Text";
                case SketchObjectType::Symbol: return "Symbol";
                case SketchObjectType::Image: return "Image";
            }
            return "Object";
        }

        std::string displayName(const SketchObject& object) {
            return object.name().empty() ? std::string("<") + typeName(object.type()) + ">" : object.name();
        }
    }

    LayersPanel::LayersPanel(newui::RootView& root, SketchDocument& document) : root_(root), document_(document) {
        tree_ = dynamic_cast<LayerTree*>(root_.findView("layersTree"));
        name_ = dynamic_cast<newui::TextField*>(root_.findView("layerNameField"));
        color_ = dynamic_cast<newui::DropDownList*>(root_.findView("layerColorCombo"));
        if (tree_ != nullptr) {
            using Kind = LayerRow::Kind;
            LayerTree::Actions actions;
            actions.toggleVisible = [this](const LayerRow& row) {
                if (row.kind == Kind::Layer) {
                    if (const Layer* layer = document_.findLayer(row.id)) { document_.setLayerVisible(row.id, !layer->visible); }
                } else {
                    document_.setObjectsVisible(idsOf(row), !row.visible);
                }
            };
            actions.toggleLock = [this](const LayerRow& row) {
                if (row.kind == Kind::Layer) {
                    if (const Layer* layer = document_.findLayer(row.id)) { document_.setLayerLocked(row.id, !layer->locked); }
                } else {
                    document_.setObjectsLocked(idsOf(row), !row.locked);
                }
            };
            actions.toggleOpen = [this](const LayerRow& row) {
                if (row.kind == Kind::Layer) {
                    if (const Layer* layer = document_.findLayer(row.id)) { document_.setLayerExpanded(row.id, !layer->expanded); }
                } else {
                    if (!openGroups_.erase(row.id)) { openGroups_.insert(row.id); }
                    refresh();
                }
            };
            actions.activate = [this](const LayerRow& row, bool ctrl, bool shift) {
                if (row.kind == Kind::Layer) {
                    focusObject_ = false;
                    document_.setActiveLayer(row.id);
                    if (ctrl) { document_.selectLayerObjects(row.id); }
                    lastActive_ = 0;   // bring the row into sight even when it was already the active one
                    refresh();
                } else {
                    select(row, ctrl, shift);
                }
            };
            actions.moveSelectionHere = [this](const LayerRow& row) {
                if (row.kind == Kind::Layer) {
                    document_.moveSelectionToLayer(row.id);
                } else {
                    select(row, true, false);   // the square of an object: add it to / take it out of the selection
                }
            };
            actions.drop = [this](const LayerRow& source, const LayerRow& target, LayerTree::Drop where) { drop(source, target, where); };
            tree_->setActions(actions);
        }
        auto onButton = [this](const char* name, std::function<void()> action) {
            if (auto* button = dynamic_cast<newui::Control*>(root_.findView(name))) {
                button->onClick += [action](newui::Control&) {
                    action();
                    return newui::SyncReturn::Handled;
                };
            }
        };
        onButton("addLayerButton", [this]() { document_.addLayer(0); });
        onButton("addSublayerButton", [this]() { document_.addLayer(document_.activeLayer()); });
        onButton("deleteLayerButton", [this]() { document_.deleteLayer(document_.activeLayer()); });
        if (name_ != nullptr) {
            name_->onReturnPressed += [this](newui::TextField&) {
                if (updating_) {
                    return newui::SyncReturn::Handled;
                }
                const std::string text = newui::wideToUtf8(name_->text());
                if (focusObject_ && document_.selection().size() == 1) {
                    document_.renameObject(document_.selection().front(), text);
                } else {
                    document_.renameLayer(document_.activeLayer(), text);
                }
                return newui::SyncReturn::Handled;
            };
        }
        if (color_ != nullptr) {
            auto model = std::make_unique<newui::StringListModel>();
            for (const char* name : kColorNames) {
                model->items().push_back(name);
            }
            color_->setModel(std::move(model));
            color_->onSelectionChanged += [this](newui::DropDownList&) {
                if (!updating_ && color_->selectedIndex().has_value() && *color_->selectedIndex() < 9) {
                    document_.setLayerColor(document_.activeLayer(), kLayerColors[*color_->selectedIndex()]);
                }
                return newui::SyncReturn::Handled;
            };
        }
        document_.onChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        document_.onSelectionChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        refresh();
    }

    std::vector<std::size_t> LayersPanel::idsOf(const LayerRow& row) const {
        return row.kind == LayerRow::Kind::Group ? document_.groupMembers(row.id) : std::vector<std::size_t>{row.id};
    }

    // A click on an object or group row: it becomes the selection (Shift adds it, Ctrl adds or takes it out); what is hidden or locked
    // cannot be selected, but its layer still becomes the active one.
    void LayersPanel::select(const LayerRow& row, bool toggle, bool add) {
        std::vector<std::size_t> ids;
        for (std::size_t id : idsOf(row)) {
            const SketchObject* object = document_.findById(id);
            if (object != nullptr && object->isVisible() && !object->isLocked()) {
                ids.push_back(id);
            }
        }
        focusObject_ = row.kind == LayerRow::Kind::Object;
        document_.setActiveLayer(row.layer);
        if (ids.empty()) {
            refresh();
            return;
        }
        std::vector<std::size_t> selection = toggle || add ? document_.selection() : std::vector<std::size_t>();
        const bool selected = !ids.empty() && document_.isSelected(ids.front());
        for (std::size_t id : ids) {
            auto at = std::find(selection.begin(), selection.end(), id);
            if (toggle && selected) {
                if (at != selection.end()) { selection.erase(at); }
            } else if (at == selection.end()) {
                selection.push_back(id);
            }
        }
        document_.setSelection(selection);
        refresh();
    }

    // The objects of a layer, front first; a group is a row of its own over its members (shown when it is open).
    void LayersPanel::addObjectRows(const Layer& layer, std::size_t depth, std::vector<LayerRow>& rows) const {
        const bool shown = document_.layerShown(layer.id), editable = document_.layerEditable(layer.id);
        const std::uint32_t color = layerColor(layer);
        std::vector<std::size_t> open;   // the groups whose rows have been written, outermost first
        for (std::size_t i = document_.objectCount(); i > 0; --i) {
            const SketchObject& object = document_.object(i - 1);
            if (document_.layerOf(object) != layer.id) {
                continue;
            }
            const std::vector<std::size_t>& chain = document_.groupsOf(object);
            std::size_t levels = chain.size();   // the group rows to show: down to the first one that is closed
            bool objectShown = true;
            for (std::size_t k = 0; k < chain.size(); ++k) {
                if (openGroups_.count(chain[k]) == 0) {
                    levels = k + 1;
                    objectShown = false;
                    break;
                }
            }
            std::size_t common = 0;
            while (common < open.size() && common < levels && open[common] == chain[common]) {
                ++common;
            }
            open.resize(common);
            for (std::size_t k = common; k < levels; ++k) {
                LayerRow row;
                row.kind = LayerRow::Kind::Group;
                row.id = chain[k];
                row.layer = layer.id;
                row.depth = depth + k;
                row.name = document_.groupProps(chain[k]).clip ? "<Clipping Group>" : "<Group>";
                row.shown = shown;
                row.editable = editable;
                row.hasChildren = true;
                row.expanded = openGroups_.count(chain[k]) != 0;
                row.color = color;
                const GroupState& state = groupStates_[chain[k]];
                row.visible = state.visible;
                row.locked = state.locked;
                row.holdsSelection = state.selected;
                rows.push_back(row);
                open.push_back(chain[k]);
            }
            if (!objectShown) {
                continue;
            }
            LayerRow row;
            row.kind = LayerRow::Kind::Object;
            row.id = object.id();
            row.layer = layer.id;
            row.depth = depth + chain.size();
            row.name = displayName(object);
            row.visible = object.ownVisible();
            row.locked = object.ownLocked();
            row.shown = shown;
            row.editable = editable;
            row.color = color;
            row.holdsSelection = document_.isSelected(object.id());
            const SketchPaint& paint = object.isClosed() && !object.fill().isNone() ? object.fill() : object.stroke();
            if (object.isImage()) {
                row.swatchKind = 2;
            } else if (paint.kind() == SketchPaint::Kind::Solid) {
                row.swatchKind = 1;
                row.swatch = paint.color().toRgba32().value & 0x00FFFFFFu;
            } else if (!paint.isNone()) {
                row.swatchKind = 2;
            }
            rows.push_back(row);
        }
    }

    // Top of the stack first: a layer's own objects come first (they lie above its sublayers), then its sublayers, the frontmost first.
    void LayersPanel::addLayerRows(std::size_t parent, std::vector<LayerRow>& rows) const {
        std::vector<std::size_t> ids = document_.layerChildren(parent);
        for (std::size_t i = ids.size(); i > 0; --i) {
            const Layer& layer = *document_.findLayer(ids[i - 1]);
            LayerRow row;
            row.kind = LayerRow::Kind::Layer;
            row.id = layer.id;
            row.layer = layer.id;
            row.depth = document_.layerDepth(layer.id);
            row.name = layer.name;
            row.visible = layer.visible;
            row.locked = layer.locked;
            row.shown = document_.layerShown(layer.id);
            row.editable = document_.layerEditable(layer.id);
            row.hasChildren = !document_.layerChildren(layer.id).empty() || document_.layerObjectCount(layer.id, false) > 0;
            row.expanded = layer.expanded;
            row.active = layer.id == document_.activeLayer();
            row.color = layerColor(layer);
            for (std::size_t id : document_.selection()) {
                const SketchObject* object = document_.findById(id);
                if (object != nullptr && document_.layerOf(*object) == layer.id) {
                    row.holdsSelection = true;
                }
            }
            rows.push_back(row);
            if (layer.expanded && row.hasChildren) {
                addObjectRows(layer, row.depth + 1, rows);
                addLayerRows(layer.id, rows);
            }
        }
    }

    std::size_t LayersPanel::signature() const {
        std::size_t h = 1469598103934665603ull;
        auto mix = [&h](std::size_t v) { h = (h ^ v) * 1099511628211ull; };
        const std::hash<std::string> hashName;
        for (const Layer& layer : document_.layers()) {
            mix(layer.id);
            mix(layer.parent);
            mix((layer.visible ? 1u : 0u) | (layer.locked ? 2u : 0u) | (layer.expanded ? 4u : 0u));
            mix(layer.color);
            mix(hashName(layer.name));
        }
        mix(document_.activeLayer());
        mix(focusObject_ ? 1u : 0u);
        for (std::size_t id : document_.selection()) { mix(id + 7); }
        for (std::size_t id : openGroups_) { mix(id + 13); }
        for (std::size_t i = 0; i < document_.objectCount(); ++i) {
            const SketchObject& object = document_.object(i);
            mix(object.id());
            mix(document_.layerOf(object));
            mix((object.ownVisible() ? 1u : 0u) | (object.ownLocked() ? 2u : 0u) | (object.isVisible() ? 4u : 0u) | (object.isLocked() ? 8u : 0u));
            mix(hashName(object.name()));
            mix(std::size_t(object.type()));
            for (std::size_t g : document_.groupsOf(object)) { mix(g + 3); }
            const SketchPaint& fill = object.fill();
            mix(std::size_t(fill.kind()));
            if (fill.kind() == SketchPaint::Kind::Solid) { mix(fill.color().toRgba32().value); }
            const SketchPaint& stroke = object.stroke();
            mix(std::size_t(stroke.kind()));
            if (stroke.kind() == SketchPaint::Kind::Solid) { mix(stroke.color().toRgba32().value); }
        }
        return h;
    }

    void LayersPanel::refresh() {
        const std::size_t sig = signature();
        if (rowsBuilt_ && sig == lastSignature_) {
            return;   // nothing the rows show has changed
        }
        rowsBuilt_ = true;
        lastSignature_ = sig;
        groupStates_.clear();
        for (std::size_t i = 0; i < document_.objectCount(); ++i) {
            const SketchObject& object = document_.object(i);
            for (std::size_t g : document_.groupsOf(object)) {
                GroupState& state = groupStates_[g];
                state.visible = state.visible && object.ownVisible();
                state.locked = state.locked && object.ownLocked();
                state.selected = state.selected && document_.isSelected(object.id());
            }
        }
        std::vector<LayerRow> rows;
        addLayerRows(0, rows);
        // Scroll to what just changed: the active layer after it was chosen, the first selected object after the selection moved.
        const bool selectionChanged = document_.selection() != lastSelection_;
        const bool activeChanged = document_.activeLayer() != lastActive_;
        lastSelection_ = document_.selection();
        lastActive_ = document_.activeLayer();
        if (selectionChanged && !lastSelection_.empty()) {
            for (LayerRow& row : rows) {
                if (row.kind == LayerRow::Kind::Object && row.holdsSelection) {
                    row.reveal = true;
                    break;
                }
            }
        } else if (activeChanged) {
            for (LayerRow& row : rows) {
                row.reveal = row.kind == LayerRow::Kind::Layer && row.active;
            }
        }
        if (tree_ != nullptr) {
            tree_->setRows(std::move(rows));
        }
        const Layer* layer = document_.findLayer(document_.activeLayer());
        const SketchObject* single = document_.selection().size() == 1 ? document_.findById(document_.selection().front()) : nullptr;
        if (single == nullptr) {
            focusObject_ = false;
        }
        updating_ = true;
        if (name_ != nullptr) {
            name_->setText(newui::utf8ToWide(focusObject_ && single != nullptr ? single->name() : (layer != nullptr ? layer->name : std::string())));
        }
        if (color_ != nullptr && layer != nullptr) {
            const std::uint32_t now = layerColor(*layer);
            for (std::size_t i = 0; i < 9; ++i) {
                if (kLayerColors[i] == now && color_->selectedIndex() != std::optional<std::size_t>(i)) {
                    color_->setSelectedIndex(i);
                }
            }
        }
        updating_ = false;
        if (auto* button = dynamic_cast<newui::Control*>(root_.findView("deleteLayerButton"))) {
            button->setEnabled(document_.layers().size() > 1);
        }
    }

    void LayersPanel::drop(const LayerRow& source, const LayerRow& target, LayerTree::Drop where) {
        using Kind = LayerRow::Kind;
        if (source.kind == Kind::Layer) {
            if (target.kind == Kind::Layer) {
                dropLayer(source.id, target.id, where);
            }
            return;
        }
        // An object or a group goes into a layer (on top), or next to another object or group (in front of it or behind it).
        std::vector<std::size_t> ids = idsOf(source);
        if (ids.empty()) {
            return;
        }
        // A single object leaves its groups; a group keeps what is inside it (and leaves the groups it was in).
        std::size_t keepFrom = std::numeric_limits<std::size_t>::max();
        if (source.kind == Kind::Group) {
            const SketchObject* first = document_.findById(ids.front());
            keepFrom = 0;
            if (first != nullptr) {
                const std::vector<std::size_t>& chain = document_.groupsOf(*first);
                keepFrom = std::size_t(std::find(chain.begin(), chain.end(), source.id) - chain.begin());
            }
        }
        if (target.kind == Kind::Layer) {
            document_.moveObjects(ids, target.id, {}, false, std::vector<std::size_t>{}, keepFrom);
            return;
        }
        std::vector<std::size_t> next = idsOf(target);
        const SketchObject* anchor = next.empty() ? nullptr : document_.findById(next.front());
        if (anchor == nullptr) {
            return;
        }
        std::vector<std::size_t> chain = document_.groupsOf(*anchor);
        if (target.kind == Kind::Group) {   // beside a group: in the groups that hold it
            chain.resize(std::size_t(std::find(chain.begin(), chain.end(), target.id) - chain.begin()));
        }
        document_.moveObjects(ids, 0, next, where == LayerTree::Drop::Above, chain, keepFrom);
    }

    // A layer row dropped on another: above it, below it, or (the middle) inside it as its front-most sublayer.
    void LayersPanel::dropLayer(std::size_t source, std::size_t target, LayerTree::Drop where) {
        const Layer* t = document_.findLayer(target);
        if (t == nullptr || source == target) {
            return;
        }
        if (where == LayerTree::Drop::Inside) {
            document_.moveLayer(source, target, document_.layerChildren(target).size());
            return;
        }
        std::vector<std::size_t> siblings = document_.layerChildren(t->parent);
        siblings.erase(std::remove(siblings.begin(), siblings.end(), source), siblings.end());
        std::size_t at = std::size_t(std::find(siblings.begin(), siblings.end(), target) - siblings.begin());
        document_.moveLayer(source, t->parent, where == LayerTree::Drop::Above ? at + 1 : at);
    }

}
