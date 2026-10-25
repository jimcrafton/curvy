#include "curvy/graphdock.h"

#include "curvy/appearancenode.h"
#include "curvy/numberscrubber.h"
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
            if (nodeType == kFillNodeType) return "Fill";
            if (nodeType == kStrokeNodeType) return "Stroke";
            if (nodeType == kEffectNodeType) return "Effect";
            return nodeType;
        }

        std::string iconForType(const std::string& nodeType) {
            if (nodeType == kPathNodeType) return "icons/node-path.svg";
            if (nodeType == kTextNodeType) return "icons/node-text.svg";
            if (nodeType == kSymbolNodeType) return "icons/node-symbol.svg";
            if (nodeType == kImageNodeType) return "icons/node-image.svg";
            if (nodeType == kFillNodeType) return "icons/node-fill.svg";
            if (nodeType == kStrokeNodeType) return "icons/node-stroke.svg";
            if (nodeType == kEffectNodeType) return "icons/node-effect.svg";
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
            row.chip = new ColourChip();
            row.chip->setVisible(false);
            detail_->addChild(row.chip);
            row.slider = new Slider();
            row.slider->setRange(0.0f, 100.0f);
            row.slider->setVisible(false);
            detail_->addChild(row.slider);
            row.percent = new Label();
            row.percent->setTextAlignment(TextAlignment::Right);
            row.percent->setVisible(false);
            detail_->addChild(row.percent);
            NumberScrubber::Callbacks scrub;
            scrub.begin = [this, i](double& value) {
                const DetailRow& r = detailRows_[i];
                if (!r.scrub || r.node == 0) {
                    return false;
                }
                value = r.scale * document_.network().node(r.node).baseFloat(r.param);
                scrubBefore_ = std::make_shared<jitnodes::GraphSnapshot>(document_.snapshotGraph());
                return true;
            };
            scrub.change = [this, i](double value) {
                DetailRow& r = detailRows_[i];
                value = std::clamp(value, r.minimum, r.maximum);
                document_.setGraphFloatLive(r.node, r.param, float(value / r.scale));
                if (r.kind == DetailRow::Kind::Slider) {
                    r.slider->setValue(float(value));
                } else {
                    r.field->setText(utf8ToWide(numberText(value)));
                }
            };
            scrub.end = [this, i](bool changed) {
                if (changed && scrubBefore_) {
                    document_.commitGraphEdit(*scrubBefore_, "Change " + detailRows_[i].param);
                }
                scrubBefore_.reset();
            };
            NumberScrubber::attach(*row.caption, 1.0, -1e9, 1e9, std::move(scrub));
            row.slider->onValueChanged += [this, i](Slider& slider) {
                detailRows_[i].percent->setText(numberText(std::round(slider.value())) + "%");
                return SyncReturn::Handled;
            };
            row.slider->onMouseUp += [this, i](View&, const Point&, std::uint32_t, std::uint32_t) {
                commitSlider(i);
                return SyncReturn::Ignored;
            };
            row.slider->onKeyUp += [this, i](View&, std::uint32_t, int, int, std::uint32_t) {
                commitSlider(i);
                return SyncReturn::Ignored;
            };
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
        auto makeLabel = [this](bool link) {
            auto* label = new Label();
            label->setVisible(false);
            if (link) {
                label->setHotLink(true);
                label->setTextAlignment(TextAlignment::Right);
            } else {
                label->setTextAlignment(TextAlignment::Left);
            }
            detail_->addChild(label);
            return label;
        };
        inputsHeader_ = makeLabel(false);
        inputsHeader_->setText("INPUTS");
        usersHeader_ = makeLabel(false);
        usersHeader_->setText("USED BY");
        noInputs_ = makeLabel(false);
        noInputs_->setText("None");
        noUsers_ = makeLabel(false);
        noUsers_->setText("Nothing");
        for (std::vector<LinkRow>* list : {&inputRows_, &userRows_}) {
            list->resize(kLinkRows);
            for (std::size_t k = 0; k < kLinkRows; ++k) {
                LinkRow& link = (*list)[k];
                link.index = makeLabel(false);
                link.name = makeLabel(false);
                link.go = makeLabel(true);
                link.go->setText("Go to");
                link.go->onLinkClicked += [this, list, k](Label&) {
                    selectNode((*list)[k].node);
                    return SyncReturn::Handled;
                };
            }
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
        const float fieldLeft = 8.0f + kCaptionWidth, fieldWidth = std::max(40.0f, width - fieldLeft - 8.0f);
        float y = 4.0f;
        for (std::size_t i = 0; i < detailRows_.size(); ++i) {
            DetailRow& row = detailRows_[i];
            if (!row.caption->isVisible()) {
                continue;
            }
            row.caption->setBounds(Rect(8.0f, y, kCaptionWidth, 20.0f));
            if (row.kind == DetailRow::Kind::Colour) {
                row.chip->setBounds(Rect(fieldLeft, y + 1.0f, 28.0f, 18.0f));
                row.field->setBounds(Rect(fieldLeft + 34.0f, y, std::max(40.0f, fieldWidth - 34.0f), 20.0f));
            } else if (row.kind == DetailRow::Kind::Slider) {
                row.slider->setBounds(Rect(fieldLeft, y, std::max(40.0f, fieldWidth - 40.0f), 20.0f));
                row.percent->setBounds(Rect(width - 8.0f - 38.0f, y, 38.0f, 20.0f));
            } else {
                row.field->setBounds(Rect(fieldLeft, y, fieldWidth, 20.0f));
            }
            y += kRowHeight;
            if (i == 0 && pathButtons_.front()->isVisible()) {
                const float buttonWidth = 64.0f;
                for (std::size_t k = 0; k < pathButtons_.size(); ++k) {
                    pathButtons_[k]->setBounds(Rect(fieldLeft + float(k) * (buttonWidth + 6.0f), y, buttonWidth, 22.0f));
                }
                y += kRowHeight;
            }
        }
        auto section = [&](Label* header, Label* none, std::vector<LinkRow>& links) {
            if (!header->isVisible()) {
                return;
            }
            y += 4.0f;
            header->setBounds(Rect(8.0f, y, width - 16.0f, 18.0f));
            y += 20.0f;
            if (none->isVisible()) {
                none->setBounds(Rect(8.0f, y, width - 16.0f, 20.0f));
                y += kRowHeight;
            }
            for (LinkRow& link : links) {
                if (!link.name->isVisible()) {
                    continue;
                }
                link.index->setBounds(Rect(8.0f, y, 20.0f, 20.0f));
                link.name->setBounds(Rect(fieldLeft, y, std::max(40.0f, fieldWidth - 52.0f), 20.0f));
                link.go->setBounds(Rect(width - 8.0f - 48.0f, y, 48.0f, 20.0f));
                y += kRowHeight;
            }
        };
        section(inputsHeader_, noInputs_, inputRows_);
        section(usersHeader_, noUsers_, userRows_);
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
            row.percent->setTextColor(rgb(theme.textPrimary));
        }
        for (Label* label : {inputsHeader_, usersHeader_, noInputs_, noUsers_}) {
            if (label != nullptr) {
                label->setTextColor(rgb(theme.textMuted));
            }
        }
        for (std::vector<LinkRow>* list : {&inputRows_, &userRows_}) {
            for (LinkRow& link : *list) {
                link.index->setTextColor(rgb(theme.textMuted));
                link.name->setTextColor(rgb(theme.textPrimary));
                link.go->setLinkColor(rgb(theme.accent));
                link.go->setHoveredLinkColor(rgb(theme.accentHover));
            }
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

    const AppearanceItem* GraphDock::itemOfNode(jitnodes::NodeId node, std::size_t* objectId) const {
        const jitnodes::Network& net = document_.network();
        std::size_t steps = 0;
        jitnodes::NodeId at = node;
        while (isAppearanceNodeType(net.node(at).typeName()) && steps < 1000) {
            at = net.inputOf(at, 0);
            ++steps;
            if (at == jitnodes::invalidNodeId) {
                return nullptr;
            }
        }
        const SketchObject* object = document_.findById(objectOfNode(at));
        if (object == nullptr || steps == 0 || steps > object->appearance().size()) {
            return nullptr;
        }
        if (objectId != nullptr) {
            *objectId = object->id();
        }
        return &object->appearance()[steps - 1];
    }

    void GraphDock::describeLook(GraphRow& row, jitnodes::NodeId node) const {
        const jitnodes::Network& net = document_.network();
        const std::string type = net.node(node).typeName();
        row.look = true;
        row.text = typeLabel(type);
        std::size_t objectId = 0;
        const AppearanceItem* item = itemOfNode(node, &objectId);
        row.objectId = objectId;
        if (item == nullptr) {
            return;
        }
        if (type == kEffectNodeType) {
            row.text = effectName(item->effect);
            if (item->effect != EffectKind::RoundCorners) {
                row.summary = numberText(isGeometryEffect(item->effect) ? item->radius : item->softness) + " px";
            }
        } else {
            const SketchPaint& paint = item->paint;
            if (paint.kind() == SketchPaint::Kind::Solid) {
                row.chip = true;
                row.chipColor = paint.color().toRgba32().value & 0x00FFFFFFu;
                row.chipAlpha = paint.color().alpha();
            } else if (paint.kind() == SketchPaint::Kind::Linear || paint.kind() == SketchPaint::Kind::Radial) {
                row.summary = paint.kind() == SketchPaint::Kind::Linear ? "Linear gradient" : "Radial gradient";
            } else if (paint.isPattern()) {
                row.summary = "Pattern";
            } else if (type == kFillNodeType) {
                row.summary = "None";
            }
            if (type == kStrokeNodeType && !paint.isNone()) {
                row.summary = numberText(item->strokeWidth) + " px";
            }
        }
        if (!item->visible) {
            row.text += " (hidden)";
        }
    }

    void GraphDock::describe(GraphRow& row, jitnodes::NodeId node) const {
        row.node = node;
        const jitnodes::Network& net = document_.network();
        const std::string type = net.node(node).typeName();
        row.icon = iconForType(type);
        if (isAppearanceNodeType(type)) {
            describeLook(row, node);
            return;
        }
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
        const jitnodes::Network& net = document_.network();
        const std::vector<jitnodes::NodeId> nodeIds = net.nodeIds();
        const auto successors = appearanceSuccessors(net, nodeIds);
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
                    if (object.graphNode() != 0) {
                        const std::vector<jitnodes::NodeId> chain = appearanceChain(successors, object.graphNode());
                        for (std::size_t c = chain.size(); c > 0; --c) {
                            GraphRow look;
                            look.depth = node.depth + 1;
                            look.layerId = layer.id;
                            look.key = node.key + "/" + std::to_string(chain[c - 1]);
                            describe(look, chain[c - 1]);
                            rows.push_back(look);
                        }
                    }
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
            mix(std::size_t(std::lround(object.strokeWidth() * 100.0)));
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
        mix(hashText(selectedKey_));
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
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1 && !selected.has_value()) {
                selectedKey_.clear();   // its object was deselected: the next time the object is picked, its own row shows
            }
            for (std::size_t i = 0; i < rows_.size() && !selected.has_value(); ++i) {
                const GraphRow& candidate = rows_[i];
                const bool wanted = pass == 0 ? candidate.key == selectedKey_ : !candidate.look;
                if (wanted && candidate.kind == GraphRow::Kind::Node && candidate.objectId != 0 && std::find(chosen.begin(), chosen.end(), candidate.objectId) != chosen.end()) {
                    selected = raw->pathOf(i);
                }
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
        const GraphRow* found = path.has_value() && model != nullptr ? model->rowAt(*path) : nullptr;
        if (found == nullptr) {
            return;
        }
        // Changing the selection rebuilds the tree and frees the model the row lives in.
        const GraphRow row = *found;
        selectedKey_ = row.key;
        updating_ = true;
        if (row.kind == GraphRow::Kind::Layer) {
            document_.setActiveLayer(row.layerId);
        } else if (row.kind == GraphRow::Kind::Group) {
            document_.setSelection(document_.groupMembers(row.groupId));
        } else if (row.objectId != 0) {
            document_.setActiveLayer(row.layerId != 0 ? row.layerId : document_.activeLayer());
            document_.setSelection({row.objectId});
        }
        updating_ = false;
        showDetail(&row);
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

    bool GraphDock::selectRow(const std::string& key) {
        for (const GraphRow& row : rows_) {
            if (row.key == key && row.kind == GraphRow::Kind::Node && row.objectId != 0) {
                const std::size_t objectId = row.objectId;
                selectedKey_ = key;
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
        if (index >= detailRows_.size()) {
            return std::string();
        }
        const DetailRow& row = detailRows_[index];
        return row.kind == DetailRow::Kind::Slider ? row.percent->text() : wideToUtf8(row.field->text());
    }

    std::vector<std::string> GraphDock::detailLinks(bool inputs) const {
        std::vector<std::string> names;
        for (const LinkRow& link : inputs ? inputRows_ : userRows_) {
            if (link.name->isVisible()) {
                names.push_back(link.name->text());
            }
        }
        return names;
    }

    bool GraphDock::selectNode(jitnodes::NodeId node) {
        for (const GraphRow& row : rows_) {
            if (row.node == node && row.kind == GraphRow::Kind::Node && row.objectId != 0) {
                return selectRow(row.key);
            }
        }
        return false;
    }

    void GraphDock::commitSlider(std::size_t index) {
        if (updating_ || index >= detailRows_.size() || !detailRows_[index].apply) {
            return;
        }
        detailRows_[index].apply(numberText(std::round(detailRows_[index].slider->value())));
    }

    bool GraphDock::detailShown(std::size_t index) const {
        return index < detailRows_.size() && detailRows_[index].caption->isVisible();
    }

    void GraphDock::setDetailValue(std::size_t index, const std::string& text) {
        if (index >= detailRows_.size()) {
            return;
        }
        if (detailRows_[index].kind == DetailRow::Kind::Slider) {
            double percent = 0.0;
            if (parseNumber(text, percent)) {
                detailRows_[index].slider->setValue(float(percent));
            }
        } else {
            detailRows_[index].field->setText(utf8ToWide(text));
        }
        if (detailRows_[index].apply) {
            detailRows_[index].apply(text);
        }
    }

    void GraphDock::clearDetail() {
        for (DetailRow& row : detailRows_) {
            row.caption->setVisible(false);
            row.field->setVisible(false);
            row.chip->setVisible(false);
            row.slider->setVisible(false);
            row.percent->setVisible(false);
            row.kind = DetailRow::Kind::Text;
            row.scrub = false;
            row.apply = nullptr;
        }
        for (Label* label : {inputsHeader_, usersHeader_, noInputs_, noUsers_}) {
            if (label != nullptr) {
                label->setVisible(false);
            }
        }
        for (std::vector<LinkRow>* list : {&inputRows_, &userRows_}) {
            for (LinkRow& link : *list) {
                link.index->setVisible(false);
                link.name->setVisible(false);
                link.go->setVisible(false);
            }
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

    void GraphDock::lookRows(const jitnodes::Node& node, jitnodes::NodeId id, std::size_t& at) {
        auto number = [&](const char* caption, const char* name, double lowest = 0.0) {
            setRow(at++, caption, numberText(node.baseFloat(name)), [this, id, key = std::string(name)](const std::string& value) {
                double parsed = 0.0;
                if (parseNumber(value, parsed)) {
                    document_.setGraphParam(id, key, float(parsed));
                }
            });
            scrubRow(at - 1, id, name, lowest, 10000.0);
        };
        auto choice = [&](const char* caption, const char* name, std::vector<std::string> names) {
            const std::size_t current = std::size_t(std::max(0L, std::lround(node.baseFloat(name))));
            setRow(at++, caption, current < names.size() ? names[current] : std::string(), [this, id, key = std::string(name), names](const std::string& value) {
                for (std::size_t i = 0; i < names.size(); ++i) {
                    if (names[i] == value) {
                        document_.setGraphParam(id, key, float(i));
                    }
                }
            });
        };
        auto slider = [&](const char* caption, const char* name) {
            setSliderRow(at++, caption, std::clamp(double(node.baseFloat(name)), 0.0, 1.0), [this, id, key = std::string(name)](const std::string& value) {
                double percent = 0.0;
                if (parseNumber(value, percent)) {
                    document_.setGraphParam(id, key, float(std::clamp(percent, 0.0, 100.0) / 100.0));
                }
            });
            scrubRow(at - 1, id, name, 0.0, 100.0, 100.0);
        };
        const std::string type = node.typeName();
        if (type == kEffectNodeType) {
            std::size_t objectId = 0;
            const AppearanceItem* item = itemOfNode(id, &objectId);
            setRow(at++, "Effect", item != nullptr ? effectName(item->effect) : "", nullptr);
            slider("Amount", "opacity");
            number("Softness", "softness");
            number("Offset X", "offsetX", -10000.0);
            number("Offset Y", "offsetY", -10000.0);
            number("Radius", "radius");
            return;
        }
        const int paint = int(std::lround(node.baseFloat("paint")));
        const AppearanceItem* item = itemOfNode(id);
        if (paint == 1 && item != nullptr) {
            setColourRow(at++, "Colour", item->paint.color(), [this, id](const std::string& value) {
                try {
                    const SketchColor parsed = SketchColor::fromHex(value);
                    float r = 0.0f, g = 0.0f, b = 0.0f;
                    parsed.toRgb(r, g, b);
                    document_.setGraphFloats(id, {{"space", 0.0f}, {"c0", r}, {"c1", g}, {"c2", b}, {"c3", 0.0f}, {"alpha", parsed.alpha()}}, "Change Colour");
                } catch (const std::exception&) {
                }
            });
        } else {
            choice("Paint", "paint", {"None", "Solid", "Linear", "Radial", "Pattern"});
        }
        if (type == kStrokeNodeType) {
            number("Width", "width");
        }
        slider("Opacity", "opacity");
    }

    void GraphDock::setColourRow(std::size_t index, const std::string& caption, const SketchColor& color, std::function<void(const std::string&)> apply) {
        if (index >= detailRows_.size()) {
            return;
        }
        std::string hex = color.toHex();
        if (color.alpha() < 1.0f) {
            char alpha[4];
            std::snprintf(alpha, sizeof(alpha), "%02x", int(std::lround(color.alpha() * 255.0f)));
            hex += alpha;
        }
        setRow(index, caption, hex, std::move(apply));
        DetailRow& row = detailRows_[index];
        row.kind = DetailRow::Kind::Colour;
        row.chip->setColour(color.toRgba32().value & 0x00FFFFFFu, color.alpha());
        row.chip->setVisible(true);
    }

    void GraphDock::setSliderRow(std::size_t index, const std::string& caption, double value, std::function<void(const std::string&)> apply) {
        if (index >= detailRows_.size()) {
            return;
        }
        setRow(index, caption, "", std::move(apply));
        DetailRow& row = detailRows_[index];
        row.kind = DetailRow::Kind::Slider;
        row.field->setVisible(false);
        row.slider->setValue(float(value * 100.0));
        row.slider->setVisible(true);
        row.percent->setText(numberText(std::round(value * 100.0)) + "%");
        row.percent->setVisible(true);
    }

    void GraphDock::scrubRow(std::size_t index, jitnodes::NodeId id, const std::string& param, double minimum, double maximum, double scale) {
        if (index < detailRows_.size()) {
            DetailRow& row = detailRows_[index];
            row.scrub = true;
            row.node = id;
            row.param = param;
            row.minimum = minimum;
            row.maximum = maximum;
            row.scale = scale;
        }
    }

    void GraphDock::showLinks(jitnodes::NodeId id) {
        const jitnodes::Network& net = document_.network();
        inputNodes_.clear();
        userNodes_.clear();
        for (int i = 0; i < net.node(id).inputCount(); ++i) {
            const jitnodes::NodeId from = net.inputOf(id, i);
            if (from != jitnodes::invalidNodeId) {
                inputNodes_.push_back(from);
            }
        }
        for (jitnodes::NodeId other : net.nodeIds()) {
            for (int i = 0; i < net.node(other).inputCount(); ++i) {
                if (net.inputOf(other, i) == id) {
                    userNodes_.push_back(other);
                    break;
                }
            }
        }
        auto fill = [&](Label* header, Label* none, std::vector<LinkRow>& rows, const std::vector<jitnodes::NodeId>& nodes, bool numbered) {
            header->setVisible(true);
            none->setVisible(nodes.empty());
            for (std::size_t k = 0; k < rows.size(); ++k) {
                const bool shown = k < nodes.size();
                rows[k].index->setVisible(shown && numbered);
                rows[k].name->setVisible(shown);
                rows[k].go->setVisible(shown);
                if (shown) {
                    GraphRow described;
                    describe(described, nodes[k]);
                    rows[k].node = nodes[k];
                    rows[k].index->setText(std::to_string(k + 1));
                    rows[k].name->setText(described.text);
                }
            }
        };
        fill(inputsHeader_, noInputs_, inputRows_, inputNodes_, true);
        fill(usersHeader_, noUsers_, userRows_, userNodes_, false);
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
        } else if (row->node != 0 && [&] { const std::vector<jitnodes::NodeId> ids = document_.network().nodeIds(); return std::find(ids.begin(), ids.end(), row->node) != ids.end(); }()) {
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
            } else if (isAppearanceNodeType(type)) {
                lookRows(node, id, at);
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
                    scrubRow(at - 1, id, name, -100000.0, 100000.0);
                }
            }
            showLinks(id);
        } else {
            detailTitle_->setText(row->text);
            detailKind_->setText("");
        }
        layoutDetail();
        restyle();
        updating_ = false;
    }

}
