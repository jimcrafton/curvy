#pragma once

#include <jitnodes/graph/node.h>

#include <newui/controllers.h>
#include <newui/items.h>
#include <newui/models.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // One line of the graph tree: a layer or group of the document, or a node of the graph (a Link row stands for a node that is already
    // shown in full elsewhere in the Flow view). Rows are kept flat with their depth, as newui's StringTreeModel does.
    // @reflect ignore=true
    struct GraphRow {
        enum class Kind { Layer, Group, Node, Link };
        Kind kind = Kind::Node;
        std::size_t depth = 0;
        std::string text;               // the name
        std::string summary;            // shown at the right when there is no chip
        std::string icon;               // resource name, drawn before the name
        std::string key;                // stable identity, so a rebuilt tree keeps what was open
        jitnodes::NodeId node = 0;      // Node and Link rows
        std::size_t objectId = 0;       // a Node row that stands for a document object
        std::size_t layerId = 0;        // a Layer row, or the layer a Group or Node row is in
        std::size_t groupId = 0;
        bool chip = false;              // a colour chip at the right (a solid fill or stroke); chipColor is 0xRRGGBB
        std::uint32_t chipColor = 0;
        double chipAlpha = 1.0;
        bool dim = false;               // matches only through a row below it (while filtering)
    };

    // The rows as a TreeModel. Paths resolve through child lists built once per setRows(), so a graph of thousands of nodes is not
    // searched linearly for every row the tree asks about.
    // @reflect ignore=true
    class GraphTreeModel : public newui::TreeModel {
    public:
        void setRows(std::vector<GraphRow> rows);
        const std::vector<GraphRow>& rows() const { return rows_; }
        // The row a path addresses, or null.
        const GraphRow* rowAt(const std::vector<std::size_t>& path) const;
        // The path of row `index` of rows().
        std::vector<std::size_t> pathOf(std::size_t index) const;

        bool empty() const override { return rows_.empty(); }
        std::size_t size() const override { return rows_.size(); }
        std::size_t childCount(const std::vector<std::size_t>& path) const override;
        std::any value(const std::any& key = std::any()) override;

    private:
        // The index in rows_ the path addresses; rows_.size() for the root (empty path) and for a path that does not resolve.
        std::size_t indexOf(const std::vector<std::size_t>& path, bool& valid) const;

        std::vector<GraphRow> rows_;
        std::vector<std::size_t> top_;                       // the depth 0 rows
        std::vector<std::vector<std::size_t>> children_;     // per row
        std::vector<std::size_t> parent_;                    // per row (rows_.size() for a top row)
        std::vector<std::size_t> position_;                  // per row: its index among its siblings
    };

    // One row drawn: the TreeItem (triangle, icon, name) and, at the right, a colour chip over a checkerboard or the summary text.
    // @reflect ignore=true
    class GraphTreeItem : public newui::TreeItem {
    public:
        void paint(BLContext& ctx, const newui::Rect& rect, const std::vector<std::size_t>& path, newui::TreeController& controller) override;
    };

    // @reflect ignore=true
    class GraphTreeController : public newui::TreeController {
    public:
        GraphTreeController();

        GraphTreeModel* graphModel() { return dynamic_cast<GraphTreeModel*>(model()); }
        const GraphTreeModel* graphModel() const { return dynamic_cast<const GraphTreeModel*>(model()); }

        newui::TreeItem* createItem(const std::vector<std::size_t>& path) override;
        // Items are made fresh for each paint (a custom class has no reflection entry for the pool to build it from) and dropped after it.
        void releaseItem(newui::Item* item) override { delete item; }
        std::optional<std::string> iconFor(const std::vector<std::size_t>& path) const override;
        float iconSize() const override { return 16.0f; }
    };

    // The rows of the Flow view: the nodes nobody uses, each with the nodes that feed it below; a node reached a second time is a Link row.
    // `labelOf` gives a node's name; `summaryOf` the text at the right.
    // @reflect ignore=true
    struct FlowSource {
        std::vector<jitnodes::NodeId> nodes;
        std::vector<std::vector<jitnodes::NodeId>> inputs;   // parallel to nodes
    };
    std::vector<GraphRow> flowRows(const FlowSource& source, const std::function<void(GraphRow&)>& describe);

    // The rows that match `filter` (a case-insensitive part of the name or summary), with the rows above them kept so they can be found in
    // place; those kept only for that reason are dimmed. An empty filter leaves the rows as they are.
    std::vector<GraphRow> filterRows(const std::vector<GraphRow>& rows, const std::string& filter);

}
