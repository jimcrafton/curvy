#pragma once

#include "curvy/codeeditorview.h"
#include "curvy/colorpickercard.h"
#include "curvy/noderef.h"
#include "curvy/numberscrubber.h"
#include "curvy/panel.h"
#include "curvy/sketchdocument.h"
#include "curvy/swatchpalettecontroller.h"
#include "curvy/units.h"

#include <newui/controls.h>
#include <newui/view.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // The Node panel of the right dock (content in Resources/nodePanel.newui): the selected node's name, type and tags, its Display / Bypass /
    // Freeze buttons, every parameter as an editable row in a scroll view, and what feeds it and what uses it. A node's r, g and b are shown as
    // one colour row (a swatch and a #RRGGBB field). Enter in the name or a field commits it as one undo step; dragging sideways on a number's
    // caption scrubs it (one undo step per drag). The buttons only report a command; the owner runs it. The owner shows the node of the
    // selection (SelectionModel::primary), whether it was picked in the graph view or on the canvas.
    // The colour card a colour row opens is a child of `pickerHost` (the canvas), at its top right next to the dock.
    // @reflect ignore=true
    class NodePanel : public Panel {
    public:
        // The panel asks for the height its fixed sections need plus this many parameter rows (most nodes have fewer, and a node with more scrolls them);
        // a fixed number, so the panel does not change height as the selection changes. A shorter body lays the content out at kLeastParamRows and scrolls.
        static constexpr std::size_t kPreferredParamRows = 8;
        static constexpr std::size_t kLeastParamRows = 3;
        static constexpr float kParamRowHeight = 24.0f;
        // The height of the content with `paramRows` parameter rows (what layoutContent and the preferred height are worked out from).
        float contentHeightFor(std::size_t paramRows) const;
        // The bottom of the lowest visible section of the content, for tests: the body must be at least this tall or the last line is cut off.
        float contentBottom() const;

        NodePanel(SketchDocument& document, newui::SubView& pickerHost);
        ~NodePanel() override;

        // Shows this node; an invalid ref shows an empty panel.
        void show(const NodeRef& node);
        // Puts the keyboard in the name field (what the ring's Rename does).
        void focusName();
        // Lets go of what listens to the document (the colour card's palettes). The owner calls it before the document dies, because the dock
        // destroys the panel later; the panel shows nothing useful afterwards.
        void releaseDocument();

        // Called when the panel is destroyed. Closing the window destroys the view tree before the owner, so the owner must not touch the panel
        // afterwards; the owner clears this in its own destructor when it goes first.
        std::function<void()> onDestroyed;

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

        // The wrangle code section (Resources/nodePanelCode.newui), shown for a `wrangle` node: a "Code" toggle row, and when it is open a
        // CodeEditorView with Commit, Revert and the class button (Points / Primitives). The editor's text is the node's `code` until it is
        // edited; Commit (or Ctrl+Enter) stores it as one undo step, Revert puts the node's text back, and the cook's error shows under the
        // editor. Opening it asks the owner for a wider panel (onCodeToggled). Parameters the code declares are ordinary rows; one with both
        // limits has a slider too.
        bool codeShown() const;
        bool codeOpen() const { return codeOpen_; }
        void setCodeOpen(bool open);
        std::string codeText() const;
        void setCodeText(const std::string& text);
        void commitCode();
        void revertCode();
        bool codeModified() const;
        // "point" or "primitive"; one undo step.
        void setCodeClass(const std::string& value);
        std::string codeClass() const;
        // The message the node's cook fails with (empty when it cooks).
        std::string cookError() const;
        // The editor's own syntax problem line.
        std::string codeProblem() const;
        CodeEditorView* codeEditor() const { return editor_; }
        bool paramHasSlider(std::size_t row) const;
        // A parameter tagged "choices:A,B,C" (an object's blend) is a dropdown of those names; its value is the index.
        bool paramIsChoice(std::size_t row) const;
        std::string paramChoice(std::size_t row) const;
        void chooseParam(std::size_t row, std::size_t index);
        // The width the code section wants the panel to have, and called with true when it opens and false when it closes.
        static constexpr float kCodeWidth = 380.0f;
        std::function<void(bool open)> onCodeToggled;
        // The same section edits the code of a `python` node (the editor then colours Python and class button is hidden). The owner supplies
        // Python's syntax check, and is told the text the user commits before the node gets it: code the user typed is trusted, so the
        // node's first cook does not ask whether it may run (PythonNodeTrust::trust).
        std::function<std::optional<SyntaxProblem>(const std::string& code)> onCheckPython;
        std::function<void(const std::string& code)> onPythonCodeCommitted;
        // The Allow button on the error line, shown when the cook of a python node was refused because its code is not allowed: the owner allows
        // this text (the node's own, not what the editor holds); the panel then shows the node again.
        std::function<void(const std::string& code)> onPythonCodeAllowed;
        bool allowShown() const;
        void allowCode();
        // True when the section is showing a python node.
        bool codeIsPython() const { return pythonShown_; }

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
            Quantity quantity = Quantity::None;   // from the "quantity:" tag: the row shows and reads the document's unit; the node keeps points / degrees
            std::optional<double> minimum, maximum;
            std::vector<std::string> choices;   // from the "choices:A,B,C" tag: the value is the index, shown as a dropdown of names
            std::string r, g, b, a;
        };
        struct Row {
            newui::Label* caption = nullptr;
            newui::TextField* field = nullptr;
            newui::SubView* swatch = nullptr;
            newui::Slider* slider = nullptr;
            newui::DropDownList* choice = nullptr;
            std::shared_ptr<NumberScrubber> scrubber;
        };
        void buildCodeSection();
        void layoutCode();
        void updateCode(const jitnodes::Node* shown, bool nodeChanged);
        std::string committedCode() const;
        void useEditor(CodeEditorView* editor);
        static bool hasCode(const jitnodes::Node* node);
        void sliderChanged(std::size_t row);
        newui::SubView* codeRow_ = nullptr;
        newui::SubView* codeSection_ = nullptr;
        newui::SubView* editorHost_ = nullptr;
        newui::Button* codeToggle_ = nullptr;
        newui::Button* classButton_ = nullptr;
        newui::Label* cookErrorLabel_ = nullptr;
        newui::Button* allowButton_ = nullptr;
        CodeEditorView* editor_ = nullptr;          // the one on show: wrangleEditor_ or pythonEditor_
        CodeEditorView* wrangleEditor_ = nullptr;
        CodeEditorView* pythonEditor_ = nullptr;
        bool pythonShown_ = false;
        bool codeOpen_ = false;
        std::string loadedCode_;   // the node's text as the editor last took it; an editor holding something else has unsaved edits
        bool sliding_ = false;
        void finishSlide();   // records an open slider gesture as one undo step
        bool updatingSlider_ = false;
        bool updatingChoice_ = false;   // the panel itself is setting a slider
        std::string sliderName_;
        std::optional<jitnodes::GraphSnapshot> sliderBefore_;

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
        std::string displayText(const Entry& entry, double stored) const;
        static std::string limitText(const Entry& entry);
        void syncPicker();
        void makePicker();
        void applyPicker();
        // Removes the swatch ties (SwatchLinks::tag) of the colour row `group` from the shown node.
        void dropLinkTags(const std::string& group);
        std::uint32_t pendingLink_ = 0;   // the linked swatch being used, so the end of its gesture ties the colour to it

        newui::SubView& loadContent();
        void layoutContent();
        float fixedContentHeight() const;

        newui::SubView& root_;   // the loaded content, in the body
        newui::SubView& pickerHost_;
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
