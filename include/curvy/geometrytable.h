#pragma once

#include <jitnodes/geo/geometry.h>

#include <newui/models.h>

#include <any>
#include <string>
#include <vector>

namespace curvy {

    // What a node cooks, as a read-only table for the data popup (docs/mockups/graphPointTable.html): a row per point, per primitive, or per detail
    // value. Points: x, y, then every other point attribute in name order (vector attributes are already separate name.x, name.y columns).
    // Primitives: start, count, closed, kind, then the primitive attributes. Detail: name and value. A TableView only asks for the cells on screen,
    // so the size of the geometry does not matter. The cell text is a std::string.
    // @reflect ignore=true
    class GeometryTableModel : public newui::TableModel {
    public:
        enum class Kind { Points, Primitives, Detail };

        // Null geometry is an empty table. `filter` keeps the columns of the Points and Primitives tables whose name contains it (case-blind), the built-in
        // ones (x, y / start, count, closed, kind) included. The Detail table ignores it.
        GeometryTableModel(jitnodes::GeometryHandle geometry, Kind kind, const std::string& filter = "");

        std::size_t rowCount() const override { return rows_; }
        std::size_t columnCount() const override { return columns_.size(); }
        std::string columnTitle(std::size_t column) const override { return column < columns_.size() ? columns_[column].title : ""; }
        std::any value(const std::any& key = std::any()) override;
        // Read-only: always throws std::runtime_error.
        void setValue(const std::any& newValue, const std::any& key = std::any()) override;

        // The text of one cell without the std::any boxing; empty for a cell outside the table.
        std::string cellText(std::size_t row, std::size_t column) const;
        // Every cell of the rows and columns, tab separated with a header line, for the clipboard.
        std::string asText(std::size_t firstRow, std::size_t lastRow, std::size_t firstColumn, std::size_t lastColumn) const;

        Kind kind() const { return kind_; }

    private:
        struct Column {
            std::string title;
            const jitnodes::AttribArray* attribute = nullptr;   // null for a structural column
            int structural = -1;
        };
        static std::string number(float value);
        void addAttributes(jitnodes::AttribClass cls);
        void keepMatching(const std::string& filter);

        jitnodes::GeometryHandle geometry_;
        Kind kind_;
        std::size_t rows_ = 0;
        std::vector<Column> columns_;
        std::vector<std::pair<std::string, float>> details_;
    };

}
