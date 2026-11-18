#include "curvy/nodepanel.h"

#include <jitnodes/graph/network.h>

#include "curvy/pythonhighlight.h"
#include "curvy/theme.h"
#include "curvy/wranglecheck.h"
#include "curvy/wranglehighlight.h"

#include <newui/bundle.h>
#include <newui/cursor.h>
#include <newui/models.h>
#include <newui/mouse_constants.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    SubView& NodePanel::loadContent() {
        View* loaded = Bundle::instance().loadView("nodePanel");
        auto* content = dynamic_cast<SubView*>(loaded);
        if (content == nullptr) {
            delete loaded;
            throw std::runtime_error("NodePanel: Resources/nodePanel.newui did not load");
        }
        body().addChild(content);
        return *content;
    }

    // What the content needs besides the parameter rows: every other section's own desired height, read from the loaded view.
    float NodePanel::fixedContentHeight() const {
        float height = 0.0f;
        for (const SubView* child : root_.childViews()) {
            if (child != scroll_ && child->isVisible()) {
                height += child->desiredSize().height;
            }
        }
        return height;
    }

    float NodePanel::contentHeightFor(std::size_t paramRows) const {
        return fixedContentHeight() + float(paramRows) * kParamRowHeight;
    }

    float NodePanel::contentBottom() const {
        float bottom = 0.0f;
        for (const SubView* child : root_.childViews()) {
            if (child->isVisible()) {
                bottom = (std::max)(bottom, child->bounds().top() + child->bounds().height());
            }
        }
        return bottom;
    }

    // The body shows the whole content when it can; a shorter body scrolls it.
    void NodePanel::layoutContent() {
        const Rect own = body().bounds();
        const float width = own.width();
        const float height = (std::max)(own.height(), contentHeightFor(kLeastParamRows));
        root_.setBounds(Rect(0.0f, 0.0f, width, height));
        body().setContentSize(Size(width, height));
        if (scroll_ != nullptr) {
            // The list is as tall as its rows (up to the room there is), so Inputs and Used by follow it instead of sitting at the bottom of a gap.
            const float room = (std::max)(kParamRowHeight, height - fixedContentHeight());
            const float wanted = (std::max)(kParamRowHeight, kParamRowHeight * float(entries_.size()));
            scroll_->setLayoutParams(std::make_unique<FlexLayoutParams>(0.0f));
            scroll_->setDesiredSize(Size(width, (std::min)(room, wanted)));
        }
        root_.updateLayout();
        style().markDirty();
    }

    NodePanel::~NodePanel() {
        if (onDestroyed) {
            onDestroyed();
        }
    }

    NodePanel::NodePanel(SketchDocument& document, SubView& pickerHost) : Panel("node", "Node"), root_(loadContent()), pickerHost_(pickerHost), document_(document) {
        scroll_ = dynamic_cast<ScrollView*>(root_.findView("graphParamScroll"));   // needed now: the preferred height leaves its rows out
        setPreferredBodyHeight(contentHeightFor(kPreferredParamRows));
        body().onSizeChanged += [this](View&, const Size&) {
            layoutContent();
            return SyncReturn::Handled;
        };
        layoutContent();
        for (const char* id : {"graphNameField", "graphTagsField"}) {
            if (auto* f = field(id)) {
                f->caret().setColor(Theme::current().textColor());
            }
        }
        if (auto* name = field("graphNameField")) {
            name->onReturnPressed += [this](TextField&) {
                commitName();
                return SyncReturn::Handled;
            };
        }
        if (auto* tags = field("graphTagsField")) {
            tags->onReturnPressed += [this](TextField&) {
                commitTags();
                return SyncReturn::Handled;
            };
        }
        buildCodeSection();
        makePicker();
        scroll_ =dynamic_cast<ScrollView*>(root_.findView("graphParamScroll"));
        viewport_ = dynamic_cast<SubView*>(root_.findView("graphParamViewport"));
        if (scroll_ != nullptr) {
            scroll_->onSizeChanged += [this](View&, const Size&) {
                layoutRows();
                return SyncReturn::Handled;
            };
        }
        const std::pair<const char*, const char*> buttons[] = {{"graphDisplayButton", "graph.display"}, {"graphBypassButton", "graph.bypass"}, {"graphFreezeButton", "graph.freeze"}};
        for (const auto& [id, command] : buttons) {
            if (auto* button = dynamic_cast<Button*>(root_.findView(id))) {
                button->onClick += [this, command = std::string(command)](Control&) {
                    if (onCommand) {
                        onCommand(command);
                    }
                    return SyncReturn::Handled;
                };
            }
        }
        show(NodeRef());
    }

    // The fragment is one child of the content, placed after the data button: a row with the toggle, then the editor section when open.
    void NodePanel::buildCodeSection() {
        View* loaded = Bundle::instance().loadView("nodePanelCode");
        auto* section = dynamic_cast<SubView*>(loaded);
        if (section == nullptr) {
            delete loaded;
            throw std::runtime_error("NodePanel: Resources/nodePanelCode.newui did not load");
        }
        root_.addChild(section);
        std::size_t after = 0;
        const auto& children = root_.childViews();
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (children[i]->name() == "graphDataRow") {
                after = i + 1;
            }
        }
        root_.reorderChild(section, after);

        codeRow_ = section;
        codeSection_ = dynamic_cast<SubView*>(section->findView("wrangleCode"));
        editorHost_ = dynamic_cast<SubView*>(section->findView("wrangleEditorHost"));
        codeToggle_ = dynamic_cast<Button*>(section->findView("wrangleCodeToggle"));
        classButton_ = dynamic_cast<Button*>(section->findView("wrangleClassButton"));
        cookErrorLabel_ = dynamic_cast<Label*>(section->findView("wrangleProblem"));
        auto* commit = dynamic_cast<Button*>(section->findView("wrangleCommit"));
        auto* revert = dynamic_cast<Button*>(section->findView("wrangleRevert"));
        if (codeSection_ == nullptr || editorHost_ == nullptr || codeToggle_ == nullptr || classButton_ == nullptr || cookErrorLabel_ == nullptr || commit == nullptr ||
            revert == nullptr) {
            throw std::runtime_error("NodePanel: Resources/nodePanelCode.newui lacks a view");
        }
        cookErrorLabel_->setText("");
        codeSection_->setLayoutParams(std::make_unique<FlexLayoutParams>(0.0f));
        codeSection_->setVisible(false);

        // Two editors share the host, one per language (a view's language is fixed when it is made); the one for the node on show is visible.
        const auto makeEditor = [this](std::shared_ptr<const CodeLanguage> language, const char* name) {
            auto* editor = new CodeEditorView(std::move(language), name);
            editorHost_->addChild(editor);
            editor->onExecuteKey = [this]() { commitCode(); };
            editor->onHistoryChanged = [this]() { layoutCode(); };
            return editor;
        };
        wrangleEditor_ = makeEditor(WrangleLanguage::instance(), "wrangleEditor");
        wrangleEditor_->onCheckSyntax = [](const std::string& code) { return checkWrangle(code); };
        pythonEditor_ = makeEditor(PythonLanguage::instance(), "pythonNodeEditor");
        pythonEditor_->onCheckSyntax = [this](const std::string& code) -> std::optional<SyntaxProblem> {
            return onCheckPython ? onCheckPython(code) : std::nullopt;
        };
        editor_ = wrangleEditor_;
        useEditor(wrangleEditor_);
        editorHost_->onSizeChanged += [this](View&, const Size& size) {
            for (CodeEditorView* editor : {wrangleEditor_, pythonEditor_}) {
                editor->setBounds(Rect(0.0f, 0.0f, size.width, size.height));
                editor->layoutParts();
            }
            return SyncReturn::Handled;
        };

        codeToggle_->onClick += [this](Control&) {
            setCodeOpen(!codeOpen_);
            return SyncReturn::Handled;
        };
        classButton_->onClick += [this](Control&) {
            setCodeClass(codeClass() == "primitive" ? "point" : "primitive");
            return SyncReturn::Handled;
        };
        commit->onClick += [this](Control&) {
            commitCode();
            return SyncReturn::Handled;
        };
        revert->onClick += [this](Control&) {
            revertCode();
            return SyncReturn::Handled;
        };
        section->setVisible(false);
    }

    // The row's own height plus the section's when it is open; the content's layout reads this as the section's desired height.
    void NodePanel::layoutCode() {
        if (codeRow_ == nullptr) {
            return;
        }
        float rowHeight = 0.0f;
        for (const SubView* child : codeRow_->childViews()) {
            if (child != codeSection_) {
                rowHeight += child->desiredSize().height;
            }
        }
        float sectionHeight = 0.0f;
        for (const SubView* child : codeSection_->childViews()) {
            sectionHeight += child->desiredSize().height;
        }
        const bool open = codeOpen_ && codeRow_->isVisible();
        codeSection_->setVisible(open);
        codeRow_->setDesiredSize(Size(codeRow_->bounds().width(), rowHeight + (open ? sectionHeight : 0.0f)));
    }

    bool NodePanel::codeShown() const {
        return codeRow_ != nullptr && codeRow_->isVisible();
    }

    void NodePanel::setCodeOpen(bool open) {
        if (open == codeOpen_) {
            return;
        }
        codeOpen_ = open;
        if (codeToggle_ != nullptr) {
            codeToggle_->setText(open ? "Code (open)" : "Code");
        }
        layoutCode();
        setPreferredBodyHeight(contentHeightFor(kPreferredParamRows));
        layoutContent();
        if (onCodeToggled) {
            onCodeToggled(open);
        }
    }

    void NodePanel::useEditor(CodeEditorView* editor) {
        editor_ = editor;
        pythonShown_ = editor == pythonEditor_;
        wrangleEditor_->setVisible(editor == wrangleEditor_);
        pythonEditor_->setVisible(editor == pythonEditor_);
        classButton_->setVisible(!pythonShown_);   // a python node has no point/primitive class
    }

    bool NodePanel::hasCode(const jitnodes::Node* node) {
        return node != nullptr && (node->typeName() == "wrangle" || node->typeName() == "python");
    }

    std::string NodePanel::committedCode() const {
        if (node_.valid() && document_.hasNode(node_) && hasCode(&document_.node(node_))) {
            return document_.node(node_).getString("code");
        }
        return "";
    }

    std::string NodePanel::codeText() const {
        return editor_ != nullptr ? editor_->text() : "";
    }

    void NodePanel::setCodeText(const std::string& text) {
        if (editor_ != nullptr) {
            editor_->setText(text);
            editor_->markClean();
        }
    }

    bool NodePanel::codeModified() const {
        return editor_ != nullptr && codeShown() && editor_->text() != committedCode();
    }

    void NodePanel::commitCode() {
        if (!codeShown() || !node_.valid() || !document_.hasNode(node_)) {
            return;
        }
        const std::string text = editor_->text();
        if (text != committedCode()) {
            if (pythonShown_ && onPythonCodeCommitted) {
                onPythonCodeCommitted(text);   // before the edit, whose cook would otherwise ask whether this code may run
            }
            try {
                document_.setGraphString(node_, "code", text);
            } catch (const std::exception& error) {
                if (onRefused) {
                    onRefused(error.what());
                }
                return;
            }
            if (onEdited) {
                onEdited();
            }
        }
        editor_->markClean();
        loadedCode_ = text;
        show(node_);
    }

    void NodePanel::revertCode() {
        if (codeShown()) {
            loadedCode_ = committedCode();
            editor_->setText(loadedCode_);
        }
        show(node_);
    }

    void NodePanel::setCodeClass(const std::string& value) {
        if (!codeShown() || !node_.valid() || !document_.hasNode(node_) || value == codeClass()) {
            return;
        }
        document_.setGraphString(node_, "class", value);
        if (onEdited) {
            onEdited();
        }
        show(node_);
    }

    std::string NodePanel::codeClass() const {
        if (node_.valid() && document_.hasNode(node_) && document_.node(node_).typeName() == "wrangle") {
            return document_.node(node_).getString("class");
        }
        return "";
    }

    std::string NodePanel::cookError() const {
        return cookErrorLabel_ != nullptr ? cookErrorLabel_->text() : "";
    }

    std::string NodePanel::codeProblem() const {
        return editor_ != nullptr ? editor_->problemText() : "";
    }

    // Shows or hides the section for the node, loads its text when it is another node (or the editor holds nothing unsaved), and reports
    // the cook's error.
    void NodePanel::updateCode(const jitnodes::Node* shown, bool nodeChanged) {
        const bool hasCodeSection = hasCode(shown);
        codeRow_->setVisible(hasCodeSection);
        if (hasCodeSection) {
            CodeEditorView* wanted = shown->typeName() == "python" ? pythonEditor_ : wrangleEditor_;
            if (wanted != editor_) {
                useEditor(wanted);
                nodeChanged = true;
            }
            if (nodeChanged || editor_->text() == loadedCode_) {
                loadedCode_ = shown->getString("code");
                setCodeText(loadedCode_);
            }
            if (!pythonShown_) {
                classButton_->setText(shown->getString("class") == "primitive" ? "Primitives" : "Points");
            }
            std::string error;
            try {
                document_.cook(node_);
            } catch (const std::exception& caught) {
                error = caught.what();
            }
            if (pythonShown_) {
                // pybind's message is the exception line followed by "At:" and the frames; the first line is the one worth showing.
                const std::size_t end = error.find('\n');
                error = error.substr(0, end);
            }
            cookErrorLabel_->setText(error);
        } else {
            cookErrorLabel_->setText("");
        }
        layoutCode();
        layoutContent();
    }

    Label* NodePanel::label(const std::string& name) const {
        return dynamic_cast<Label*>(root_.findView(name));
    }

    TextField* NodePanel::field(const std::string& name) const {
        return dynamic_cast<TextField*>(root_.findView(name));
    }

    std::string NodePanel::narrow(const std::wstring& text) {
        return std::string(text.begin(), text.end());
    }

    std::wstring NodePanel::widen(const std::string& text) {
        return std::wstring(text.begin(), text.end());
    }

    void NodePanel::show(const NodeRef& node) {
        const bool nodeChanged = !(node == node_);
        if (nodeChanged) {
            finishSlide();
            closeColorPicker();
        }
        node_ = node;
        const bool have = node.valid() && document_.hasNode(node);
        const jitnodes::Node* shown = have ? &document_.node(node) : nullptr;
        const auto set = [&](const char* labelName, const std::string& text) {
            if (Label* l = label(labelName)) {
                l->setText(text);
            }
        };
        set("graphPropsTitle", shown != nullptr ? shown->name() : "Nothing selected");
        set("graphPropsKind", shown != nullptr ? shown->typeName() : "");
        if (TextField* name = field("graphNameField")) {
            name->setText(widen(shown != nullptr ? shown->name() : ""));
            name->setEnabled(shown != nullptr);
        }
        if (TextField* tags = field("graphTagsField")) {
            std::string text;
            if (shown != nullptr) {
                for (const std::string& tag : shown->tags()) {
                    text += (text.empty() ? "" : " ") + tag;
                }
            }
            tags->setText(widen(text));
            tags->setEnabled(shown != nullptr);
        }
        const auto flag = [&](const char* id, const std::string& text) {
            if (auto* button = dynamic_cast<Button*>(root_.findView(id))) {
                button->setText(text);
                button->setEnabled(shown != nullptr);
            }
        };
        const bool displayed = have && node.scope == 0 && document_.network().displayNode() == node.id;
        flag("graphDisplayButton", displayed ? "Displayed" : "Display");
        flag("graphBypassButton", shown != nullptr && shown->bypassed() ? "Enable" : "Bypass");
        flag("graphFreezeButton", shown != nullptr && shown->frozen() ? "Unfreeze" : "Freeze");

        entries_.clear();
        if (shown != nullptr) {
            std::vector<std::string> names = shown->floatNames();
            std::sort(names.begin(), names.end());
            std::vector<Entry> colors, plain;
            const auto addPlain = [&](const std::string& n) {
                Entry entry;
                entry.name = n;
                if (const auto unit = shown->paramTagValue(n, "unit")) {
                    entry.unit = std::strtod(unit->c_str(), nullptr);
                }
                if (!(entry.unit > 0.0)) {
                    entry.unit = 1.0;
                }
                if (const auto low = shown->paramTagValue(n, "min")) {
                    entry.minimum = std::strtod(low->c_str(), nullptr);
                }
                if (const auto high = shown->paramTagValue(n, "max")) {
                    entry.maximum = std::strtod(high->c_str(), nullptr);
                }
                if (const auto choices = shown->paramTagValue(n, "choices")) {
                    std::istringstream in(*choices);
                    for (std::string word; std::getline(in, word, ',');) {
                        entry.choices.push_back(word);
                    }
                }
                float limit = 0.0f;
                if (!entry.minimum && shown->floatMinimum(n, limit)) {
                    entry.minimum = double(limit);
                }
                if (!entry.maximum && shown->floatMaximum(n, limit)) {
                    entry.maximum = double(limit);
                }
                plain.push_back(entry);
            };
            for (const std::string& n : names) {
                const auto group = shown->paramTagValue(n, "group");
                if (!group || shown->paramTagValue(n, "kind").value_or("") != "color") {
                    addPlain(n);
                    continue;
                }
                auto at = std::find_if(colors.begin(), colors.end(), [&](const Entry& e) { return e.name == *group; });
                if (at == colors.end()) {
                    Entry color;
                    color.name = *group;
                    color.color = true;
                    colors.push_back(color);
                    at = colors.end() - 1;
                }
                const std::string channel = shown->paramTagValue(n, "channel").value_or("");
                (channel == "r" ? at->r : channel == "g" ? at->g : channel == "b" ? at->b : at->a) = n;
            }
            // A colour needs all of r, g and b; the parameters of one that does not are shown as plain numbers.
            for (const Entry& color : colors) {
                if (!color.r.empty() && !color.g.empty() && !color.b.empty()) {
                    entries_.push_back(color);
                } else {
                    for (const std::string& n : {color.r, color.g, color.b, color.a}) {
                        if (!n.empty()) {
                            addPlain(n);
                        }
                    }
                }
            }
            if (shown->typeName() == "wrangle") {
                std::stable_partition(plain.begin(), plain.end(), [&](const Entry& entry) { return shown->isSpare(entry.name); });
            }
            entries_.insert(entries_.end(), plain.begin(), plain.end());
        }
        while (viewport_ != nullptr && rows_.size() < entries_.size()) {
            makeRow();
        }
        for (std::size_t row = 0; row < rows_.size(); ++row) {
            fillRow(row);
        }
        layoutRows();
        syncPicker();
        updateCode(shown, nodeChanged);

        const jitnodes::Network* net = have ? document_.findNetwork(node.scope) : nullptr;
        const auto link = [&](const char* id, const std::string& text) {
            if (Label* l = label(id)) {
                l->setText(text);
            }
        };
        // What feeds each input, and what this node feeds. The input's number is only said when the node has more than one (a bare "0" read like a count).
        std::vector<std::string> inputs, users;
        if (net != nullptr) {
            for (int i = 0; i < shown->inputCount(); ++i) {
                const jitnodes::NodeId from = net->inputOf(node.id, i);
                const std::string source = from == jitnodes::invalidNodeId ? "(nothing wired)" : net->node(from).name();
                inputs.push_back(shown->inputCount() > 1 ? "input " + std::to_string(i) + ": " + source : source);
            }
            for (const jitnodes::NodeId id : net->nodeIds()) {
                for (int i = 0; i < net->node(id).inputCount(); ++i) {
                    if (net->inputOf(id, i) == node.id) {
                        users.push_back(net->node(id).inputCount() > 1 ? net->node(id).name() + " (input " + std::to_string(i) + ")" : net->node(id).name());
                    }
                }
            }
        }
        std::string usedBy;
        if (net != nullptr) {
            usedBy = users.empty() ? "(not used)" : users[0] + (users.size() > 1 ? "  and " + std::to_string(users.size() - 1) + " more" : "");
        }
        link("graphInput0", inputs.size() > 0 ? inputs[0] : "");
        link("graphInput1", inputs.size() > 1 ? inputs[1] : "");
        link("graphUsed0", usedBy);
        root_.style().markDirty();
    }

    void NodePanel::focusName() {
        if (TextField* name = field("graphNameField")) {
            if (RootView* rootView = name->rootView()) {
                rootView->setFocusedSubView(name);
            }
        }
    }

    void NodePanel::releaseDocument() {
        closeColorPicker();
        swatches_.reset();
    }

    void NodePanel::commitName() {
        if (TextField* name = field("graphNameField")) {
            rename(narrow(name->text()));
        }
    }

    void NodePanel::commitTags() {
        if (TextField* tags = field("graphTagsField")) {
            retag(narrow(tags->text()));
        }
    }

    void NodePanel::retag(const std::string& text) {
        if (!node_.valid() || !document_.hasNode(node_)) {
            show(node_);
            return;
        }
        std::vector<std::string> words;
        std::string spaced = text;
        std::replace(spaced.begin(), spaced.end(), ',', ' ');
        std::istringstream in(spaced);
        for (std::string word; in >> word;) {
            if (std::find(words.begin(), words.end(), word) == words.end()) {
                words.push_back(word);
            }
        }
        try {
            document_.setGraphTags(node_, words);
            if (onEdited) {
                onEdited();
            }
        } catch (const std::exception& error) {
            if (onRefused) {
                onRefused(error.what());
            }
        }
        show(node_);
    }

    void NodePanel::rename(const std::string& text) {
        if (!node_.valid() || !document_.hasNode(node_) || text.empty() || text == document_.node(node_).name()) {
            show(node_);
            return;
        }
        try {
            document_.renameGraphNode(node_, text);
            if (onEdited) {
                onEdited();
            }
        } catch (const std::exception& error) {
            if (onRefused) {
                onRefused(error.what());
            }
        }
        show(node_);
    }

    void NodePanel::makeRow() {
        if (viewport_ == nullptr) {
            return;
        }
        const std::size_t index = rows_.size();
        Row row;
        row.caption = new Label();
        row.caption->setTextAlignment(TextAlignment::Left);
        row.caption->setTextColor(BLRgba32(0xFFA0A0A0u));
        viewport_->addChild(row.caption);
        row.field = new TextField();
        row.field->setTextColor(BLRgba32(0xFFF0F0F0u));
        row.field->caret().setColor(Theme::current().textColor());
        viewport_->addChild(row.field);
        row.swatch = new SubView();
        row.swatch->setName("nodeSwatch" + std::to_string(index));
        row.swatch->setVisible(false);
        row.swatch->setCursor(Cursor(CursorKind::Hand));
        row.swatch->onMouseDown += [this, index](View&, const Point&, std::uint32_t buttons, std::uint32_t) {
            if ((buttons & mbmLeftButton) == 0) {
                return SyncReturn(SyncReturn::Ignored);
            }
            toggleColorPicker(index);
            return SyncReturn(SyncReturn::Handled);
        };
        viewport_->addChild(row.swatch);
        row.slider = new Slider();
        row.slider->setName("nodeSlider" + std::to_string(index));
        row.slider->setVisible(false);
        row.slider->onValueChanged += [this, index](Slider&) {
            sliderChanged(index);
            return SyncReturn::Handled;
        };
        // A drag that starts on the thumb sends its mouse events to the thumb, not the slider, so both end the gesture.
        const auto released = [this](View&, const Point&, std::uint32_t, std::uint32_t) {
            finishSlide();
            return SyncReturn(SyncReturn::Ignored);
        };
        row.slider->onMouseUp += released;
        if (row.slider->thumb() != nullptr) {
            row.slider->thumb()->onMouseUp += released;
        }
        viewport_->addChild(row.slider);
        row.choice = new DropDownList();
        row.choice->setName("nodeChoice" + std::to_string(index));
        row.choice->setVisible(false);
        row.choice->onSelectionChanged += [this, index](DropDownList& list) {
            if (!updatingChoice_ && list.selectedIndex().has_value()) {
                chooseParam(index, *list.selectedIndex());
            }
            return SyncReturn::Handled;
        };
        viewport_->addChild(row.choice);

        row.field->onReturnPressed += [this, index](TextField&) {
            commitParam(index);
            return SyncReturn::Handled;
        };
        row.scrubber = NumberScrubber::attach(*row.caption, 1.0, -1e9, 1e9);
        row.scrubber->onBegin += [this, index](NumberScrubber&, double& value) {
            if (index >= entries_.size() || entries_[index].color || !entries_[index].choices.empty() || !node_.valid() || !document_.hasNode(node_)) {
                return SyncReturn(SyncReturn::Ignored);
            }
            scrubBefore_ = document_.snapshotGraph();
            value = double(document_.node(node_).getFloat(entries_[index].name));
            return SyncReturn(SyncReturn::Handled);
        };
        row.scrubber->onChange += [this, index](NumberScrubber&, double value) {
            if (index < entries_.size() && node_.valid() && document_.hasNode(node_)) {
                document_.setGraphFloatLive(node_, entries_[index].name, float(clampTo(entries_[index], value)));
                fillRow(index);
                if (onEdited) {
                    onEdited();
                }
            }
            return SyncReturn::Handled;
        };
        row.scrubber->onEnd += [this, index](NumberScrubber&, bool changed) {
            if (changed && scrubBefore_ && index < entries_.size()) {
                document_.commitGraphEdit(*scrubBefore_, "Change " + entries_[index].name);
            }
            scrubBefore_.reset();
            return SyncReturn::Handled;
        };
        row.scrubber->onUnit += [this, index](NumberScrubber&, double& unit) {
            if (index >= entries_.size()) {
                return SyncReturn(SyncReturn::Ignored);
            }
            unit = entries_[index].unit;
            return SyncReturn(SyncReturn::Handled);
        };
        rows_.push_back(row);
    }

    bool NodePanel::paramIsChoice(std::size_t row) const {
        return row < entries_.size() && row < rows_.size() && rows_[row].choice->isVisible();
    }

    std::string NodePanel::paramChoice(std::size_t row) const {
        if (!paramIsChoice(row) || !rows_[row].choice->selectedIndex().has_value()) {
            return "";
        }
        return entries_[row].choices.at(*rows_[row].choice->selectedIndex());
    }

    void NodePanel::chooseParam(std::size_t row, std::size_t index) {
        if (row >= entries_.size() || index >= entries_[row].choices.size() || !node_.valid() || !document_.hasNode(node_)) {
            return;
        }
        document_.setGraphParam(node_, entries_[row].name, float(index));
        if (onEdited) {
            onEdited();
        }
        show(node_);
    }

    bool NodePanel::paramHasSlider(std::size_t row) const {
        return row < entries_.size() && row < rows_.size() && rows_[row].slider->isVisible();
    }

    void NodePanel::finishSlide() {
        if (sliding_ && sliderBefore_) {
            document_.commitGraphEdit(*sliderBefore_, "Change " + sliderName_);
        }
        sliding_ = false;
        sliderBefore_.reset();
    }

    void NodePanel::sliderChanged(std::size_t row) {
        if (updatingSlider_ || row >= entries_.size() || row >= rows_.size() || entries_[row].color || !node_.valid() || !document_.hasNode(node_)) {
            return;
        }
        const Entry& entry = entries_[row];
        const float value = float(clampTo(entry, double(rows_[row].slider->value())));
        // The first change of a gesture takes the snapshot (a click on the track changes the value before any press handler could run); the
        // mouse release records the whole gesture as one undo step.
        if (!sliding_) {
            sliding_ = true;
            sliderBefore_ = document_.snapshotGraph();
        }
        sliderName_ = entry.name;
        document_.setGraphFloatLive(node_, entry.name, value);
        char text[32];
        std::snprintf(text, sizeof(text), "%g", double(value));
        rows_[row].field->setText(widen(text));
        if (onEdited) {
            onEdited();
        }
    }

    void NodePanel::fillRow(std::size_t index) {
        Row& row = rows_[index];
        const bool used = index < entries_.size() && node_.valid() && document_.hasNode(node_);
        row.caption->setVisible(used);
        row.field->setVisible(used);
        row.swatch->setVisible(used && entries_[index].color);
        const bool choiceRow = used && !entries_[index].color && !entries_[index].choices.empty();
        row.choice->setVisible(choiceRow);
        if (choiceRow) {
            row.field->setVisible(false);
        }
        const bool sliderRow = used && !choiceRow && !entries_[index].color && entries_[index].minimum && entries_[index].maximum && *entries_[index].maximum > *entries_[index].minimum;
        row.slider->setVisible(sliderRow);
        if (!used) {
            return;
        }
        if (choiceRow) {
            updatingChoice_ = true;
            auto model = std::make_unique<StringListModel>();
            for (const std::string& name : entries_[index].choices) {
                model->addItem(name);
            }
            row.choice->setModel(std::move(model));
            const long chosen = std::lround(document_.node(node_).getFloat(entries_[index].name));
            row.choice->setSelectedIndex(std::size_t(std::clamp<long>(chosen, 0, long(entries_[index].choices.size()) - 1)));
            updatingChoice_ = false;
        }
        if (sliderRow) {
            updatingSlider_ = true;
            row.slider->setRange(float(*entries_[index].minimum), float(*entries_[index].maximum));
            row.slider->setValue(document_.node(node_).getFloat(entries_[index].name));
            updatingSlider_ = false;
        }
        const Entry& entry = entries_[index];
        const jitnodes::Node& shown = document_.node(node_);
        row.caption->setText(entry.name);
        row.caption->setCursor(Cursor(entry.color || !entry.choices.empty() ? CursorKind::Arrow : CursorKind::SizeWE));
        if (entry.color) {
            const float r = shown.getFloat(entry.r), g = shown.getFloat(entry.g), b = shown.getFloat(entry.b);
            row.field->setText(widen(entry.a.empty() ? colorText(r, g, b) : colorText(r, g, b, shown.getFloat(entry.a))));
            const auto byte = [](float v) { return std::uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            row.swatch->style().setBackgroundColor(BLRgba32(0xFF000000u | (byte(r) << 16) | (byte(g) << 8) | byte(b)));
        } else {
            char text[32];
            std::snprintf(text, sizeof(text), "%g", double(shown.getFloat(entry.name)));
            row.field->setText(widen(text));
        }
    }

    void NodePanel::layoutRows() {
        if (viewport_ == nullptr || scroll_ == nullptr) {
            return;
        }
        constexpr float rowHeight = 24.0f, margin = 8.0f, captionWidth = 90.0f, swatchWidth = 22.0f, scrollBar = 16.0f;
        const float width = (std::max)(120.0f, scroll_->getClientBounds().width() - scrollBar);
        const float height = (std::max)(rowHeight, rowHeight * float(entries_.size()));
        viewport_->setBounds(Rect(0.0f, 0.0f, width, height));
        scroll_->setContentSize(Size(width, height));
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const float y = rowHeight * float(i);
            Row& row = rows_[i];
            row.caption->setBounds(Rect(margin, y + 3.0f, captionWidth, 18.0f));
            float fieldLeft = margin + captionWidth + 6.0f;
            if (i < entries_.size() && entries_[i].color) {
                row.swatch->setBounds(Rect(fieldLeft, y + 4.0f, swatchWidth, 16.0f));
                fieldLeft += swatchWidth + 6.0f;
            }
            if (row.choice->isVisible()) {
                row.choice->setBounds(Rect(fieldLeft, y + 2.0f, (std::max)(40.0f, width - fieldLeft - margin), 20.0f));
                continue;
            }
            if (row.slider->isVisible()) {
                constexpr float numberWidth = 56.0f;
                row.slider->setBounds(Rect(fieldLeft, y + 4.0f, (std::max)(40.0f, width - fieldLeft - margin - numberWidth - 6.0f), 16.0f));
                fieldLeft = width - margin - numberWidth;
                row.field->setBounds(Rect(fieldLeft, y + 2.0f, numberWidth, 20.0f));
                continue;
            }
            row.field->setBounds(Rect(fieldLeft, y + 2.0f, (std::max)(40.0f, width - fieldLeft - margin), 20.0f));
        }
        scroll_->updateLayout();
        scroll_->style().markDirty();
    }

    double NodePanel::clampTo(const Entry& entry, double value) {
        if (entry.minimum && value < *entry.minimum) {
            return *entry.minimum;
        }
        if (entry.maximum && value > *entry.maximum) {
            return *entry.maximum;
        }
        return value;
    }

    std::string NodePanel::limitText(const Entry& entry) {
        char low[32], high[32];
        std::snprintf(low, sizeof(low), "%g", entry.minimum.value_or(0.0));
        std::snprintf(high, sizeof(high), "%g", entry.maximum.value_or(0.0));
        if (entry.minimum && entry.maximum) {
            return std::string(low) + " to " + high;
        }
        return entry.minimum ? "at least " + std::string(low) : "at most " + std::string(high);
    }

    // The card lies over the canvas, at its top right next to the dock, so it never covers the panel it was opened from.
    void NodePanel::makePicker() {
        SubView* nodes = &pickerHost_;
        picker_ = new ColorPickerCard();
        picker_->setHexVisible(false);   // the colour row already shows the hex
        picker_->setVisible(false);
        nodes->addChild(picker_);
        picker_->onDragStarted += [this](ColorPickerCard&) {
            pickerBefore_ = document_.snapshotGraph();
            return SyncReturn::Handled;
        };
        picker_->onChanged += [this](ColorPickerCard&) {
            applyPicker();
            swatches_->setCurrent(Swatch{float(picker_->red()), float(picker_->green()), float(picker_->blue()), float(picker_->alpha()), ""});
            return SyncReturn::Handled;
        };
        picker_->onDragFinished += [this](ColorPickerCard&) {
            if (pickerBefore_ && pickerRow_ < entries_.size()) {
                if (pendingLink_ != 0 && node_.valid() && document_.hasNode(node_)) {
                    document_.node(node_).addTag(SwatchLinks::tag(pendingLink_, entries_[pickerRow_].name));
                }
                document_.commitGraphEdit(*pickerBefore_, "Change " + entries_[pickerRow_].name);
            }
            pickerBefore_.reset();
            document_.noteRecentColor(Swatch{float(picker_->red()), float(picker_->green()), float(picker_->blue()), float(picker_->alpha()), ""});
            return SyncReturn::Handled;
        };

        picker_->setSwatchesVisible(true);
        swatches_ = std::make_unique<SwatchPaletteController>(document_, picker_->swatchStrip());
        swatches_->onUse = [this](const Swatch& swatch) {
            pendingLink_ = swatch.link;
            picker_->applyColor(swatch.r, swatch.g, swatch.b, swatch.a);
            pendingLink_ = 0;
        };
        swatches_->onMessage = [this](const std::string& message) {
            if (onRefused) {
                onRefused(message);
            }
        };
    }

    bool NodePanel::colorPickerOpen() const {
        return picker_ != nullptr && picker_->isVisible();
    }

    bool NodePanel::useRecentColor(std::size_t index) {
        const std::vector<Swatch>& recent = document_.recentColors();
        if (!colorPickerOpen() || index >= recent.size()) {
            return false;
        }
        const Swatch swatch = recent[index];   // applying it reorders the list
        picker_->applyColor(swatch.r, swatch.g, swatch.b, swatch.a);
        return true;
    }

    void NodePanel::closeColorPicker() {
        if (picker_ != nullptr && picker_->isVisible()) {
            picker_->setVisible(false);
            if (View* parent = picker_->parent()) {
                parent->style().markDirty();
            }
        }
        pickerBefore_.reset();
    }

    void NodePanel::toggleColorPicker(std::size_t row) {
        if (picker_ == nullptr || row >= entries_.size() || !entries_[row].color || !node_.valid() || !document_.hasNode(node_)) {
            return;
        }
        if (colorPickerOpen() && pickerRow_ == row) {
            closeColorPicker();
            return;
        }
        pickerRow_ = row;
        if (View* parent = picker_->parent()) {
            const Rect bounds = picker_->bounds();
            picker_->setBounds(Rect((std::max)(10.0f, parent->bounds().width() - bounds.width() - 10.0f), 10.0f, bounds.width(), bounds.height()));
            parent->style().markDirty();
        }
        picker_->setVisible(true);
        swatches_->refresh();   // New and Open change the palettes without telling the strip
        syncPicker();
    }

    void NodePanel::syncPicker() {
        if (!colorPickerOpen()) {
            return;
        }
        if (pickerRow_ >= entries_.size() || !entries_[pickerRow_].color || !node_.valid() || !document_.hasNode(node_)) {
            closeColorPicker();
            return;
        }
        const Entry& entry = entries_[pickerRow_];
        const jitnodes::Node& shown = document_.node(node_);
        const bool hasAlpha = !entry.a.empty();
        picker_->setColor(shown.getFloat(entry.r), shown.getFloat(entry.g), shown.getFloat(entry.b), hasAlpha ? shown.getFloat(entry.a) : 1.0, hasAlpha);
        swatches_->setCurrent(Swatch{float(picker_->red()), float(picker_->green()), float(picker_->blue()), float(picker_->alpha()), ""});
    }

    void NodePanel::dropLinkTags(const std::string& group) {
        jitnodes::Node& node = document_.node(node_);
        const std::vector<std::string> tags = node.tags();
        for (const std::string& tag : tags) {
            std::uint32_t link = 0;
            std::string tied;
            if (SwatchLinks::parse(tag, link, tied) && tied == group) {
                node.removeTag(tag);
            }
        }
    }

    void NodePanel::applyPicker() {
        if (pickerRow_ >= entries_.size() || !entries_[pickerRow_].color || !node_.valid() || !document_.hasNode(node_)) {
            return;
        }
        const Entry& entry = entries_[pickerRow_];
        dropLinkTags(entry.name);   // a linked swatch's use ties the colour again when the gesture ends
        document_.setGraphFloatLive(node_, entry.r, float(picker_->red()));
        document_.setGraphFloatLive(node_, entry.g, float(picker_->green()));
        document_.setGraphFloatLive(node_, entry.b, float(picker_->blue()));
        if (!entry.a.empty()) {
            document_.setGraphFloatLive(node_, entry.a, float(picker_->alpha()));
        }
        fillRow(pickerRow_);
        if (onEdited) {
            onEdited();
        }
    }

    std::string NodePanel::colorText(float r, float g, float b) {
        const auto byte = [](float v) { return unsigned(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        char text[16];
        std::snprintf(text, sizeof(text), "#%02X%02X%02X", byte(r), byte(g), byte(b));
        return text;
    }

    std::string NodePanel::colorText(float r, float g, float b, float a) {
        const auto byte = [](float v) { return unsigned(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        char text[16];
        std::snprintf(text, sizeof(text), "#%02X%02X%02X%02X", byte(r), byte(g), byte(b), byte(a));
        return text;
    }

    bool NodePanel::parseColor(const std::string& text, float& r, float& g, float& b, float& a, bool& hasAlpha) {
        std::string digits = text;
        digits.erase(std::remove_if(digits.begin(), digits.end(), [](unsigned char c) { return std::isspace(c) != 0; }), digits.end());
        if (!digits.empty() && digits[0] == '#') {
            digits.erase(0, 1);
        }
        if ((digits.size() != 6 && digits.size() != 8) || !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) {
            return false;
        }
        hasAlpha = digits.size() == 8;
        const unsigned long value = std::strtoul(digits.c_str(), nullptr, 16);
        const unsigned long rgb = hasAlpha ? value >> 8 : value;
        r = float((rgb >> 16) & 0xFFu) / 255.0f;
        g = float((rgb >> 8) & 0xFFu) / 255.0f;
        b = float(rgb & 0xFFu) / 255.0f;
        if (hasAlpha) {
            a = float(value & 0xFFu) / 255.0f;
        }
        return true;
    }

    void NodePanel::setParam(std::size_t row, const std::string& text) {
        if (row < rows_.size()) {
            rows_[row].field->setText(widen(text));
        }
        commitParam(row);
    }

    void NodePanel::commitParam(std::size_t row) {
        if (row >= rows_.size() || row >= entries_.size() || !node_.valid() || !document_.hasNode(node_)) {
            return;
        }
        const std::string text = narrow(rows_[row].field->text());
        if (entries_[row].color) {
            const Entry& entry = entries_[row];
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
            bool hasAlpha = false;
            if (!parseColor(text, r, g, b, a, hasAlpha)) {
                if (onRefused) {
                    onRefused("'" + text + "' is not a colour (use #RRGGBB or #RRGGBBAA)");
                }
                show(node_);
                return;
            }
            std::map<std::string, float> values = {{entry.r, r}, {entry.g, g}, {entry.b, b}};
            if (hasAlpha && !entry.a.empty()) {
                values[entry.a] = a;
            }
            const jitnodes::GraphSnapshot before = document_.snapshotGraph();
            for (const auto& [name, value] : values) {
                document_.setGraphFloatLive(node_, name, value);
            }
            dropLinkTags(entry.name);   // a colour typed by hand is no longer the swatch's
            document_.commitGraphEdit(before, "Change " + entry.name);
        } else {
            char* end = nullptr;
            const double number = std::strtod(text.c_str(), &end);
            if (end == text.c_str()) {
                if (onRefused) {
                    onRefused("'" + text + "' is not a number");
                }
                show(node_);
                return;
            }
            const double limited = clampTo(entries_[row], number);
            if (limited != number && onRefused) {
                onRefused(entries_[row].name + " is limited to " + limitText(entries_[row]));
            }
            document_.setGraphParam(node_, entries_[row].name, float(limited));
        }
        if (onEdited) {
            onEdited();
        }
        show(node_);
    }

    std::string NodePanel::title() const {
        const Label* l = label("graphPropsTitle");
        return l != nullptr ? l->text() : "";
    }

    std::string NodePanel::kind() const {
        const Label* l = label("graphPropsKind");
        return l != nullptr ? l->text() : "";
    }

    std::string NodePanel::name() const {
        const TextField* f = field("graphNameField");
        return f != nullptr ? narrow(f->text()) : "";
    }

    std::string NodePanel::paramCaption(std::size_t row) const {
        return row < entries_.size() ? entries_[row].name : "";
    }

    std::string NodePanel::paramValue(std::size_t row) const {
        return row < entries_.size() && row < rows_.size() ? narrow(rows_[row].field->text()) : "";
    }

    bool NodePanel::paramShown(std::size_t row) const {
        return row < entries_.size() && row < rows_.size() && (rows_[row].field->isVisible() || rows_[row].choice->isVisible());
    }

}
