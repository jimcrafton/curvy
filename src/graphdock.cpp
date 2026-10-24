#include "curvy/graphdock.h"

#include "curvy/pathnode.h"
#include "curvy/theme.h"

#include <newui/bundle.h>
#include <newui/clipboardmgr.h>
#include <newui/segmentedcontrol.h>
#include <newui/utils.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kRowHeight = 24.0f, kCaptionWidth = 74.0f;

        template <typename T>
        T* find(View& root, const char* name) {
            return dynamic_cast<T*>(root.findView(name));
        }

        BLRgba32 rgb(std::uint32_t color, std::uint32_t alpha = 0xFF) { return BLRgba32((color & 0x00FFFFFFu) | (alpha << 24)); }
        Color tint(std::uint32_t color, std::uint32_t alpha = 255) { return Color(Theme::argb(color, alpha), true); }

        std::string typeLabel(const std::string& nodeType) {
            if (nodeType == kPathNodeType) return "Path";
            if (nodeType == kTextNodeType) return "Text";
            if (nodeType == kSymbolNodeType) return "Symbol";
            if (nodeType == kImageNodeType) return "Image";
            return nodeType;
        }

        std::string iconForType(const std::string& nodeType) {
            if (nodeType == kPathNodeType) return "icons/node-path.svg";
            if (nodeType == kTextNodeType) return "icons/node-text.svg";
            if (nodeType == kSymbolNodeType) return "icons/node-symbol.svg";
            if (nodeType == kImageNodeType) return "icons/node-image.svg";
            return "icons/node-shape.svg";
        }

        const char* objectTypeName(SketchObjectType type) {
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

        std::string numberText(double value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", value);
            return buffer;
        }

        bool parseNumber(const std::string& text, double& value) {
            const char* begin = text.data();
            const char* end = begin + text.size();
            while (begin < end && *begin == ' ') {
                ++begin;
            }
            const auto result = std::from_chars(begin, end, value);
            return result.ec == std::errc() && std::isfinite(value);
        }

        // "5 anchors - closed - 140 x 140" for a curve geometry.
        std::string curveSummary(const jitnodes::Geometry& geometry) {
            std::size_t anchors = 0;
            bool closed = !geometry.primitives().empty();
            for (const jitnodes::Primitive& prim : geometry.primitives()) {
                const std::uint32_t segments = jitnodes::cubicSegments(prim);
                anchors += segments > 0 ? segments + (prim.closed ? 0 : 1) : prim.count;
                closed = closed && prim.closed;
            }
            BLPath path;
            BLBox box(0, 0, 0, 0);
            if (pathFromGeometry(geometry, path) && path.size() > 0) {
                path.get_bounding_box(&box);
            }
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer), "%zu anchors, %s, %.0f x %.0f", anchors, closed ? "closed" : "open", box.x1 - box.x0, box.y1 - box.y0);
            return buffer;
        }
    }

    GraphDock::GraphDock(Frame& frame, CanvasView& canvas, WorkspaceModel& model, SketchDocument& document, DockController& docks)
        : frame_(frame), canvas_(canvas), model_(model), document_(document), docks_(docks) {
        dock_ = dynamic_cast<SubView*>(Bundle::instance().loadView("graphDock"));
        if (dock_ == nullptr) {
            return;
        }
        dock_->setVisible(false);
        canvas_.addChild(dock_);
        tree_ = find<TreeView>(*dock_, "graphTree");
        modeControl_ = find<SegmentedControl>(*dock_, "graphModeControl");
        filterField_ = find<TextField>(*dock_, "graphFilterField");
        detail_ = find<SubView>(*dock_, "graphDetail");
        detailTitle_ = find<Label>(*dock_, "graphDetailTitle");
        detailKind_ = find<Label>(*dock_, "graphDetailKind");
        info_ = find<Label>(*dock_, "graphInfo");
        status_ = find<Label>(*dock_, "graphStatus");

        controller_ = std::make_shared<GraphTreeController>();
        controller_->setModel(std::make_unique<GraphTreeModel>());
        if (tree_ != nullptr) {
            tree_->setController(controller_);
            tree_->onSelectionChanged += [this](TreeView&) {
                selectionChangedInTree();
                return SyncReturn::Handled;
            };
        }
        if (modeControl_ != nullptr) {
            modeControl_->setSegments({"Document", "Flow"});
            modeControl_->setSelectedIndex(0);
            modeControl_->onSelectionChanged += [this](SegmentedControl& changed) {
                setMode(changed.selectedIndex() == 1 ? Mode::Flow : Mode::Document);
                return SyncReturn::Handled;
            };
        }
        if (filterField_ != nullptr) {
            filterField_->onKeyUp += [this](View&, std::uint32_t, int, int, std::uint32_t) {
                setFilter(wideToUtf8(filterField_->text()));
                return SyncReturn::Handled;
            };
        }
        buildDetail();
        if (auto* toggle = find<Button>(frame_.rootView(), "graphToggle")) {
            toggle->setToggleButton(true);
            toggle->onClick += [this](Control&) {
                model_.setGraphVisible(!model_.graphVisible());
                return SyncReturn::Handled;
            };
        }
        model_.onChanged += [this](Model&) {
            sync();
            return SyncReturn::Handled;
        };
        canvas_.onSizeChanged += [this](View&, const Size&) {
            layout();
            return SyncReturn::Handled;
        };
        document_.onChanged += [this](SketchDocument&) {
            if (shown_) {
                refresh();
            }
            return SyncReturn::Handled;
        };
        document_.onSelectionChanged += [this](SketchDocument&) {
            if (shown_) {
                refresh();
            }
            return SyncReturn::Handled;
        };
        restyle();
        sync();
    }

    void GraphDock::buildDetail() {
        if (detail_ == nullptr) {
            return;
        }
        detailRows_.resize(kDetailRows);
        for (std::size_t i = 0; i < kDetailRows; ++i) {
            DetailRow& row = detailRows_[i];
            row.caption = new Label();
            row.caption->setVisible(false);
            detail_->addChild(row.caption);
            row.field = new TextField();
            row.field->setVisible(false);
            detail_->addChild(row.field);
            auto apply = [this, i](View&) {
                DetailRow& r = detailRows_[i];
                if (!updating_ && r.apply) {
                    r.apply(wideToUtf8(r.field->text()));
                }
                return SyncReturn::Handled;
            };
            row.field->onReturnPressed += [this, i](TextField&) {
                DetailRow& r = detailRows_[i];
                if (!updating_ && r.apply) {
                    r.apply(wideToUtf8(r.field->text()));
                }
                return SyncReturn::Handled;
            };
            row.field->onLostFocus += apply;
        }
        const char* names[3] = {"Edit", "Copy", "Paste"};
        for (int k = 0; k < 3; ++k) {
            auto* button = new Button();
            button->setText(names[k]);
            button->setVisible(false);
            detail_->addChild(button);
            pathButtons_.push_back(button);
        }
        pathButtons_[0]->onClick += [this](Control&) {
            if (detailObject_ != 0) {
                document_.setSelection({detailObject_});
                canvas_.setTool(SketchTool::DirectSelect);
            }
            return SyncReturn::Handled;
        };
        pathButtons_[1]->onClick += [this](Control&) {
            if (detailNode_ != 0 && document_.network().node(detailNode_).hasGeometryParam("curve")) {
                BLPath path;
                if (pathFromGeometry(*document_.network().node(detailNode_).geometryParam("curve"), path)) {
                    ClipboardManager::setText(utf8ToWide(pathToText(path)), nullptr);
                    status_->setText("Copied path data");
                }
            }
            return SyncReturn::Handled;
        };
        pathButtons_[2]->onClick += [this](Control&) {
            std::wstring text;
            BLPath path;
            if (detailNode_ != 0 && ClipboardManager::getText(text) && pathFromText(wideToUtf8(text), path) && path.size() > 1) {
                document_.setGraphGeometry(detailNode_, "curve", geometryFromPath(path));
                status_->setText("Pasted path data");
            } else if (status_ != nullptr) {
                status_->setText("The clipboard does not hold path data");
            }
            return SyncReturn::Handled;
        };
        layoutDetail();
    }

    void GraphDock::layoutDetail() {
        if (detail_ == nullptr) {
            return;
        }
        const float width = detail_->bounds().size().width;
        float y = 4.0f;
        for (DetailRow& row : detailRows_) {
            row.caption->setBounds(Rect(8.0f, y, kCaptionWidth, 20.0f));
            row.field->setBounds(Rect(8.0f + kCaptionWidth, y, std::max(40.0f, width - kCaptionWidth - 16.0f), 20.0f));
            y += kRowHeight;
        }
        const float buttonWidth = 64.0f;
        for (std::size_t k = 0; k < pathButtons_.size(); ++k) {
            pathButtons_[k]->setBounds(Rect(8.0f + kCaptionWidth + float(k) * (buttonWidth + 6.0f), 4.0f + kRowHeight, buttonWidth, 22.0f));   // under the first row
        }
    }

    void GraphDock::layout() {
        if (dock_ == nullptr) {
            return;
        }
        const Rect client = canvas_.getClientBounds();
        const float top = canvas_.rulerSize();
        const float width = dock_->bounds().size().width;
        SubView* properties = docks_.propertyDock();
        const float beside = properties != nullptr && model_.propertiesVisible() ? properties->bounds().size().width : 0.0f;
        const float height = std::max(0.0f, client.height() - top);
        dock_->setBounds(Rect(client.width() - width - beside, top, width, height));
        if (View* inner = dock_->findView("graphDockBox")) {
            inner->setBounds(Rect(0.0f, 0.0f, width, height));
        }
        layoutDetail();
        canvas_.style().markDirty();
    }

    void GraphDock::sync() {
        if (dock_ == nullptr) {
            return;
        }
        SubView* properties = docks_.propertyDock();
        const float right = properties != nullptr && model_.propertiesVisible() ? properties->bounds().size().width : 0.0f;
        const bool visible = model_.graphVisible();
        if (visible == shown_ && right == shownRight_) {
            return;   // the model changes on every pointer move; the dock only when one of these does
        }
        shown_ = visible;
        shownRight_ = right;
        dock_->setVisible(visible);
        if (auto* toggle = find<Button>(frame_.rootView(), "graphToggle")) {
            if (toggle->isChecked() != visible) {
                toggle->setChecked(visible);
            }
        }
        layout();
        if (visible) {
            refresh();
        }
    }

    void GraphDock::restyle() {
        const Theme& theme = Theme::current();
        if (dock_ == nullptr) {
            return;
        }
        if (View* box = dock_->findView("graphDockBox")) {
            box->style().setBackgroundColor(tint(theme.bgSurface, 0x99));
        }
        for (const char* name : {"graphHeaderBar", "graphDetailBar"}) {
            if (View* bar = dock_->findView(name)) {
                bar->style().setBackgroundColor(tint(theme.slot, 0x99));
            }
        }
        for (Label* label : {detailKind_, info_, status_}) {
            if (label != nullptr) {
                label->setTextColor(rgb(theme.textMuted));
            }
        }
        for (Label* label : {find<Label>(*dock_, "graphDockTitle"), detailTitle_}) {
            if (label != nullptr) {
                label->setTextColor(rgb(theme.textPrimary));
            }
        }
        if (filterField_ != nullptr) {
            filterField_->style().setBackgroundColor(tint(theme.bgApp));
            filterField_->setTextColor(rgb(theme.textPrimary));
        }
        for (DetailRow& row : detailRows_) {
            row.caption->setTextColor(rgb(theme.textMuted));
            row.field->style().setBackgroundColor(tint(theme.bgApp));
            row.field->setTextColor(rgb(theme.textPrimary));
        }
        if (tree_ != nullptr) {
            tree_->style().markDirty();
        }
    }

    const std::vector<GraphRow>& GraphDock::rows() const {
        return rows_;
    }

    void GraphDock::setMode(Mode mode) {
        if (mode == mode_) {
            return;
        }
        mode_ = mode;
        if (modeControl_ != nullptr && modeControl_->selectedIndex() != (mode == Mode::Flow ? 1u : 0u)) {
            modeControl_->setSelectedIndex(mode == Mode::Flow ? 1 : 0);
        }
        collapsed_.clear();
        refresh();
        status_->setText(mode == Mode::Document ? "Layers and groups, each shape with the node behind it" : "The nodes nobody uses, each with what feeds it");
    }

    void GraphDock::setFilter(const std::string& filter) {
        if (filter == filter_) {
            return;
        }
        filter_ = filter;
        refresh();
    }

    std::size_t GraphDock::objectOfNode(jitnodes::NodeId node) const {
        for (std::size_t i = 0; i < document_.objectCount(); ++i) {
            if (document_.object(i).graphNode() == node) {
                return document_.object(i).id();
            }
        }
        return 0;
    }

    void GraphDock::describe(GraphRow& row, jitnodes::NodeId node) const {
        row.node = node;
        const jitnodes::Network& net = document_.network();
        const std::string type = net.node(node).typeName();
        row.icon = iconForType(type);
        const std::size_t objectId = objectOfNode(node);
        const SketchObject* object = objectId != 0 ? document_.findById(objectId) : nullptr;
        row.objectId = objectId;
        if (object != nullptr) {
            row.text = !object->name().empty() ? object->name()
                : object->isText() ? "\"" + object->text().substr(0, 24) + "\"" : std::string("<") + objectTypeName(object->type()) + ">";
            const SketchPaint& paint = object->isClosed() && !object->fill().isNone() ? object->fill() : object->stroke();
            if (paint.kind() == SketchPaint::Kind::Solid) {
                row.chip = true;
                row.chipColor = paint.color().toRgba32().value & 0x00FFFFFFu;
                row.chipAlpha = paint.color().alpha();
            } else if (object->isSymbol() && object->symbol()) {
                row.summary = object->symbol()->name;
            } else if (object->isImage() && object->image()) {
                row.summary = object->image()->name;
            } else {
                row.summary = typeLabel(type);
            }
        } else {
            row.text = net.node(node).name();
            row.summary = typeLabel(type);
        }
    }

    std::vector<GraphRow> GraphDock::documentRows() const {
        std::vector<GraphRow> rows;
        std::function<void(std::size_t, std::size_t)> addLayer = [&](std::size_t parent, std::size_t depth) {
            const std::vector<std::size_t> ids = document_.layerChildren(parent);
            for (std::size_t i = ids.size(); i > 0; --i) {
                const Layer& layer = *document_.findLayer(ids[i - 1]);
                GraphRow row;
                row.kind = GraphRow::Kind::Layer;
                row.depth = depth;
                row.text = layer.name;
                row.icon = "icons/node-layer.svg";
                row.key = "L" + std::to_string(layer.id);
                row.layerId = layer.id;
                rows.push_back(row);
                std::vector<std::size_t> open;   // the groups whose rows were written, outermost first
                for (std::size_t k = document_.objectCount(); k > 0; --k) {
                    const SketchObject& object = document_.object(k - 1);
                    if (object.layer() != layer.id) {
                        continue;
                    }
                    const std::vector<std::size_t>& chain = object.groups();
                    std::size_t common = 0;
                    while (common < open.size() && common < chain.size() && open[common] == chain[common]) {
                        ++common;
                    }
                    open.resize(common);
                    for (std::size_t g = common; g < chain.size(); ++g) {
                        GraphRow group;
                        group.kind = GraphRow::Kind::Group;
                        group.depth = depth + 1 + g;
                        group.text = document_.groupProps(chain[g]).clip ? "<Clipping Group>" : "<Group>";
                        group.icon = "icons/node-group.svg";
                        group.key = "G" + std::to_string(chain[g]);
                        group.groupId = chain[g];
                        group.layerId = layer.id;
                        rows.push_back(group);
                        open.push_back(chain[g]);
                    }
                    GraphRow node;
                    node.kind = GraphRow::Kind::Node;
                    node.depth = depth + 1 + chain.size();
                    node.layerId = layer.id;
                    node.key = "N" + std::to_string(object.id());
                    if (object.graphNode() != 0 && document_.network().nodeIds().size() > 0) {
                        describe(node, object.graphNode());
                    }
                    if (node.node == 0) {
                        node.text = object.name().empty() ? std::string("<") + objectTypeName(object.type()) + ">" : object.name();
                        node.icon = iconForType("");
                    }
                    node.objectId = object.id();
                    rows.push_back(node);
                }
                addLayer(layer.id, depth + 1);
            }
        };
        addLayer(0, 0);
        return rows;
    }

    std::vector<GraphRow> GraphDock::flowRowsFromGraph() const {
        FlowSource source;
        const jitnodes::Network& net = document_.network();
        source.nodes = net.nodeIds();
        for (jitnodes::NodeId id : source.nodes) {
            std::vector<jitnodes::NodeId> inputs;
            for (int i = 0; i < net.node(id).inputCount(); ++i) {
                const jitnodes::NodeId from = net.inputOf(id, i);
                if (from != jitnodes::invalidNodeId) {
                    inputs.push_back(from);
                }
            }
            source.inputs.push_back(std::move(inputs));
        }
        return flowRows(source, [this](GraphRow& row) { describe(row, row.node); });
    }

    std::vector<GraphRow> GraphDock::buildRows() const {
        return filterRows(mode_ == Mode::Document ? documentRows() : flowRowsFromGraph(), filter_);
    }

    std::size_t GraphDock::signature() const {
        std::size_t h = 1469598103934665603ull;
        auto mix = [&h](std::size_t v) { h = (h ^ v) * 1099511628211ull; };
        const std::hash<std::string> hashText;
        mix(mode_ == Mode::Flow ? 1u : 0u);
        mix(hashText(filter_));
        for (const Layer& layer : document_.layers()) {
            mix(layer.id);
            mix(layer.parent);
            mix(hashText(layer.name));
        }
        const jitnodes::Network& net = document_.network();
        mix(net.nodeIds().size());
        for (std::size_t i = 0; i < document_.objectCount(); ++i) {
            const SketchObject& object = document_.object(i);
            mix(object.id());
            mix(object.layer());
            mix(object.graphNode());
            mix(hashText(object.name()));
            mix(std::size_t(object.type()));
            for (std::size_t g : object.groups()) {
                mix(g + 3);
            }
            const SketchPaint& fill = object.fill();
            mix(std::size_t(fill.kind()));
            if (fill.kind() == SketchPaint::Kind::Solid) {
                mix(fill.color().toRgba32().value);
            }
            const SketchPaint& stroke = object.stroke();
            mix(std::size_t(stroke.kind()));
            if (stroke.kind() == SketchPaint::Kind::Solid) {
                mix(stroke.color().toRgba32().value);
            }
            if (object.isText()) {
                mix(hashText(object.text()));
            }
        }
        for (std::size_t id : document_.selection()) {
            mix(id + 7);
        }
        for (const std::string& key : collapsed_) {
            mix(hashText(key));
        }
        mix(detailNode_);
        return h;
    }

    void GraphDock::expandAll() {
        GraphTreeModel* model = controller_->graphModel();
        for (std::size_t i = 0; i < model->rows().size(); ++i) {
            const std::vector<std::size_t> path = model->pathOf(i);
            controller_->setExpanded(path, model->hasChildren(path) && collapsed_.count(model->rows()[i].key) == 0);
        }
    }

    void GraphDock::refresh() {
        if (dock_ == nullptr || tree_ == nullptr) {
            return;
        }
        const std::size_t sig = signature();
        if (built_ && sig == lastSignature_) {
            return;
        }
        built_ = true;
        lastSignature_ = sig;
        updating_ = true;
        // What was open stays open; a row the user closed stays closed, even after the tree is rebuilt.
        GraphTreeModel* model = controller_->graphModel();
        if (model != nullptr) {
            for (std::size_t i = 0; i < model->rows().size(); ++i) {
                const std::vector<std::size_t> path = model->pathOf(i);
                if (model->hasChildren(path) && !controller_->isExpanded(path)) {
                    collapsed_.insert(model->rows()[i].key);
                } else if (model->hasChildren(path)) {
                    collapsed_.erase(model->rows()[i].key);
                }
            }
        }
        rows_ = buildRows();
        auto fresh = std::make_unique<GraphTreeModel>();
        GraphTreeModel* raw = fresh.get();
        raw->setRows(rows_);
        controller_->setModel(std::move(fresh));
        expandAll();
        // The selection of the document is the selection of the tree.
        std::optional<std::vector<std::size_t>> selected;
        const std::vector<std::size_t>& chosen = document_.selection();
        for (std::size_t i = 0; i < rows_.size() && !selected.has_value(); ++i) {
            if (rows_[i].kind == GraphRow::Kind::Node && std::find(chosen.begin(), chosen.end(), rows_[i].objectId) != chosen.end() && rows_[i].objectId != 0) {
                selected = raw->pathOf(i);
            }
        }
        tree_->clearSelection();
        if (selected.has_value()) {
            tree_->setSelectedPath(selected);
        }
        updating_ = false;
        if (info_ != nullptr) {
            std::size_t nodes = document_.network().nodeIds().size();
            info_->setText(std::to_string(nodes) + (nodes == 1 ? " node" : " nodes"));
        }
        showDetail(selected.has_value() ? raw->rowAt(*selected) : nullptr);
        tree_->style().markDirty();
    }

    void GraphDock::selectionChangedInTree() {
        if (updating_ || controller_ == nullptr) {
            return;
        }
        GraphTreeModel* model = controller_->graphModel();
        const auto path = tree_->selectedPath();
        const GraphRow* row = path.has_value() && model != nullptr ? model->rowAt(*path) : nullptr;
        if (row == nullptr) {
            return;
        }
        updating_ = true;
        if (row->kind == GraphRow::Kind::Layer) {
            document_.setActiveLayer(row->layerId);
        } else if (row->kind == GraphRow::Kind::Group) {
            document_.setSelection(document_.groupMembers(row->groupId));
        } else if (row->objectId != 0) {
            document_.setActiveLayer(row->layerId != 0 ? row->layerId : document_.activeLayer());
            document_.setSelection({row->objectId});
        }
        updating_ = false;
        showDetail(row);
    }

    bool GraphDock::selectObject(std::size_t objectId) {
        for (const GraphRow& row : rows_) {
            if (row.kind == GraphRow::Kind::Node && row.objectId == objectId) {
                document_.setSelection({objectId});
                refresh();
                return true;
            }
        }
        return false;
    }

    std::string GraphDock::title() const {
        return detailTitle_ != nullptr ? detailTitle_->text() : std::string();
    }

    std::string GraphDock::detailCaption(std::size_t index) const {
        return index < detailRows_.size() ? detailRows_[index].caption->text() : std::string();
    }

    std::string GraphDock::detailValue(std::size_t index) const {
        return index < detailRows_.size() ? wideToUtf8(detailRows_[index].field->text()) : std::string();
    }

    bool GraphDock::detailShown(std::size_t index) const {
        return index < detailRows_.size() && detailRows_[index].field->isVisible();
    }

    void GraphDock::setDetailValue(std::size_t index, const std::string& text) {
        if (index >= detailRows_.size()) {
            return;
        }
        detailRows_[index].field->setText(utf8ToWide(text));
        if (detailRows_[index].apply) {
            detailRows_[index].apply(text);
        }
    }

    void GraphDock::clearDetail() {
        for (DetailRow& row : detailRows_) {
            row.caption->setVisible(false);
            row.field->setVisible(false);
            row.apply = nullptr;
        }
        for (Button* button : pathButtons_) {
            button->setVisible(false);
        }
    }

    void GraphDock::setRow(std::size_t index, const std::string& caption, const std::string& value, std::function<void(const std::string&)> apply) {
        if (index >= detailRows_.size()) {
            return;
        }
        DetailRow& row = detailRows_[index];
        row.caption->setText(caption);
        row.caption->setVisible(true);
        row.field->setText(utf8ToWide(value));
        row.field->setVisible(true);
        row.field->setEnabled(static_cast<bool>(apply));
        row.apply = std::move(apply);
    }

    void GraphDock::showDetail(const GraphRow* row) {
        if (detail_ == nullptr) {
            return;
        }
        updating_ = true;
        clearDetail();
        detailNode_ = 0;
        detailObject_ = 0;
        if (row == nullptr) {
            detailTitle_->setText("Nothing selected");
            detailKind_->setText("");
        } else if (row->kind == GraphRow::Kind::Layer || row->kind == GraphRow::Kind::Group) {
            detailTitle_->setText(row->text);
            detailKind_->setText(row->kind == GraphRow::Kind::Layer ? "Layer" : "Group");
        } else if (row->node != 0 && document_.network().nodeIds().end() != std::find(document_.network().nodeIds().begin(), document_.network().nodeIds().end(), row->node)) {
            detailNode_ = row->node;
            detailObject_ = row->objectId;
            jitnodes::Node& node = document_.network().node(row->node);
            const std::string type = node.typeName();
            detailTitle_->setText(row->text);
            detailKind_->setText(typeLabel(type));
            std::size_t at = 0;
            const jitnodes::NodeId id = row->node;
            if (type == kPathNodeType) {
                setRow(at++, "Curves", curveSummary(*node.geometryParam("curve")), nullptr);
                for (Button* button : pathButtons_) {
                    button->setVisible(true);
                }
                at++;   // the row of buttons
            } else if (type == kTextNodeType) {
                setRow(at++, "Words", node.getString("text"), [this, id](const std::string& value) { document_.setGraphString(id, "text", value); });
                setRow(at++, "Font", node.getString("font"), [this, id](const std::string& value) { document_.setGraphString(id, "font", value); });
                setRow(at++, "Size", node.getString("size"), [this, id](const std::string& value) {
                    double size = 0.0;
                    if (parseNumber(value, size) && size > 0.0) {
                        document_.setGraphString(id, "size", numberText(size));
                    }
                });
            } else if (type == kSymbolNodeType) {
                setRow(at++, "Symbol", node.getString("symbol"), nullptr);
            } else if (type == kImageNodeType) {
                setRow(at++, "Picture", node.getString("image"), nullptr);
            } else {
                for (const std::string& name : node.floatNames()) {
                    if (at >= 4) {
                        break;
                    }
                    setRow(at++, name, numberText(node.baseFloat(name)), [this, id, name](const std::string& value) {
                        double number = 0.0;
                        if (parseNumber(value, number)) {
                            document_.setGraphParam(id, name, float(number));
                        }
                    });
                }
            }
            const jitnodes::Network& net = document_.network();
            std::string inputs, users;
            for (int i = 0; i < node.inputCount(); ++i) {
                const jitnodes::NodeId from = net.inputOf(id, i);
                if (from != jitnodes::invalidNodeId) {
                    inputs += (inputs.empty() ? "" : ", ") + net.node(from).name();
                }
            }
            for (jitnodes::NodeId other : net.nodeIds()) {
                for (int i = 0; i < net.node(other).inputCount(); ++i) {
                    if (net.inputOf(other, i) == id) {
                        users += (users.empty() ? "" : ", ") + net.node(other).name();
                    }
                }
            }
            setRow(at, "Inputs", inputs.empty() ? "None" : inputs, nullptr);
            setRow(at + 1, "Used by", users.empty() ? "Nothing" : users, nullptr);
        } else {
            detailTitle_->setText(row->text);
            detailKind_->setText("");
        }
        layoutDetail();
        restyle();
        updating_ = false;
    }

}
