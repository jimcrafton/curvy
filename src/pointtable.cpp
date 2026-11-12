#include "curvy/pointtable.h"

#include "curvy/theme.h"

#include <newui/clipboardmgr.h>
#include <newui/models.h>

#include <jitnodes/graph/network.h>

#include <memory>

namespace curvy {

    using namespace newui;

    PointTable::PointTable(View& root, SketchDocument& document) : root_(root), document_(document) {
        tabs_ = dynamic_cast<SegmentedControl*>(find("pointTableTabs"));
        sourceList_ = dynamic_cast<DropDownList*>(find("pointTableSource"));
        filterField_ = dynamic_cast<TextField*>(find("pointTableFilter"));
        if (filterField_ != nullptr) {
            filterField_->caret().setColor(Theme::current().textColor());
        }
        grid_ = dynamic_cast<TableView*>(find("pointTableGrid"));
        title_ = dynamic_cast<Label*>(find("pointTableTitle"));
        info_ = dynamic_cast<Label*>(find("pointTableInfo"));
        status_ = dynamic_cast<Label*>(find("pointTableStatus"));
        if (tabs_ == nullptr || sourceList_ == nullptr || filterField_ == nullptr || grid_ == nullptr) {
            throw std::runtime_error("PointTable: pointTable.newui is missing its tabs, source list, filter or grid");
        }
        updating_ = true;
        tabs_->setSegments({"Points", "Primitives", "Detail"});
        updating_ = false;
        tabs_->onSelectionChanged += [this](SegmentedControl&) {
            if (!updating_) {
                refresh();
            }
            return SyncReturn::Handled;
        };
        sourceList_->onSelectionChanged += [this](DropDownList&) {
            if (!updating_ && sourceList_->selectedIndex().has_value()) {
                source_ = *sourceList_->selectedIndex();
                refresh();
            }
            return SyncReturn::Handled;
        };
        filterField_->onReturnPressed += [this](TextField&) {
            const std::wstring text = filterField_->text();
            setFilter(std::string(text.begin(), text.end()));
            return SyncReturn::Handled;
        };
        grid_->onSelectionChanged += [this](TableView&) {
            showStatus();
            return SyncReturn::Handled;
        };
        if (auto* copy = dynamic_cast<Button*>(find("pointTableCopyButton"))) {
            copy->onClick += [this](Control&) {
                copySelection();
                return SyncReturn::Handled;
            };
        }
        root_.setVisible(false);
    }

    View* PointTable::find(const char* name) const {
        return root_.findView(name);
    }

    std::size_t PointTable::sourceCount() const {
        if (!node_.valid() || !document_.hasNode(node_)) {
            return 0;
        }
        return std::size_t(document_.node(node_).inputCount()) + 1;
    }

    void PointTable::show(const NodeRef& node) {
        node_ = node;
        source_ = 0;
        filter_.clear();
        updating_ = true;
        filterField_->setText(L"");
        auto sources = std::make_unique<StringListModel>();
        sources->addItem("Output");
        if (node_.valid() && document_.hasNode(node_)) {
            for (int i = 0; i < document_.node(node_).inputCount(); ++i) {
                sources->addItem("Input " + std::to_string(i));
            }
        }
        sourceList_->setModel(std::move(sources));
        sourceList_->setSelectedIndex(0);
        updating_ = false;
        root_.setVisible(true);
        refresh();
    }

    void PointTable::hide() {
        root_.setVisible(false);
        if (onHidden) {
            onHidden();
        }
    }

    void PointTable::setKind(GeometryTableModel::Kind kind) {
        tabs_->setSelectedIndex(std::size_t(kind));
        refresh();
    }

    void PointTable::setSource(std::size_t source) {
        source_ = source;
        updating_ = true;
        sourceList_->setSelectedIndex(source);
        updating_ = false;
        refresh();
    }

    void PointTable::setFilter(const std::string& filter) {
        filter_ = filter;
        refresh();
    }

    void PointTable::refresh() {
        jitnodes::GeometryHandle geometry;
        std::string problem;
        if (node_.valid() && document_.hasNode(node_)) {
            try {
                const jitnodes::Network& net = document_.networkOf(node_.scope);
                if (source_ == 0) {
                    geometry = document_.cook(node_);
                } else {
                    const jitnodes::NodeId from = net.inputOf(node_.id, int(source_) - 1);
                    if (from != jitnodes::invalidNodeId) {
                        geometry = document_.cook(NodeRef(node_.scope, from));
                    }
                }
            } catch (const std::exception& error) {
                problem = error.what();
            }
        }
        const auto kind = GeometryTableModel::Kind(tabs_->selectedIndex());
        auto model = std::make_unique<GeometryTableModel>(geometry, kind, filter_);
        model_ = model.get();
        grid_->setModel(std::move(model));
        grid_->clearSelection();
        // The loader adds the grid straight into the scroll view's internal viewport, so the scroll view never hears the grid's content size
        // change; it is asked to re-measure here so its bars follow the new row count.
        if (auto* scroll = dynamic_cast<ScrollView*>(find("pointTableScroll"))) {
            scroll->updateLayout();
        }

        if (title_ != nullptr) {
            title_->setText(kind == GeometryTableModel::Kind::Points ? "Points" : kind == GeometryTableModel::Kind::Primitives ? "Primitives" : "Detail");
        }
        if (info_ != nullptr) {
            const std::string name = node_.valid() && document_.hasNode(node_) ? document_.node(node_).name() : "";
            info_->setText(geometry == nullptr ? name + (problem.empty() ? " - no geometry" : " - " + problem)
                                                : name + " - " + std::to_string(geometry->pointCount()) + " points, " + std::to_string(geometry->primitiveCount()) + " primitives");
        }
        if (status_ != nullptr) {
            status_->setText("");
        }
        grid_->style().markDirty();
    }

    void PointTable::showStatus() {
        if (status_ == nullptr || model_ == nullptr || !grid_->hasActiveCell()) {
            return;
        }
        const std::size_t row = grid_->activeRow(), column = grid_->activeColumn();
        status_->setText("Row " + std::to_string(row) + ", " + model_->columnTitle(column) + " = " + model_->cellText(row, column));
    }

    std::string PointTable::copySelection() {
        if (model_ == nullptr || model_->rowCount() == 0) {
            return "";
        }
        std::size_t firstRow = 0, lastRow = model_->rowCount() - 1, firstColumn = 0, lastColumn = model_->columnCount() - 1;
        if (const auto range = grid_->selectedRange()) {
            firstRow = range->firstRow;
            lastRow = range->lastRow;
            firstColumn = range->firstColumn;
            lastColumn = range->lastColumn;
        }
        const std::string text = model_->asText(firstRow, lastRow, firstColumn, lastColumn);
        ClipboardManager::setText(std::wstring(text.begin(), text.end()));
        if (status_ != nullptr) {
            status_->setText("Copied " + std::to_string(lastRow - firstRow + 1) + " rows");
        }
        return text;
    }

}
