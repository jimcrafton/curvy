#pragma once

#include "curvy/panel.h"
#include "curvy/selectionmodel.h"
#include "curvy/shapeproperties.h"
#include "curvy/sketchdocument.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/items.h>
#include <newui/models.h>
#include <newui/segmentedcontrol.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace curvy {

    // One row of the Select panel's value list: a value of the chosen property, how many shapes have it, a swatch for the colour properties, and
    // whether the selected shape has this value itself (the "select similar" row).
    // @reflect ignore=true
    struct SelectValueRow {
        std::string text;
        std::size_t count = 0;
        bool colour = false;
        std::uint32_t rgb = 0;
        bool own = false;
    };

    // @reflect ignore=true
    class SelectValueModel : public newui::ListModel {
    public:
        void setRows(std::vector<SelectValueRow> rows);
        const SelectValueRow* row(std::size_t index) const { return index < rows_.size() ? &rows_[index] : nullptr; }

        void clear() override;
        bool empty() const override { return rows_.empty(); }
        std::size_t size() const override { return rows_.size(); }
        std::any value(const std::any& key = std::any()) override;

    private:
        std::vector<SelectValueRow> rows_;
    };

    // A value row: swatch, the value, and the count at the right; an outline when the selected shape has the value. Reflected, so the list's
    // controller can make it by name.
    class SelectValueItem : public newui::ListItem {
    public:
        SelectValueItem() = default;
        void paint(BLContext& ctx, const newui::Rect& rect, std::size_t index, newui::ListController& controller) override;
    };

    // The Select panel of the right dock (Resources/selectPanel.newui): chooses shapes by a property instead of by area. The tabs name the
    // property (tag, fill, stroke, width, type, layer), the list shows each value the document has with its count, and a click on a value
    // selects every shape that has it (Shift or Ctrl held adds, Alt subtracts). Within narrows the values to the current selection. Add and
    // Subtract apply the value last clicked to the selection without a keyboard, and Similar selects what has the selected shape's own value of
    // the chosen property. The owner calls refresh() when the selection or the document changed.
    // @reflect ignore=true
    class SelectPanel : public Panel {
    public:
        enum class Within { Document, Selection };

        SelectPanel(SketchDocument& document, SelectionModel& selection);
        ~SelectPanel() override;

        void refresh();

        ShapeProperty property() const { return property_; }
        void setProperty(ShapeProperty property);
        Within within() const { return within_; }
        void setWithin(Within within);

        // The list as it is now, for tests and scripts.
        std::size_t rowCount() const { return model_ != nullptr ? model_->size() : 0; }
        const SelectValueRow* row(std::size_t index) const { return model_ != nullptr ? model_->row(index) : nullptr; }
        const std::string& status() const { return statusText_; }

        // What a click on a value does, with the combine the keys give. False when no shape has the value.
        bool choose(const std::string& value, SelectionModel::Combine how);
        // The last value clicked, and Add / Subtract on it.
        const std::string& chosen() const { return chosen_; }
        bool applyChosen(SelectionModel::Combine how);
        // Selects every shape that has the selected shape's own value of the chosen property; false when the selection is not one shape.
        bool selectSimilar();

        std::function<void(const std::string& message)> onMessage;
        std::function<void()> onDestroyed;

    private:
        void layoutAll();
        void rebuildShapes();
        void showStatus(const std::string& text);
        void applyKeys(const std::vector<std::string>& keys, SelectionModel::Combine how);
        std::vector<std::string> ownValues() const;

        SketchDocument& document_;
        SelectionModel& selection_;
        newui::SubView* root_ = nullptr;
        newui::SubView* tabsRow_ = nullptr;
        newui::SubView* valuesBox_ = nullptr;
        newui::SubView* withinRow_ = nullptr;
        newui::SubView* withinBox_ = nullptr;
        newui::SubView* buttons_ = nullptr;
        newui::Label* title_ = nullptr;
        newui::Label* status_ = nullptr;
        newui::SegmentedControl* tabs_ = nullptr;
        newui::SegmentedControl* withinTabs_ = nullptr;
        newui::ListView* list_ = nullptr;
        SelectValueModel* model_ = nullptr;

        ShapeProperty property_ = ShapeProperty::Fill;
        Within within_ = Within::Document;
        std::vector<ShapeValues> shapes_;   // every shape of the artwork (what Within narrows)
        std::string chosen_;
        std::string statusText_;
        bool updating_ = false;
    };

}
