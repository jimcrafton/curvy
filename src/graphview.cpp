#include "curvy/graphview.h"

#include "curvy/graphoverviews.h"
#include "curvy/theme.h"

#include <jitnodes/graph/network.h>
#include <jitnodes/graph/subnet.h>
#include <newui/font.h>
#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/rootview.h>

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr double kBoxWidth = 150.0;
        constexpr double kColumnGap = 60.0;
        constexpr double kRowGap = 24.0;
        constexpr double kPortStep = 12.0;
        constexpr double kPortTop = 22.0;
    }

    GraphView::GraphView() {
        setName("GraphView");
        setVisible(true);
        setAcceptsFocus(true);
        onMouseDown.add(this, &GraphView::handleMouseDown);
        onMouseDblClick.add(this, &GraphView::handleMouseDoubleClick);
        onMouseMove.add(this, &GraphView::handleMouseMove);
        onMouseUp.add(this, &GraphView::handleMouseUp);
        onMouseWheel.add(this, &GraphView::handleMouseWheel);
        onKeyDown.add(this, &GraphView::handleKeyDown);
        onSizeChanged.add(this, &GraphView::handleSizeChanged);
    }

    // The document preview stays at the top left (where it was made) and the minimap goes to the lower right.
    SyncReturn GraphView::handleSizeChanged(View&, const Size& size) {
        if (View* map = findView("graphMiniMap")) {
            const Rect bounds = map->bounds();
            map->setBounds(Rect(size.width - bounds.width() - 10.0f, size.height - bounds.height() - 10.0f, bounds.width(), bounds.height()));
        }
        return SyncReturn::Handled;
    }

    void GraphView::setDocument(SketchDocument* document) {
        document_ = document;
        scope_ = 0;
        selected_ = NodeRef();
        selection_.clear();
        fitted_ = false;
        style().markDirty();
    }

    bool GraphView::enter(const NodeRef& subnet) {
        if (document_ == nullptr || !document_->hasNode(subnet)) {
            return false;
        }
        auto sn = dynamic_cast<jitnodes::SubnetNode*>(&document_->node(subnet));
        if (sn == nullptr) {
            return false;
        }
        scope_ = document_->scopeOfSubnet(subnet);
        selected_ = NodeRef();
        selection_.clear();
        fitted_ = false;
        style().markDirty();
        if (onSelectionChanged) {
            onSelectionChanged();
        }
        return true;
    }

    bool GraphView::exit() {
        if (document_ == nullptr || scope_ == 0) {
            return false;
        }
        const NodeRef left = document_->parentOfScope(scope_);
        scope_ = left.scope;
        selected_ = NodeRef();
        selection_.clear();
        fitted_ = false;
        select(left);
        style().markDirty();
        return true;
    }

    void GraphView::select(const NodeRef& node) {
        if (selected_ == node && selection_.size() == (node.valid() ? 1u : 0u)) {
            return;
        }
        selected_ = node;
        selection_.clear();
        if (node.valid()) {
            selection_.push_back(node);
        }
        style().markDirty();
        if (onSelectionChanged) {
            onSelectionChanged();
        }
    }

    GraphViewState GraphView::state() const {
        GraphViewState state;
        if (document_ == nullptr) {
            return state;
        }
        state.level = document_->scopePath(scope_);
        state.placed = fitted_;
        state.zoom = zoom_;
        state.panX = panX_;
        state.panY = panY_;
        for (const NodeRef& ref : selection_) {
            state.selected.push_back(ref.id);
        }
        return state;
    }

    void GraphView::restore(const GraphViewState& state) {
        if (document_ == nullptr) {
            return;
        }
        std::uint32_t scope = 0;
        for (jitnodes::NodeId id : state.level) {
            const NodeRef subnet(scope, id);
            if (!document_->hasNode(subnet) || dynamic_cast<jitnodes::SubnetNode*>(&document_->node(subnet)) == nullptr) {
                scope = 0;
                break;
            }
            scope = document_->scopeOfSubnet(subnet);
        }
        scope_ = scope;
        selection_.clear();
        selected_ = NodeRef();
        for (jitnodes::NodeId id : state.selected) {
            const NodeRef ref(scope_, id);
            if (document_->hasNode(ref)) {
                selection_.push_back(ref);
                selected_ = ref;
            }
        }
        fitted_ = state.placed;
        if (state.placed) {
            zoom_ = std::clamp(state.zoom, 0.1, 4.0);
            panX_ = state.panX;
            panY_ = state.panY;
        }
        style().markDirty();
        if (onSelectionChanged) {
            onSelectionChanged();
        }
        if (onViewChanged) {
            onViewChanged();
        }
    }

    std::vector<std::string> GraphView::levelNames() const {
        std::vector<std::string> names;
        if (document_ == nullptr) {
            return names;
        }
        for (std::uint32_t s = scope_; s != 0;) {
            const NodeRef owner = document_->parentOfScope(s);
            if (!document_->hasNode(owner)) {
                break;
            }
            names.push_back(document_->node(owner).name());
            s = owner.scope;
        }
        names.push_back("root");
        std::reverse(names.begin(), names.end());
        return names;
    }

    bool GraphView::goToLevel(std::size_t level) {
        bool moved = false;
        while (levelNames().size() > level + 1 && exit()) {
            moved = true;
        }
        return moved;
    }

    std::string GraphView::path() const {
        std::string text;
        for (const std::string& name : levelNames()) {
            text += text.empty() ? name : " > " + name;
        }
        return text;
    }

    std::string GraphView::describeSelected() const {
        if (document_ == nullptr || !selected_.valid() || !document_->hasNode(selected_)) {
            return "";
        }
        const jitnodes::Node& node = document_->node(selected_);
        std::string text = node.name() + " (" + node.typeName() + ")";
        if (node.bypassed()) {
            text += ", bypassed";
        }
        if (node.frozen()) {
            text += ", frozen";
        }
        for (const std::string& tag : node.tags()) {
            text += " #" + tag;
        }
        return text;
    }

    std::string GraphView::hint() const {
        std::string text = describeSelected();
        const bool onSubnet = !text.empty() && dynamic_cast<const jitnodes::SubnetNode*>(&document_->node(selected_)) != nullptr;
        if (text.empty()) {
            text = scope_ == 0 ? "Graph: click a node to select it" : "Graph: inside " + path();
        }
        if (onSubnet) {
            text += "  |  double-click to enter";
        }
        if (scope_ != 0) {
            text += "  |  Backspace to exit";
        }
        return text;
    }

    // Saved positions win; the rest go in a column per depth (how far the longest chain back to a source is), top to bottom in id order.
    std::vector<GraphView::Box> GraphView::boxes(const jitnodes::Network& network) const {
        std::map<jitnodes::NodeId, int> depth;
        const std::vector<jitnodes::NodeId> ids = network.nodeIds();
        for (std::size_t pass = 0; pass <= ids.size(); ++pass) {
            bool moved = false;
            for (const jitnodes::NodeId id : ids) {
                int best = 0;
                for (int i = 0; i < network.node(id).inputCount(); ++i) {
                    const jitnodes::NodeId from = network.inputOf(id, i);
                    if (from != jitnodes::invalidNodeId) {
                        best = (std::max)(best, depth[from] + 1);
                    }
                }
                if (depth[id] != best) {
                    depth[id] = best;
                    moved = true;
                }
            }
            if (!moved) {
                break;
            }
        }
        std::map<int, double> nextY;
        std::vector<Box> result;
        for (const jitnodes::NodeId id : ids) {
            const jitnodes::Node& node = network.node(id);
            Box box;
            box.id = id;
            box.w = kBoxWidth;
            box.h = 40.0 + kPortStep * double((std::max)(0, node.inputCount() - 1));
            if (node.hasPosition()) {
                box.x = double(node.posX());
                box.y = double(node.posY());
            }
            result.push_back(box);
        }
        // The laid-out boxes go where no placed box is, so a node added next to pinned ones never lands on top of one.
        const auto hits = [&](const Box& box) -> const Box* {
            for (const Box& other : result) {
                if (&other != &box && other.placed && box.x < other.x + other.w + kRowGap * 0.5 && other.x < box.x + box.w + kRowGap * 0.5 &&
                    box.y < other.y + other.h + kRowGap * 0.5 && other.y < box.y + box.h + kRowGap * 0.5) {
                    return &other;
                }
            }
            return nullptr;
        };
        for (Box& box : result) {
            box.placed = network.node(box.id).hasPosition();
        }
        for (Box& box : result) {
            if (box.placed) {
                continue;
            }
            double& y = nextY[depth[box.id]];
            box.x = double(depth[box.id]) * (kBoxWidth + kColumnGap);
            box.y = y;
            while (const Box* blocker = hits(box)) {
                box.y = blocker->y + blocker->h + kRowGap;
            }
            y = box.y + box.h + kRowGap;
            box.placed = true;
        }
        return result;
    }

    BLPoint GraphView::toView(double x, double y) const {
        const Rect client = getClientBounds();
        return BLPoint(double(client.left()) + panX_ + x * zoom_, double(client.top()) + panY_ + y * zoom_);
    }

    BLPoint GraphView::toGraph(const Point& viewPoint) const {
        const Rect client = getClientBounds();
        return BLPoint((double(viewPoint.x) - double(client.left()) - panX_) / zoom_, (double(viewPoint.y) - double(client.top()) - panY_) / zoom_);
    }

    const GraphView::Box* GraphView::boxAt(const std::vector<Box>& all, const BLPoint& p) const {
        for (auto it = all.rbegin(); it != all.rend(); ++it) {
            if (p.x >= it->x && p.x <= it->x + it->w && p.y >= it->y && p.y <= it->y + it->h) {
                return &*it;
            }
        }
        return nullptr;
    }

    std::vector<BLRect> GraphView::nodeRects() const {
        std::vector<BLRect> rects;
        const jitnodes::Network* network = document_ != nullptr ? document_->findNetwork(scope_) : nullptr;
        if (network != nullptr) {
            for (const Box& box : boxes(*network)) {
                rects.push_back(BLRect(box.x, box.y, box.w, box.h));
            }
        }
        return rects;
    }

    BLRect GraphView::visibleRect() const {
        const Rect client = getClientBounds();
        const BLPoint topLeft = toGraph(Point(client.left(), client.top()));
        return BLRect(topLeft.x, topLeft.y, double(client.width()) / zoom_, double(client.height()) / zoom_);
    }

    void GraphView::centerOn(double graphX, double graphY) {
        const Rect client = getClientBounds();
        panX_ = double(client.width()) * 0.5 - graphX * zoom_;
        panY_ = double(client.height()) * 0.5 - graphY * zoom_;
        fitted_ = true;
        style().markDirty();
    }

    void GraphView::fit() {
        fitView();
        style().markDirty();
        if (onViewChanged) {
            onViewChanged();
        }
    }

    void GraphView::fitView() {
        fitted_ = true;
        const jitnodes::Network* network = document_ != nullptr ? document_->findNetwork(scope_) : nullptr;
        const Rect client = getClientBounds();
        if (network == nullptr || network->nodeIds().empty() || client.width() < 10.0f || client.height() < 10.0f) {
            zoom_ = 1.0;
            panX_ = 20.0;
            panY_ = 30.0;
            return;
        }
        double left = 1e9, top = 1e9, right = -1e9, bottom = -1e9;
        for (const Box& box : boxes(*network)) {
            left = (std::min)(left, box.x);
            top = (std::min)(top, box.y);
            right = (std::max)(right, box.x + box.w);
            bottom = (std::max)(bottom, box.y + box.h);
        }
        constexpr double margin = 24.0;
        const double availW = double(client.width()) - 2.0 * margin, availH = double(client.height()) - 2.0 * margin - 16.0;
        zoom_ = std::clamp((std::min)(availW / (right - left), availH / (bottom - top)), 0.1, 1.0);
        panX_ = margin + (availW - (right - left) * zoom_) * 0.5 - left * zoom_;
        panY_ = margin + 16.0 + (availH - (bottom - top) * zoom_) * 0.5 - top * zoom_;
    }

    BLPoint GraphView::portPoint(const Box& box, int input) const {
        return input < 0 ? BLPoint(box.x + box.w, box.y + box.h * 0.5) : BLPoint(box.x, box.y + kPortTop + kPortStep * input);
    }

    GraphView::Port GraphView::portAt(const std::vector<Box>& all, const BLPoint& p, bool inputsOnly) const {
        const double reach = (std::max)(7.0, 9.0 / zoom_);
        for (auto it = all.rbegin(); it != all.rend(); ++it) {
            const int inputs = document_->node(NodeRef(scope_, it->id)).inputCount();
            for (int i = inputsOnly ? 0 : -1; i < inputs; ++i) {
                const BLPoint at = portPoint(*it, i);
                if (std::hypot(p.x - at.x, p.y - at.y) <= reach) {
                    return Port{it->id, i};
                }
            }
        }
        return Port{};
    }

    void GraphView::say(const std::string& text) const {
        if (onMessage) {
            onMessage(text);
        }
    }

    SyncReturn GraphView::handleMouseDown(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        if (RootView* root = rootView()) {
            root->setFocusedSubView(this);
        }
        last_ = pt;
        const jitnodes::Network* network = document_ != nullptr ? document_->findNetwork(scope_) : nullptr;
        if (network == nullptr) {
            return SyncReturn::Handled;
        }
        const std::vector<Box> all = boxes(*network);
        const bool left = (buttons & mbmLeftButton) != 0;
        const bool extend = (keys & (kmShift | kmCtrl)) != 0;
        if (left) {
            const Port port = portAt(all, toGraph(pt), false);
            if (port.valid() && port.input < 0) {
                wiring_ = true;
                wireSource_ = port.id;
                wireOldTo_ = jitnodes::invalidNodeId;
                wireEnd_ = pt;
                return SyncReturn::Handled;
            }
            if (port.valid() && network->inputOf(port.id, port.input) != jitnodes::invalidNodeId) {
                wiring_ = true;
                wireSource_ = network->inputOf(port.id, port.input);
                wireOldTo_ = port.id;
                wireOldInput_ = port.input;
                wireEnd_ = pt;
                style().markDirty();
                return SyncReturn::Handled;
            }
        }
        const Box* hit = left || (buttons & mbmRightButton) != 0 ? boxAt(all, toGraph(pt)) : nullptr;
        if (hit == nullptr) {
            if (left && extend) {
                marquee_ = true;
                marqueeStart_ = pt;
                wireEnd_ = pt;
                return SyncReturn::Handled;
            }
            panning_ = true;
            if (left) {
                select(NodeRef());
            }
            return SyncReturn::Handled;
        }
        const NodeRef ref(scope_, hit->id);
        const auto inSelection = std::find(selection_.begin(), selection_.end(), ref);
        if (left && extend) {
            if (inSelection != selection_.end()) {
                selection_.erase(inSelection);
                selected_ = selection_.empty() ? NodeRef() : selection_.back();
            } else {
                selection_.push_back(ref);
                selected_ = ref;
            }
            style().markDirty();
            if (onSelectionChanged) {
                onSelectionChanged();
            }
            return SyncReturn::Handled;
        }
        if (inSelection == selection_.end()) {
            select(ref);
        } else if (selected_ != ref) {
            selected_ = ref;   // pressing a box of a larger selection keeps it, so the group can be dragged
            style().markDirty();
            if (onSelectionChanged) {
                onSelectionChanged();
            }
        }
        if (left) {
            dragId_ = hit->id;
            dragMoved_ = false;
            dragPress_ = pt;
            dragOrigins_.clear();
            for (const Box& box : all) {
                if (std::find(selection_.begin(), selection_.end(), NodeRef(scope_, box.id)) != selection_.end()) {
                    dragOrigins_[box.id] = BLPoint(box.x, box.y);
                }
            }
        }
        return SyncReturn::Handled;
    }

    void GraphView::endWire(const Point& pt) {
        const jitnodes::Network* network = document_ != nullptr ? document_->findNetwork(scope_) : nullptr;
        const jitnodes::NodeId source = wireSource_, oldTo = wireOldTo_;
        const int oldInput = wireOldInput_;
        wiring_ = false;
        wireSource_ = wireOldTo_ = jitnodes::invalidNodeId;
        wireOldInput_ = -1;
        style().markDirty();
        if (network == nullptr) {
            return;
        }
        const Port target = portAt(boxes(*network), toGraph(pt), true);
        try {
            if (oldTo != jitnodes::invalidNodeId) {
                if (!target.valid()) {
                    document_->disconnectGraphNode(NodeRef(scope_, oldTo), oldInput);
                } else if (target.id != oldTo || target.input != oldInput) {
                    document_->moveGraphWire(NodeRef(scope_, oldTo), oldInput, NodeRef(scope_, target.id), target.input);
                }
            } else if (target.valid()) {
                document_->connectGraphNodes(NodeRef(scope_, source), NodeRef(scope_, target.id), target.input);
            }
        } catch (const std::exception& e) {
            say(std::string("Cannot wire: ") + e.what());
        }
    }

    void GraphView::endMarquee() {
        marquee_ = false;
        style().markDirty();
        const jitnodes::Network* network = document_ != nullptr ? document_->findNetwork(scope_) : nullptr;
        if (network == nullptr) {
            return;
        }
        const BLPoint a = toGraph(marqueeStart_), b = toGraph(wireEnd_);
        const double left = (std::min)(a.x, b.x), right = (std::max)(a.x, b.x), top = (std::min)(a.y, b.y), bottom = (std::max)(a.y, b.y);
        bool changed = false;
        for (const Box& box : boxes(*network)) {
            const NodeRef ref(scope_, box.id);
            if (box.x <= right && box.x + box.w >= left && box.y <= bottom && box.y + box.h >= top &&
                std::find(selection_.begin(), selection_.end(), ref) == selection_.end()) {
                selection_.push_back(ref);
                selected_ = ref;
                changed = true;
            }
        }
        if (changed && onSelectionChanged) {
            onSelectionChanged();
        }
    }

    bool GraphView::deleteSelected() {
        if (document_ == nullptr) {
            return false;
        }
        std::vector<NodeRef> doomed;
        for (const NodeRef& ref : selection_) {
            if (ref.scope == scope_ && document_->hasNode(ref)) {
                doomed.push_back(ref);
            }
        }
        if (doomed.empty()) {
            return false;
        }
        try {
            document_->removeGraphNodes(doomed);
        } catch (const std::exception& e) {
            say(std::string("Cannot delete: ") + e.what());
            return false;
        }
        select(NodeRef());
        say(doomed.size() == 1 ? "Node deleted" : std::to_string(doomed.size()) + " nodes deleted");
        return true;
    }

    void GraphView::endDrag() {
        if (dragMoved_ && dragBefore_ && document_ != nullptr) {
            document_->commitGraphEdit(*dragBefore_, "Move Node");
        }
        dragId_ = jitnodes::invalidNodeId;
        dragMoved_ = false;
        dragBefore_.reset();
    }

    SyncReturn GraphView::handleMouseDoubleClick(View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
        const jitnodes::Network* network = document_ != nullptr ? document_->findNetwork(scope_) : nullptr;
        if (network == nullptr || (buttons & mbmLeftButton) == 0) {
            return SyncReturn::Handled;
        }
        const std::vector<Box> all = boxes(*network);
        if (const Box* hit = boxAt(all, toGraph(pt))) {
            enter(NodeRef(scope_, hit->id));
        }
        return SyncReturn::Handled;
    }

    SyncReturn GraphView::handleMouseMove(View&, const Point& pt, std::uint32_t buttons, std::uint32_t) {
        if (wiring_ || marquee_) {
            if ((buttons & mbmLeftButton) == 0) {
                if (wiring_) {
                    endWire(pt);
                } else {
                    endMarquee();
                }
            } else {
                wireEnd_ = pt;
                style().markDirty();
            }
            last_ = pt;
            return SyncReturn::Handled;
        }
        if (dragId_ != jitnodes::invalidNodeId) {
            const jitnodes::Network* network = document_ != nullptr ? document_->findNetwork(scope_) : nullptr;
            if ((buttons & mbmLeftButton) == 0 || network == nullptr || !network->hasNode(dragId_)) {
                endDrag();
            } else {
                constexpr double kClickTolerance = 4.0;
                const double dx = double(pt.x - dragPress_.x), dy = double(pt.y - dragPress_.y);
                if (!dragMoved_ && std::hypot(dx, dy) >= kClickTolerance) {
                    dragMoved_ = true;
                    dragBefore_ = document_->snapshotGraph();
                    // Unplaced nodes are laid out by column, so moving one would make the rest jump: the first move places them all where they are.
                    std::map<jitnodes::NodeId, std::pair<float, float>> placed;
                    for (const Box& box : boxes(*network)) {
                        placed[box.id] = {float(box.x), float(box.y)};
                    }
                    document_->setGraphPositionsLive(scope_, placed);
                }
                if (dragMoved_) {
                    std::map<jitnodes::NodeId, std::pair<float, float>> moved;
                    for (const auto& [id, origin] : dragOrigins_) {
                        moved[id] = {float(origin.x + dx / zoom_), float(origin.y + dy / zoom_)};
                    }
                    document_->setGraphPositionsLive(scope_, moved);
                    style().markDirty();
                }
            }
            last_ = pt;
            return SyncReturn::Handled;
        }
        if (panning_) {
            if ((buttons & (mbmLeftButton | mbmRightButton | mbmMiddleButton)) != 0) {
                panX_ += double(pt.x - last_.x);
                panY_ += double(pt.y - last_.y);
                style().markDirty();
            } else {
                panning_ = false;
            }
        }
        last_ = pt;
        return SyncReturn::Handled;
    }

    SyncReturn GraphView::handleMouseUp(View&, const Point& pt, std::uint32_t, std::uint32_t) {
        if (wiring_) {
            endWire(pt);
        }
        if (marquee_) {
            wireEnd_ = pt;
            endMarquee();
        }
        endDrag();
        panning_ = false;
        return SyncReturn::Handled;
    }

    SyncReturn GraphView::handleMouseWheel(View&, const Point& pt, float delta) {
        const BLPoint before = toGraph(pt);
        zoom_ = std::clamp(zoom_ * std::pow(1.15, double(delta) / 120.0), 0.1, 4.0);
        const BLPoint after = toView(before.x, before.y);
        panX_ += double(pt.x) - after.x;
        panY_ += double(pt.y) - after.y;
        fitted_ = true;
        style().markDirty();
        if (onViewChanged) {
            onViewChanged();
        }
        return SyncReturn::Handled;
    }

    SyncReturn GraphView::handleKeyDown(View&, std::uint32_t keyMask, int, int, std::uint32_t vkCode) {
        if (onKeyFilter && onKeyFilter(keyMask, vkCode)) {
            return SyncReturn::Handled;
        }
        if (vkCode == vkTab && onCloseRequested) {
            onCloseRequested();
            return SyncReturn::Handled;
        }
        if (vkCode == vkBackSpace) {
            return exit() ? SyncReturn::Handled : SyncReturn::Ignored;
        }
        if (vkCode == vkDelete) {
            deleteSelected();
            return SyncReturn::Handled;
        }
        if (vkCode == vkLetterF) {
            fit();
            style().markDirty();
            return SyncReturn::Handled;
        }
        return SyncReturn::Ignored;
    }

    void GraphView::paintNode(BLContext& ctx, const jitnodes::Network& network, const Box& box) const {
        const Theme& theme = Theme::current();
        const jitnodes::Node& node = network.node(box.id);
        const NodeRef self(scope_, box.id);
        const bool isSelected = std::find(selection_.begin(), selection_.end(), self) != selection_.end();
        const bool isSubnet = dynamic_cast<const jitnodes::SubnetNode*>(&node) != nullptr;
        const BLPoint topLeft = toView(box.x, box.y);
        const double w = box.w * zoom_, h = box.h * zoom_;
        const std::uint32_t alpha = node.bypassed() ? 110u : 255u;

        ctx.set_fill_style(BLRgba32(Theme::argb(isSubnet ? theme.slot : theme.bgSurface, alpha)));
        ctx.fill_round_rect(BLRoundRect(topLeft.x, topLeft.y, w, h, 5.0 * zoom_));
        const bool isDisplay = network.displayNode() == box.id;
        ctx.set_stroke_style(BLRgba32(Theme::argb(isSelected || isDisplay ? theme.accent : theme.border, alpha)));
        ctx.set_stroke_width(isSelected ? 2.0 : 1.0);
        ctx.stroke_round_rect(BLRoundRect(topLeft.x, topLeft.y, w, h, 5.0 * zoom_));

        for (int i = 0; i < node.inputCount(); ++i) {
            const bool wired = network.inputOf(box.id, i) != jitnodes::invalidNodeId;
            const BLPoint port = toView(box.x, box.y + kPortTop + kPortStep * i);
            ctx.set_fill_style(BLRgba32(Theme::argb(wired ? theme.accent : theme.textMuted)));
            ctx.fill_circle(BLCircle(port.x, port.y, 3.5 * zoom_));
        }
        const BLPoint out = toView(box.x + box.w, box.y + box.h * 0.5);
        ctx.set_fill_style(BLRgba32(Theme::argb(theme.accent)));
        ctx.fill_circle(BLCircle(out.x, out.y, 3.5 * zoom_));

        if (zoom_ < 0.35) {
            return;
        }
        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(float(12.0 * zoom_));
        Font tiny = FontManager::getSystemFont(SystemUIFont::Message);
        tiny.setSize(float(10.0 * zoom_));
        BLFont* blFont = font.blFont();
        BLFont* blSmall = tiny.blFont();
        if (blFont == nullptr || !blFont->is_valid() || blSmall == nullptr || !blSmall->is_valid()) {
            return;
        }
        ctx.save();
        ctx.clip_to_rect(BLRect(topLeft.x + 10.0 * zoom_, topLeft.y, w - 20.0 * zoom_, h));
        ctx.set_fill_style(BLRgba32(Theme::argb(theme.textPrimary, alpha)));
        ctx.fill_utf8_text(BLPoint(topLeft.x + 12.0 * zoom_, topLeft.y + 14.0 * zoom_), *blFont, node.name().c_str(), node.name().size());
        ctx.set_fill_style(BLRgba32(Theme::argb(theme.textMuted, alpha)));
        const std::string kind = node.typeName() + (node.frozen() ? " (frozen)" : "");
        ctx.fill_utf8_text(BLPoint(topLeft.x + 12.0 * zoom_, topLeft.y + h - 8.0 * zoom_), *blSmall, kind.c_str(), kind.size());
        ctx.restore();
    }

    void GraphView::paint(BLContext& ctx) {
        const Theme& theme = Theme::current();
        const Rect client = getClientBounds();
        ctx.set_fill_style(BLRgba32(Theme::argb(theme.bgCanvas)));
        ctx.fill_rect(BLRect(client.left(), client.top(), client.width(), client.height()));
        if (document_ == nullptr) {
            return;
        }
        if (document_->findNetwork(scope_) == nullptr) {
            scope_ = 0;
            fitted_ = false;
        }
        const jitnodes::Network& network = document_->networkOf(scope_);
        if (!fitted_) {
            fitView();
        }
        ctx.save();
        ctx.clip_to_rect(BLRect(client.left(), client.top(), client.width(), client.height()));
        const std::vector<Box> all = boxes(network);
        std::map<jitnodes::NodeId, const Box*> byId;
        for (const Box& box : all) {
            byId[box.id] = &box;
        }

        ctx.set_stroke_width(1.5);
        for (const Box& box : all) {
            for (int i = 0; i < network.node(box.id).inputCount(); ++i) {
                const jitnodes::NodeId from = network.inputOf(box.id, i);
                if (from == jitnodes::invalidNodeId || byId.count(from) == 0 || (wiring_ && box.id == wireOldTo_ && i == wireOldInput_)) {
                    continue;
                }
                const Box& source = *byId[from];
                const BLPoint a = toView(source.x + source.w, source.y + source.h * 0.5);
                const BLPoint b = toView(box.x, box.y + kPortTop + kPortStep * i);
                const double pull = (std::max)(30.0, std::abs(b.x - a.x) * 0.5);
                BLPath wire;
                wire.move_to(a);
                wire.cubic_to(BLPoint(a.x + pull, a.y), BLPoint(b.x - pull, b.y), b);
                ctx.set_stroke_style(BLRgba32(Theme::argb(theme.textMuted, 200)));
                ctx.stroke_path(wire);
            }
        }
        for (const Box& box : all) {
            paintNode(ctx, network, box);
        }
        if (wiring_ && byId.count(wireSource_) != 0) {
            const Box& source = *byId[wireSource_];
            const BLPoint a = toView(source.x + source.w, source.y + source.h * 0.5);
            const Port target = portAt(all, toGraph(wireEnd_), true);
            BLPoint b(double(wireEnd_.x), double(wireEnd_.y));
            if (target.valid()) {
                const BLPoint at = portPoint(*byId[target.id], target.input);
                b = toView(at.x, at.y);
                ctx.set_stroke_style(BLRgba32(Theme::argb(theme.accent)));
                ctx.set_stroke_width(2.0);
                ctx.stroke_circle(BLCircle(b.x, b.y, 7.0 * zoom_));
            }
            const double pull = (std::max)(30.0, std::abs(b.x - a.x) * 0.5);
            BLPath wire;
            wire.move_to(a);
            wire.cubic_to(BLPoint(a.x + pull, a.y), BLPoint(b.x - pull, b.y), b);
            ctx.set_stroke_style(BLRgba32(Theme::argb(theme.accent)));
            ctx.set_stroke_width(2.0);
            ctx.stroke_path(wire);
        }
        if (marquee_) {
            const double left = (std::min)(double(marqueeStart_.x), double(wireEnd_.x)), top = (std::min)(double(marqueeStart_.y), double(wireEnd_.y));
            const double w = std::abs(double(wireEnd_.x - marqueeStart_.x)), h = std::abs(double(wireEnd_.y - marqueeStart_.y));
            ctx.set_fill_style(BLRgba32(Theme::argb(theme.accent, 40)));
            ctx.fill_rect(BLRect(left, top, w, h));
            ctx.set_stroke_style(BLRgba32(Theme::argb(theme.accent)));
            ctx.set_stroke_width(1.0);
            ctx.stroke_rect(BLRect(left, top, w, h));
        }

        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(11.0f);
        if (BLFont* blFont = font.blFont(); blFont != nullptr && all.empty() && blFont->is_valid()) {
            const std::string text = "(empty)";
            ctx.set_fill_style(BLRgba32(Theme::argb(theme.textMuted)));
            ctx.fill_utf8_text(BLPoint(double(client.left()) + 8.0, double(client.top()) + 14.0), *blFont, text.c_str(), text.size());
        }
        // The preview and the minimap are painted after this (they are children), so their shadows go down now.
        for (const char* overlay : {"graphPreview", "graphMiniMap"}) {
            if (const View* box = findView(overlay); box != nullptr && box->isVisible()) {
                const Rect b = box->bounds();
                GraphOverlayChrome::paintShadow(ctx, BLRect(double(b.left()), double(b.top()), double(b.width()), double(b.height())));
            }
        }
        ctx.restore();
    }

}
