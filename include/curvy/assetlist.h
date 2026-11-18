#pragma once

#include <newui/controllers.h>
#include <newui/items.h>
#include <newui/models.h>

#include <cstddef>
#include <string>
#include <vector>

namespace curvy {

    // One row of the Assets panel's library list: a group heading (only `name`, the title) or an asset.
    // @reflect ignore=true
    struct AssetListRow {
        bool heading = false;
        std::string name;
        int version = 0;
        std::vector<std::string> badges;   // "Brush", "Outdated 2", ...
    };

    // What the library list shows. value(index) is the row's name (the heading's title), so the stock heading item and anything that reads text work;
    // AssetRowItem reads the whole row.
    // @reflect ignore=true
    class AssetListModel : public newui::ListModel {
    public:
        void setRows(std::vector<AssetListRow> rows);
        const AssetListRow* row(std::size_t index) const { return index < rows_.size() ? &rows_[index] : nullptr; }

        void clear() override;
        bool empty() const override { return rows_.empty(); }
        std::size_t size() const override { return rows_.size(); }
        std::any value(const std::any& key = std::any()) override;

    private:
        std::vector<AssetListRow> rows_;
    };

    // An asset row: the name, the version at the right and a chip for each badge (amber for "Outdated"). Reflected, so the list's controller can make
    // it by name (setDefaultItemClassName("AssetRowItem")).
    class AssetRowItem : public newui::ListItem {
    public:
        AssetRowItem() = default;
        void paint(BLContext& ctx, const newui::Rect& rect, std::size_t index, newui::ListController& controller) override;
    };

}
