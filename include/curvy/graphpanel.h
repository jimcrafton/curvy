#pragma once

#include "curvy/colorpickercard.h"
#include "curvy/noderef.h"
#include "curvy/numberscrubber.h"
#include "curvy/sketchdocument.h"
#include "curvy/swatchpalettecontroller.h"

#include <newui/controls.h>
#include <newui/view.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // The property panel on the right of the graph view (the "graphProps" part of Resources/graphView.newui): the selected node's name, type and
    // tags, its Display / Bypass / Freeze buttons, every parameter as an editable row in a scroll view, and what feeds it and what uses it. A
    // node's r, g and b are shown as one colour row (a swatch and a #RRGGBB field). Enter in the name or a field commits it as one undo step;
    // dragging sideways on a number's caption scrubs it (one undo step per drag). The buttons only report a command; the owner runs it.
    // @reflect ignore=true
    class GraphPanel {
    public:
        GraphPanel(newui::View& root, SketchDocument& document);
        GraphPanel(const GraphPanel&) = delete;
        GraphPanel& operator=(const GraphPanel&) = delete;

        // Shows this node; an invalid ref shows an empty panel.
        void show(const NodeRef& node);
        // Puts the keyboard in the name field (what the ring's Rename does).
        void focusName();

        // "graph.display", "graph.bypass" or "graph.freeze" from the buttons.
        std::function<void(const std::string& command)> onCommand;
        // Called after the panel changed the document (a rename or a number), so the owner can refresh the other views.
        std::function<void()> onEdited;
        // Called after an edit was refused (the text says why).
        std::function<void(const std::string& message)> onRefused;

        // What the panel shows, for tests.
        std::string title() const;
        std::string kind() const;
        std::string name() const;
        std::size_t paramCount() const { return entries_.size(); }
        std::string paramCaption(std::size_t row) const;
        std::string paramValue(std::size_t row) const;
        bool paramShown(std::size_t row) const;
        // Types into a row's field and commits it, as Enter does.
        void setParam(std::size_t row, const std::string& text);
        void rename(const std::string& text);
        // Sets the node's tags from words separated by spaces, as one undo step; the panel shows the node as it is afterwards (a refused edit
        // goes to onRefused and the field goes back).
        void retag(const std::string& text);

        // The colour card a colour row's swatch opens (a floating box in the node area: wheel, RGB / HSB / CMYK / Gray sliders, alpha when the colour has
        // one, hex); clicking the swatch again closes it. It edits the row's r, g, b (and alpha) live and makes each gesture one undo step. Closed when
        // another node is shown.
        void toggleColorPicker(std::size_t row);
        void closeColorPicker();
        bool colorPickerOpen() const;
        ColorPickerCard* colorPicker() const { return picker_; }
        // Applies the index'th recent colour (0 is the newest) to the open card's colour, as a swatch click does (keys 1 to 8). False when the
        // card is closed or there are fewer recent colours.
        bool useRecentColor(std::size_t index);
        // The card's palettes: the document's, plus the automatic Recent (the colours the card applied this session).
        SwatchPaletteController* swatches() const { return swatches_.get(); }

        // "#RRGGBB" (or "#RRGGBBAA" with an alpha) for channels in 0..1, and back; false when the text is not a colour. parseColor sets hasAlpha
        // for the 8 digit form and leaves `a` alone otherwise.
        static std::string colorText(float r, float g, float b);
        static std::string colorText(float r, float g, float b, float a);
        static bool parseColor(const std::string& text, float& r, float& g, float& b, float& a, bool& hasAlpha);

    private:
        // One shown row: a number (name is the parameter), or a colour made of the parameters tagged "group:<name>" and "kind:color" (name is the
        // group; r, g, b and a are the parameters tagged channel:r|g|b|a, a may be empty). unit comes from the parameter's "unit:" tag, the
        // optional limits from "min:" and "max:" (a typed or scrubbed number is kept within them).
        struct Entry {
            std::string name;
            bool color = false;
            double unit = 1.0;
            std::optional<double> minimum, maximum;
            std::string r, g, b, a;
        };
        struct Row {
            newui::Label* caption = nullptr;
            newui::TextField* field = nullptr;
            newui::SubView* swatch = nullptr;
            std::shared_ptr<NumberScrubber> scrubber;
        };

        newui::Label* label(const std::string& name) const;
        newui::TextField* field(const std::string& name) const;
        static std::string narrow(const std::wstring& text);
        static std::wstring widen(const std::string& text);
        void commitName();
        void commitTags();
        void commitParam(std::size_t row);
        void makeRow();
        void layoutRows();
        void fillRow(std::size_t row);
        static double clampTo(const Entry& entry, double value);
        static std::string limitText(const Entry& entry);
        void syncPicker();
        void makePicker();
        void applyPicker();
        // Removes the swatch ties (SwatchLinks::tag) of the colour row `group` from the shown node.
        void dropLinkTags(const std::string& group);
        std::uint32_t pendingLink_ = 0;   // the linked swatch being used, so the end of its gesture ties the colour to it

        newui::View& root_;
        ColorPickerCard* picker_ = nullptr;
        std::unique_ptr<SwatchPaletteController> swatches_;
        std::size_t pickerRow_ = 0;
        std::optional<jitnodes::GraphSnapshot> pickerBefore_;
        SketchDocument& document_;
        NodeRef node_;
        std::vector<Entry> entries_;
        std::vector<Row> rows_;   // grows to the most parameters any node has shown; the unused ones are hidden
        newui::ScrollView* scroll_ = nullptr;
        newui::SubView* viewport_ = nullptr;
        std::optional<jitnodes::GraphSnapshot> scrubBefore_;
    };

}
