#pragma once

#include "curvy/noderef.h"
#include "curvy/numberscrubber.h"
#include "curvy/panel.h"
#include "curvy/selectionmodel.h"
#include "curvy/sketchdocument.h"
#include "curvy/units.h"

#include <newui/controls.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // The Transform panel of the right dock: two tabs over the selection, Object (the transform of the layer or group the selected shape belongs to)
    // and Local (the transform node on the shape itself). It only shows and edits transform nodes that already exist in the graph; the Local tab
    // offers an Add button, and Add is the one thing that creates a node (one undo step), because making graph nodes is the user's call. Enter in a field commits
    // it as one undo step and dragging sideways on a caption scrubs it (one undo step per drag), as in the Node panel. The owner calls refresh() when the
    // selection or the graph changed. The selection model and the document must outlive the panel's use; the owner calls refresh() only while they live.
    // @reflect ignore=true
    class TransformPanel : public Panel {
    public:
        enum class Part { Object, Local };

        TransformPanel(SketchDocument& document, SelectionModel& selection);
        ~TransformPanel() override;

        // Reads the selection again and shows the nodes it maps to.
        void refresh();

        // Called after the panel changed the document, so the owner can repaint the other views.
        std::function<void()> onEdited;
        // Called after an edit was refused (the text says why).
        std::function<void(const std::string& message)> onRefused;
        // Called when the panel is destroyed (closing the window destroys the view tree before its owner).
        std::function<void()> onDestroyed;

        // What the panel shows, for tests. `param` is the node's parameter name: tx ty rot sx sy skew px py.
        bool hasNode(Part part) const { return pages_[index(part)].shown; }
        bool addButtonShown() const;
        newui::TabControl& tabs() { return *tabs_; }
        // How tall the page's content is, and whether the scrolling area that holds it has a size (so it can be seen).
        float contentHeight(Part part) const { return pages_[index(part)].contentHeight; }
        bool scrollHasSize(Part part) const;
        std::string note(Part part) const;
        std::string value(Part part, const std::string& param) const;
        // Types into a field and commits it, as Enter does.
        void setValue(Part part, const std::string& param, const std::string& text);
        // Creates the local transform of the selected shape, between its generator and what the generator fed; false when there is no shape or
        // it already has one.
        bool addLocalTransform();

    private:
        struct Row {
            const char* param;
            newui::Label* caption = nullptr;
            newui::TextField* field = nullptr;
            std::shared_ptr<NumberScrubber> scrubber;
        };
        struct Page {
            newui::TabPage* tab = nullptr;
            newui::ScrollView* scroll = nullptr;   // the rows are in it, so a short panel scrolls instead of clipping them
            float contentHeight = 0.0f;
            newui::Label* note = nullptr;
            newui::Button* add = nullptr;
            std::vector<Row> rows;
            NodeRef node;
            bool shown = false;
        };

        static std::size_t index(Part part) { return part == Part::Object ? 0 : 1; }
        std::string numberText(const std::string& param, double stored) const;
        static std::string narrow(const std::wstring& text);
        static std::wstring widen(const std::string& text);
        void buildPage(Part part, const char* title);
        void layoutParts();
        void commit(Part part, std::size_t row);
        NodeRef nodeFor(Part part) const;

        SketchDocument& document_;
        SelectionModel& selection_;
        newui::TabControl* tabs_ = nullptr;
        Page pages_[2];
        std::optional<jitnodes::GraphSnapshot> scrubBefore_;
    };

}
