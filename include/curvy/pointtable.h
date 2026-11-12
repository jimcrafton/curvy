#pragma once

#include "curvy/geometrytable.h"
#include "curvy/noderef.h"
#include "curvy/sketchdocument.h"

#include <newui/controls.h>
#include <newui/segmentedcontrol.h>
#include <newui/view.h>

#include <functional>
#include <string>

namespace curvy {

    // The data popup of the graph view (Resources/pointTable.newui from docs/mockups/graphPointTable.html): the geometry a node cooks, as a
    // read-only table of its points, primitives or detail values. show() reads the node's geometry once; the table does not follow later edits
    // until it is shown again. Sources are the node's output and each of its inputs.
    // @reflect ignore=true
    class PointTable {
    public:
        // `root` is the loaded fragment; it is hidden until show().
        PointTable(newui::View& root, SketchDocument& document);
        PointTable(const PointTable&) = delete;
        PointTable& operator=(const PointTable&) = delete;

        void show(const NodeRef& node);
        void hide();
        bool shown() const { return root_.isVisible(); }
        newui::View* root() const { return &root_; }

        // Tabs, source and filter as the user can set them.
        void setKind(GeometryTableModel::Kind kind);
        // 0 is the node's output, i + 1 its input i.
        void setSource(std::size_t source);
        void setFilter(const std::string& filter);

        const GeometryTableModel* model() const { return model_; }
        std::size_t sourceCount() const;
        // Copies the selected cells (everything when none is selected) as tab separated text; returns the text.
        std::string copySelection();

        std::function<void()> onHidden;

    private:
        void refresh();
        void showStatus();
        newui::View* find(const char* name) const;

        newui::View& root_;
        SketchDocument& document_;
        NodeRef node_;
        std::size_t source_ = 0;
        std::string filter_;
        GeometryTableModel* model_ = nullptr;
        newui::SegmentedControl* tabs_ = nullptr;
        newui::DropDownList* sourceList_ = nullptr;
        newui::TextField* filterField_ = nullptr;
        newui::TableView* grid_ = nullptr;
        newui::Label* title_ = nullptr;
        newui::Label* info_ = nullptr;
        newui::Label* status_ = nullptr;
        bool updating_ = false;
    };

}
