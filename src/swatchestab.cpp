#include "curvy/swatchestab.h"

#include "curvy/nodepanel.h"

#include <newui/bundle.h>
#include <newui/cursor.h>
#include <newui/mouse_constants.h>
#include <newui/rootview.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kPad = 10.0f;
        constexpr float kGap = 6.0f;
        constexpr float kTitleHeight = 14.0f;
        constexpr float kHeadHeight = 24.0f;
        constexpr float kRowHeight = 44.0f;
        constexpr float kRowGap = 4.0f;
        constexpr float kTileWidth = 29.0f;    // 80% of the 36 px square before it
        constexpr float kTileChipHeight = kTileWidth;   // the colour is a square
        constexpr float kTileGap = 5.0f;
        constexpr float kTileHeight = kTileChipHeight;   // just the square: the name is in the details box
        constexpr float kUseHeight = 26.0f;
        constexpr std::size_t kSegments = 32;

        std::uint32_t byteOf(float value) {
            return std::uint32_t(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        BLRgba32 colorOf(const Swatch& swatch) {
            return BLRgba32(byteOf(swatch.r), byteOf(swatch.g), byteOf(swatch.b), 255u);
        }

        std::wstring widen(const std::string& text) {
            return std::wstring(text.begin(), text.end());
        }

        std::string narrow(const std::wstring& text) {
            return std::string(text.begin(), text.end());
        }

        std::string hexOf(const Swatch& swatch) {
            return swatch.a < 1.0f ? NodePanel::colorText(swatch.r, swatch.g, swatch.b, swatch.a) : NodePanel::colorText(swatch.r, swatch.g, swatch.b);
        }

        std::string upper(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            return text;
        }

        std::string plural(std::size_t count, const char* one, const char* many) {
            return std::to_string(count) + " " + (count == 1 ? one : many);
        }

        constexpr float kDescriptionSize = 11.0f;   // the muted, descriptive text: a little smaller than the names and values

        void setFontSize(Label* label, float size) {
            if (label != nullptr) {
                Font font = label->style().font();
                font.setSize(size);
                label->style().setFont(font);
            }
        }

        Label* makeLabel(SubView& parent, bool muted) {
            auto* label = new Label();
            label->setTextAlignment(TextAlignment::Left);
            label->setTextColor(BLRgba32(muted ? 0xFFA0A0A0u : 0xFFF0F0F0u));
            if (muted) {
                setFontSize(label, kDescriptionSize);
            }
            parent.addChild(label);
            return label;
        }
    }

    SwatchesTab::SwatchesTab(SketchDocument& document, SubView& parent) : document_(document), parent_(parent) {
        scroll_ = new ScrollView();
        parent_.addChild(scroll_);
        View* loaded = Bundle::instance().loadView("swatchesTab");
        root_ = dynamic_cast<SubView*>(loaded);
        if (root_ == nullptr) {
            delete loaded;
            throw std::runtime_error("SwatchesTab: Resources/swatchesTab.newui did not load");
        }
        scroll_->addChild(root_);
        root_->setLayout(nullptr);   // the sections are stacked by hand: their heights come from what the palettes hold

        paletteButton_ = find<Button>("paletteName");
        moreButton_ = find<Button>("moreButton");
        paletteRows_ = find<SubView>("paletteRows");
        grid_ = find<SubView>("swatchGrid");
        details_ = find<SubView>("details");
        detailColor_ = find<SubView>("detailColor");
        detailName_ = find<TextField>("detailName");
        detailHex_ = find<TextField>("detailHex");
        detailLinked_ = find<Toggle>("detailLinked");
        usedBy_ = find<Label>("detailUsedBy");
        selectThem_ = find<Button>("selectThem");
        useAsFill_ = find<Button>("useAsFill");
        gridTitle_ = find<Label>("gridTitle");
        inUseTitle_ = find<Label>("inUseTitle");
        inUseList_ = find<SubView>("inUseList");
        addAll_ = find<Button>("addAllInUse");
        status_ = find<Label>("status");
        setFontSize(status_, kDescriptionSize);
        setFontSize(usedBy_, kDescriptionSize);
        if (auto* statusStyle = dynamic_cast<LabelStyle*>(&status_->style())) {
            statusStyle->setWordWrap(false);   // one line: a message that is too long is cut, not allowed to push the row's height
        }
        detailsHeight_ = details_->bounds().height();
        emptyNote_ = makeLabel(*root_, true);
        emptyNote_->setVisible(false);
        emptyNote_->setText("Nothing is open. The swatches of a document show here.");
        if (auto* noteStyle = dynamic_cast<LabelStyle*>(&emptyNote_->style())) {
            noteStyle->setWordWrap(true);
        }

        for (SubView* container : {paletteRows_, grid_, inUseList_}) {
            clearBuiltIn(container);   // the mockup's sample rows, tiles and colours
            container->setLayout(nullptr);
        }
        renameField_ = new TextField();
        renameField_->setTextColor(BLRgba32(0xFFF0F0F0u));
        renameField_->setVisible(false);
        root_->addChild(renameField_);

        wire();
        connection_ = document_.onPalettesChanged.add([this](SketchDocument&) {
            refresh();
            return SyncReturn::Handled;
        });
        refresh();
    }

    SwatchesTab::~SwatchesTab() {
        document_.onPalettesChanged.remove(connection_);
    }

    void SwatchesTab::clearBuiltIn(SubView* container) {
        if (container == nullptr) {
            return;
        }
        for (SubView* child : std::vector<SubView*>(container->childViews())) {
            container->removeChild(child);
            delete child;
        }
    }

    void SwatchesTab::wire() {
        paletteButton_->onClick += [this](Control&) {
            popup(paletteListMenu(), *paletteButton_, Point(0.0f, paletteButton_->bounds().height()));
            return SyncReturn::Handled;
        };
        moreButton_->onClick += [this](Control&) {
            popup(paletteMoreMenu(), *moreButton_, Point(0.0f, moreButton_->bounds().height()));
            return SyncReturn::Handled;
        };
        const auto commitName = [this](TextField&) {
            if (!updating_) {
                setName(narrow(detailName_->text()));
            }
            return SyncReturn::Handled;
        };
        detailName_->onReturnPressed += commitName;
        detailName_->onLostFocus += [this](View&) {
            if (!updating_) {
                setName(narrow(detailName_->text()));
            }
            return SyncReturn::Handled;
        };
        detailHex_->onReturnPressed += [this](TextField&) {
            if (!updating_) {
                setHex(narrow(detailHex_->text()));
            }
            return SyncReturn::Handled;
        };
        detailHex_->onLostFocus += [this](View&) {
            if (!updating_ && narrow(detailHex_->text()) != hexText()) {
                setHex(narrow(detailHex_->text()));
            }
            return SyncReturn::Handled;
        };
        detailLinked_->onCheckedChanged += [this](Toggle& changed) {
            if (!updating_) {
                setLinked(changed.isChecked());
            }
            return SyncReturn::Handled;
        };
        selectThem_->onClick += [this](Control&) {
            selectUsers();
            return SyncReturn::Handled;
        };
        useAsFill_->onClick += [this](Control&) {
            useSelected();
            return SyncReturn::Handled;
        };
        addAll_->onClick += [this](Control&) {
            addAllInUse();
            return SyncReturn::Handled;
        };
        renameField_->onReturnPressed += [this](TextField&) {
            if (renameField_->isVisible()) {
                const std::string name = narrow(renameField_->text());
                renameField_->setVisible(false);
                renameActivePalette(name);
            }
            return SyncReturn::Handled;
        };
        renameField_->onLostFocus += [this](View&) {
            renameField_->setVisible(false);
            return SyncReturn::Handled;
        };
    }

    // ---- State ----

    const std::vector<Swatch>& SwatchesTab::shown() const {
        return showingRecent_ ? document_.recentColors() : document_.palettes()[document_.activePalette()].swatches;
    }

    void SwatchesTab::say(const std::string& message) {
        if (status_ != nullptr) {
            status_->setText(message);
        }
        if (onMessage) {
            onMessage(message);
        }
    }

    void SwatchesTab::guarded(const std::function<void()>& edit) {
        try {
            edit();
        } catch (const std::runtime_error& problem) {
            say(problem.what());
            refresh();
        }
    }

    void SwatchesTab::setDocumentOpen(bool open) {
        documentOpen_ = open;
        refresh();
    }

    void SwatchesTab::refresh() {
        if (updating_) {
            return;
        }
        const std::vector<Swatch>& list = shown();
        if (selected_ >= int(list.size())) {
            selected_ = int(list.size()) - 1;
        }
        if (selected_ < 0 && !list.empty()) {
            selected_ = 0;
        }
        inUse_ = document_.colorsInUse(true);
        refreshPalettes();
        refreshTiles();
        refreshDetails();
        refreshInUse();
        layoutAll(lastWidth_);
    }

    // ---- Palette rows ----

    SwatchesTab::PaletteRow& SwatchesTab::paletteRowAt(std::size_t index) {
        while (paletteViews_.size() <= index) {
            const std::size_t at = paletteViews_.size();
            PaletteRow row;
            row.view = new SubView();
            row.view->style().setBackgroundColor(BLRgba32(0xFF2E2E2Eu));
            row.view->style().setBorderWidth(1.0f);
            row.view->setCursor(Cursor(CursorKind::Hand));
            paletteRows_->addChild(row.view);
            row.name = makeLabel(*row.view, false);
            row.count = makeLabel(*row.view, true);
            row.count->setTextAlignment(TextAlignment::Right);
            row.strip = new SubView();
            row.strip->setVisible(true);
            row.view->addChild(row.strip);
            for (std::size_t i = 0; i < kSegments; ++i) {
                auto* segment = new SubView();
                segment->setVisible(false);
                row.strip->addChild(segment);
                row.segments.push_back(segment);
            }
            const auto choose = [this, at](View&, const Point&, std::uint32_t buttons, std::uint32_t) {
                if ((buttons & mbmLeftButton) != 0) {
                    selectPalette(at);
                    return SyncReturn(SyncReturn::Handled);
                }
                return SyncReturn(SyncReturn::Ignored);
            };
            row.view->onMouseDown += choose;
            row.name->onMouseDown += choose;
            row.count->onMouseDown += choose;
            row.strip->onMouseDown += choose;
            paletteViews_.push_back(row);
        }
        return paletteViews_[index];
    }

    void SwatchesTab::refreshPalettes() {
        const std::vector<Palette>& palettes = document_.palettes();
        shownRows_ = palettes.size() + 1;
        for (std::size_t i = 0; i < paletteViews_.size() || i < shownRows_; ++i) {
            PaletteRow& row = paletteRowAt(i);
            row.view->setVisible(i < shownRows_);
            if (i >= shownRows_) {
                continue;
            }
            const bool recent = i == palettes.size();
            const std::vector<Swatch>& swatches = recent ? document_.recentColors() : palettes[i].swatches;
            row.name->setText(recent ? "Recent" : palettes[i].name);
            row.count->setText(recent ? "automatic" : std::to_string(swatches.size()));
            const bool active = i == activeRow();
            row.view->style().setBorderFill(active ? Color(1.0f, 0.71f, 0.33f) : Color(0.29f, 0.29f, 0.29f));
            const std::size_t segments = (std::min)(swatches.size(), kSegments);
            for (std::size_t s = 0; s < row.segments.size(); ++s) {
                row.segments[s]->setVisible(s < segments);
                if (s < segments) {
                    row.segments[s]->style().setBackgroundColor(colorOf(swatches[s * swatches.size() / segments]));
                }
            }
        }
    }

    // ---- Tiles ----

    SwatchesTab::Tile& SwatchesTab::tileAt(std::size_t index) {
        while (tiles_.size() <= index) {
            const std::size_t at = tiles_.size();
            Tile tile;
            tile.view = new SubView();
            tile.view->setCursor(Cursor(CursorKind::Hand));
            grid_->addChild(tile.view);
            tile.chip = new SubView();
            tile.chip->setVisible(true);
            tile.chip->style().setBorderWidth(1.0f);
            tile.view->addChild(tile.chip);
            const auto act = [this, at](View& source, const Point& local, std::uint32_t buttons, std::uint32_t) {
                if ((buttons & mbmRightButton) != 0) {
                    selectSwatch(at);
                    popup(swatchMenu(at), source, local);
                    return SyncReturn(SyncReturn::Handled);
                }
                if ((buttons & mbmLeftButton) != 0) {
                    selectSwatch(at);
                    return SyncReturn(SyncReturn::Handled);
                }
                return SyncReturn(SyncReturn::Ignored);
            };
            tile.view->onMouseDown += act;
            tile.chip->onMouseDown += act;
            tiles_.push_back(tile);
        }
        return tiles_[index];
    }

    void SwatchesTab::refreshTiles() {
        const std::vector<Swatch>& list = shown();
        shownTiles_ = list.size();
        for (std::size_t i = 0; i < tiles_.size() || i < shownTiles_; ++i) {
            Tile& tile = tileAt(i);
            tile.view->setVisible(i < shownTiles_);
            if (i >= shownTiles_) {
                continue;
            }
            const Swatch& swatch = list[i];
            tile.chip->style().setBackgroundColor(colorOf(swatch));
        }
        paintSelection();
        if (gridTitle_ != nullptr) {
            gridTitle_->setText(upper(showingRecent_ ? std::string("Recent") : document_.palettes()[document_.activePalette()].name));
        }
        if (paletteButton_ != nullptr) {
            paletteButton_->setText(showingRecent_ ? std::string("Recent") : document_.palettes()[document_.activePalette()].name);
        }
    }

    void SwatchesTab::paintSelection() {
        for (std::size_t i = 0; i < shownTiles_ && i < tiles_.size(); ++i) {
            const bool selected = int(i) == selected_;
            tiles_[i].chip->style().setBorderWidth(selected ? 2.0f : 1.0f);
            tiles_[i].chip->style().setBorderFill(selected ? Color(1.0f, 0.71f, 0.33f) : Color(0.0f, 0.0f, 0.0f));
        }
    }

    // ---- Details ----

    void SwatchesTab::refreshDetails() {
        const std::vector<Swatch>& list = shown();
        const bool has = selected_ >= 0 && selected_ < int(list.size());
        details_->setVisible(has && documentOpen_);
        if (!has) {
            return;
        }
        const Swatch& swatch = list[std::size_t(selected_)];
        const bool editable = !showingRecent_;
        updating_ = true;
        detailColor_->style().setBackgroundColor(colorOf(swatch));
        detailName_->setText(widen(swatch.name));
        detailHex_->setText(widen(hexOf(swatch)));
        detailLinked_->setChecked(swatch.link != 0);
        updating_ = false;
        detailName_->setEnabled(editable);
        detailHex_->setEnabled(editable);
        detailLinked_->setEnabled(editable);
        if (showingRecent_) {
            usedBy_->setText("A recent colour: save it to a palette to keep it");
            selectThem_->setEnabled(false);
        } else {
            const ColorUse use = document_.swatchUse(document_.activePalette(), std::size_t(selected_));
            usedBy_->setText(use.rows == 0 ? std::string("Not used by any shape")
                                           : "Used " + plural(use.rows, "time", "times") + " in " + plural(use.groups, "group", "groups"));
            selectThem_->setEnabled(use.rows > 0);
        }
    }

    // ---- Colours in use ----

    SwatchesTab::UseRow& SwatchesTab::useRowAt(std::size_t index) {
        while (useRows_.size() <= index) {
            const std::size_t at = useRows_.size();
            UseRow row;
            row.view = new SubView();
            inUseList_->addChild(row.view);
            row.chip = new SubView();
            row.chip->setVisible(true);
            row.chip->style().setBorderWidth(1.0f);
            row.chip->style().setBorderFill(Color(0.0f, 0.0f, 0.0f));
            row.view->addChild(row.chip);
            row.hex = makeLabel(*row.view, false);
            row.info = makeLabel(*row.view, true);
            row.add = new Button();
            row.add->setText("+");
            row.add->onClick += [this, at](Control&) {
                addInUse(at);
                return SyncReturn::Handled;
            };
            row.view->addChild(row.add);
            useRows_.push_back(row);
        }
        return useRows_[index];
    }

    void SwatchesTab::refreshInUse() {
        shownUses_ = inUse_.size();
        for (std::size_t i = 0; i < useRows_.size() || i < shownUses_; ++i) {
            UseRow& row = useRowAt(i);
            row.view->setVisible(i < shownUses_);
            if (i >= shownUses_) {
                continue;
            }
            const ColorUse& use = inUse_[i];
            row.chip->style().setBackgroundColor(colorOf(use.color));
            row.hex->setText(hexOf(use.color));
            row.info->setText(plural(use.rows, "use", "uses") + ", " + plural(use.groups, "group", "groups"));
            row.add->setEnabled(!showingRecent_);
        }
        if (inUseTitle_ != nullptr) {
            inUseTitle_->setText("IN USE, IN NO PALETTE" + (inUse_.empty() ? std::string() : " (" + std::to_string(inUse_.size()) + ")"));
        }
        addAll_->setEnabled(!inUse_.empty() && !showingRecent_);
    }

    // ---- Layout ----

    void SwatchesTab::layoutFor(float width, float height) {
        lastWidth_ = (std::max)(width, 120.0f);
        layoutAll(lastWidth_);
        scroll_->setBounds(Rect(0.0f, 0.0f, width, height));
        scroll_->setContentSize(Size(width, (std::max)(contentHeight_, height)));
    }

    void SwatchesTab::layoutAll(float width) {
        const float inner = (std::max)(width - 2.0f * kPad, 60.0f);
        float y = kPad;
        const float heightBefore = contentHeight_;
        const auto heightChanged = [this, heightBefore]() {
            // refresh() reaches here without layoutFor(), and a capped panel does not resize to trigger it
            scroll_->setContentSize(Size(scroll_->bounds().width(), (std::max)(contentHeight_, scroll_->bounds().height())));
            if (contentHeight_ != heightBefore && onContentHeightChanged) {
                onContentHeightChanged();
            }
        };
        for (const char* name : {"head", "palettesTitle", "bottomRow"}) {
            if (View* section = root_->findView(name)) {
                section->setVisible(documentOpen_);
            }
        }
        for (View* section : std::initializer_list<View*>{paletteRows_, grid_, inUseList_, gridTitle_, inUseTitle_}) {
            section->setVisible(documentOpen_);
        }
        renameField_->setVisible(renameField_->isVisible() && documentOpen_);
        emptyNote_->setVisible(!documentOpen_);
        if (!documentOpen_) {
            details_->setVisible(false);
            emptyNote_->setBounds(Rect(kPad, kPad, inner, 40.0f));
            contentHeight_ = 2.0f * kPad + 40.0f;
            root_->setBounds(Rect(0.0f, 0.0f, width, contentHeight_));
            heightChanged();
            return;
        }
        const auto place = [&](View* view, float height) {
            if (view != nullptr && view->isVisible()) {
                view->setBounds(Rect(kPad, y, inner, height));
                y += height + kGap;
            }
        };
        View* head = root_->findView("head");
        place(head, kHeadHeight);
        if (head != nullptr) {
            head->updateLayout();
            if (renameField_ != nullptr && paletteButton_ != nullptr) {
                const Rect button = paletteButton_->bounds();
                renameField_->setBounds(Rect(head->bounds().left() + button.left(), head->bounds().top() + button.top(), button.width(), button.height()));
            }
        }
        place(root_->findView("palettesTitle"), kTitleHeight);

        const float rowsHeight = float(shownRows_) * kRowHeight + float(shownRows_ > 0 ? shownRows_ - 1 : 0) * kRowGap;
        place(paletteRows_, rowsHeight);
        for (std::size_t i = 0; i < shownRows_ && i < paletteViews_.size(); ++i) {
            PaletteRow& row = paletteViews_[i];
            row.view->setBounds(Rect(0.0f, float(i) * (kRowHeight + kRowGap), inner, kRowHeight));
            row.name->setBounds(Rect(6.0f, 4.0f, (std::max)(inner - 90.0f, 20.0f), 18.0f));
            row.count->setBounds(Rect(inner - 84.0f, 4.0f, 78.0f, 18.0f));
            row.strip->setBounds(Rect(6.0f, 26.0f, inner - 12.0f, 12.0f));
            const std::size_t held = i == document_.palettes().size() ? document_.recentColors().size() : document_.palettes()[i].swatches.size();
            const std::size_t segments = (std::min)(held, kSegments);
            for (std::size_t s = 0; s < segments; ++s) {
                const float left = std::floor((inner - 12.0f) * float(s) / float(segments));
                const float right = std::floor((inner - 12.0f) * float(s + 1) / float(segments));
                row.segments[s]->setBounds(Rect(left, 0.0f, right - left, 12.0f));
            }
        }

        place(gridTitle_, kTitleHeight);
        const std::size_t columns = (std::max<std::size_t>)(1, std::size_t((inner + kTileGap) / (kTileWidth + kTileGap)));
        const std::size_t tileRows = (shownTiles_ + columns - 1) / columns;
        const float gridHeight = tileRows == 0 ? 0.0f : float(tileRows) * kTileHeight + float(tileRows - 1) * kTileGap;
        place(grid_, gridHeight);
        for (std::size_t i = 0; i < shownTiles_ && i < tiles_.size(); ++i) {
            Tile& tile = tiles_[i];
            tile.view->setBounds(Rect(float(i % columns) * (kTileWidth + kTileGap), float(i / columns) * (kTileHeight + kTileGap), kTileWidth, kTileHeight));
            tile.chip->setBounds(Rect(0.0f, 0.0f, kTileWidth, kTileChipHeight));
        }

        if (details_->isVisible()) {
            place(details_, detailsHeight_);
            details_->updateLayout();
        }

        place(root_->findView("inUseTitle"), kTitleHeight);
        place(inUseList_, float(shownUses_) * kUseHeight);
        for (std::size_t i = 0; i < shownUses_ && i < useRows_.size(); ++i) {
            UseRow& row = useRows_[i];
            row.view->setBounds(Rect(0.0f, float(i) * kUseHeight, inner, kUseHeight));
            row.chip->setBounds(Rect(0.0f, 4.0f, 18.0f, 18.0f));
            row.hex->setBounds(Rect(26.0f, 4.0f, 62.0f, 18.0f));
            row.info->setBounds(Rect(92.0f, 4.0f, (std::max)(inner - 92.0f - 34.0f, 20.0f), 18.0f));
            row.add->setBounds(Rect(inner - 28.0f, 0.0f, 28.0f, kUseHeight - 2.0f));
        }
        place(root_->findView("bottomRow"), kHeadHeight);
        contentHeight_ = y - kGap + kPad;
        root_->setBounds(Rect(0.0f, 0.0f, width, contentHeight_));
        root_->style().markDirty();
        heightChanged();
    }

    // ---- Menus ----

    void SwatchesTab::popup(const std::vector<PanelMenuEntry>& entries, View& at, const Point& local) {
        if (presentMenu && !entries.empty()) {
            presentMenu(entries, at.localToScreen(local));
        }
    }

    std::vector<PanelMenuEntry> SwatchesTab::paletteListMenu() {
        std::vector<PanelMenuEntry> entries;
        const std::vector<Palette>& palettes = document_.palettes();
        for (std::size_t i = 0; i < palettes.size(); ++i) {
            PanelMenuEntry entry;
            entry.text = palettes[i].name;
            entry.checked = !showingRecent_ && i == document_.activePalette();
            entry.run = [this, i]() { selectPalette(i); };
            entries.push_back(entry);
        }
        PanelMenuEntry line;
        line.separator = true;
        entries.push_back(line);
        PanelMenuEntry recent;
        recent.text = "Recent (automatic)";
        recent.checked = showingRecent_;
        recent.run = [this]() { selectPalette(document_.palettes().size()); };
        entries.push_back(recent);
        return entries;
    }

    std::vector<PanelMenuEntry> SwatchesTab::paletteMoreMenu() {
        std::vector<PanelMenuEntry> entries;
        const auto add = [&](const char* text, std::function<void()> run) {
            PanelMenuEntry entry;
            entry.text = text;
            entry.run = std::move(run);
            entries.push_back(entry);
        };
        const auto line = [&]() {
            PanelMenuEntry entry;
            entry.separator = true;
            entries.push_back(entry);
        };
        add("New palette", [this]() {
            guarded([&]() {
                const std::size_t added = document_.addPalette("Palette");
                showingRecent_ = false;
                selected_ = -1;
                document_.setActivePalette(added);
            });
        });
        if (!showingRecent_) {
            add("Duplicate", [this]() {
                guarded([&]() {
                    const std::size_t copy = document_.duplicatePalette(document_.activePalette());
                    selected_ = -1;
                    document_.setActivePalette(copy);
                });
            });
            add("Rename...", [this]() {
                renameField_->setText(widen(document_.palettes()[document_.activePalette()].name));
                renameField_->setVisible(true);
                if (RootView* rootView = renameField_->rootView()) {
                    rootView->setFocusedSubView(renameField_);
                }
                renameField_->selectAll();
            });
        }
        line();
        add("Import palette (.ase, .aco)...", [this]() {
            if (onImportPalette) {
                onImportPalette();
            }
        });
        add("New palette from image...", [this]() {
            if (onPaletteFromImage) {
                onPaletteFromImage();
            }
        });
        if (!showingRecent_) {
            add("Add colours in use", [this]() { addAllInUse(); });
            if (document_.palettes().size() >= 2) {
                line();
                add("Delete", [this]() {
                    guarded([&]() {
                        const std::string name = document_.palettes()[document_.activePalette()].name;
                        selected_ = -1;
                        document_.deletePalette(document_.activePalette());
                        say("Deleted the palette " + name + " (undo brings it back)");
                    });
                });
            }
        }
        return entries;
    }

    std::vector<PanelMenuEntry> SwatchesTab::swatchMenu(std::size_t index) {
        std::vector<PanelMenuEntry> entries;
        const auto add = [&](const std::string& text, std::function<void()> run) {
            PanelMenuEntry entry;
            entry.text = text;
            entry.run = std::move(run);
            entries.push_back(entry);
        };
        add("Use", [this, index]() {
            selectSwatch(index);
            useSelected();
        });
        add("Select shapes using this", [this, index]() {
            selectSwatch(index);
            selectUsers();
        });
        if (showingRecent_) {
            add("Save to palette", [this, index]() {
                guarded([&]() {
                    const std::vector<Swatch>& recent = document_.recentColors();
                    if (index < recent.size()) {
                        const Swatch swatch = recent[index];
                        document_.addSwatch(document_.activePalette(), swatch);
                        say("Saved to the palette " + document_.palettes()[document_.activePalette()].name);
                    }
                });
            });
            return entries;
        }
        const bool linked = index < shown().size() && shown()[index].link != 0;
        add(linked ? "Unlink" : "Link", [this, index, linked]() {
            selectSwatch(index);
            setLinked(!linked);
        });
        add("Rename...", [this, index]() {
            selectSwatch(index);
            if (RootView* rootView = detailName_->rootView()) {
                rootView->setFocusedSubView(detailName_);
            }
            detailName_->selectAll();
        });
        PanelMenuEntry line;
        line.separator = true;
        entries.push_back(line);
        add("Delete", [this, index]() {
            guarded([&]() {
                document_.deleteSwatch(document_.activePalette(), index);
            });
        });
        return entries;
    }

    // ---- Actions ----

    void SwatchesTab::selectPalette(std::size_t row) {
        guarded([&]() {
            selected_ = -1;
            if (row >= document_.palettes().size()) {
                showingRecent_ = true;
                refresh();
                return;
            }
            showingRecent_ = false;
            document_.setActivePalette(row);
            refresh();
        });
    }

    void SwatchesTab::selectSwatch(std::size_t index) {
        if (index >= shown().size()) {
            return;
        }
        selected_ = int(index);
        paintSelection();
        refreshDetails();
        layoutAll(lastWidth_);
    }

    void SwatchesTab::setName(const std::string& text) {
        if (showingRecent_ || selected_ < 0 || selected_ >= int(shown().size())) {
            return;
        }
        if (text == shown()[std::size_t(selected_)].name) {
            return;
        }
        guarded([&]() { document_.renameSwatch(document_.activePalette(), std::size_t(selected_), text); });
    }

    void SwatchesTab::setHex(const std::string& text) {
        if (showingRecent_ || selected_ < 0 || selected_ >= int(shown().size())) {
            return;
        }
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
        bool hasAlpha = false;
        if (!NodePanel::parseColor(text, r, g, b, a, hasAlpha)) {
            say("'" + text + "' is not a colour (use #RRGGBB or #RRGGBBAA)");
            refreshDetails();
            return;
        }
        Swatch replacement = shown()[std::size_t(selected_)];
        replacement.r = r;
        replacement.g = g;
        replacement.b = b;
        if (hasAlpha) {
            replacement.a = a;
        }
        if (replacement.sameColor(shown()[std::size_t(selected_)])) {
            refreshDetails();
            return;
        }
        guarded([&]() { document_.replaceSwatch(document_.activePalette(), std::size_t(selected_), replacement); });
    }

    void SwatchesTab::setLinked(bool linked) {
        if (showingRecent_ || selected_ < 0 || selected_ >= int(shown().size())) {
            return;
        }
        guarded([&]() {
            document_.setSwatchLinked(document_.activePalette(), std::size_t(selected_), linked);
            say(linked ? "Linked: using it on a colour ties the colour to the swatch" : "Unlinked: colours keep their values");
        });
    }

    void SwatchesTab::selectUsers() {
        if (showingRecent_) {
            say("Save the colour to a palette first, then select the shapes that use it");
            return;
        }
        if (selected_ < 0 || selected_ >= int(shown().size())) {
            return;
        }
        guarded([&]() {
            std::vector<NodeRef> users;
            const ColorUse use = document_.swatchUse(document_.activePalette(), std::size_t(selected_), &users);
            if (users.empty()) {
                say("No shape uses this colour");
                return;
            }
            if (onSelectUsers) {
                onSelectUsers(users);
            }
            say("Selected " + plural(use.rows, "colour row", "colour rows") + " in " + plural(use.groups, "group", "groups"));
        });
    }

    void SwatchesTab::useSelected() {
        if (selected_ >= 0 && selected_ < int(shown().size()) && onUse) {
            onUse(shown()[std::size_t(selected_)]);
        }
    }

    void SwatchesTab::addInUse(std::size_t index) {
        if (showingRecent_ || index >= inUse_.size()) {
            return;
        }
        guarded([&]() {
            Swatch added = inUse_[index].color;
            added.name.clear();
            document_.addSwatch(document_.activePalette(), added);
        });
    }

    void SwatchesTab::addAllInUse() {
        guarded([&]() {
            if (inUse_.empty()) {
                say("Every colour in use is already in a palette");
                return;
            }
            std::vector<Swatch> missing;
            for (const ColorUse& use : inUse_) {
                missing.push_back(use.color);
            }
            document_.addSwatches(document_.activePalette(), missing);
            say("Added " + plural(missing.size(), "colour", "colours") + " that were in use");
        });
    }

    void SwatchesTab::renameActivePalette(const std::string& name) {
        if (showingRecent_) {
            return;
        }
        guarded([&]() { document_.renamePalette(document_.activePalette(), name); });
    }

    // ---- For tests ----

    std::size_t SwatchesTab::paletteRowCount() const {
        return shownRows_;
    }

    std::size_t SwatchesTab::tileCount() const {
        return shownTiles_;
    }

    std::string SwatchesTab::paletteName() const {
        return paletteButton_ != nullptr ? paletteButton_->text() : std::string();
    }

    std::string SwatchesTab::nameText() const {
        return narrow(detailName_->text());
    }

    std::string SwatchesTab::hexText() const {
        return narrow(detailHex_->text());
    }

    bool SwatchesTab::linkedChecked() const {
        return detailLinked_->isChecked();
    }

    std::string SwatchesTab::usedByText() const {
        return usedBy_->text();
    }

    bool SwatchesTab::selectThemEnabled() const {
        return selectThem_->isEnabled();
    }

    std::size_t SwatchesTab::inUseCount() const {
        return shownUses_;
    }

    namespace {
        // Visible, with a size, and so is every view above it up to `top` (the tab's own root: whether the panel's tab is the one showing is not its business).
        bool drawn(const View* view, const View* top) {
            if (view == nullptr || view->bounds().width() <= 0.0f || view->bounds().height() <= 0.0f) {
                return false;
            }
            for (const View* above = view; above != nullptr; above = above->parent()) {
                if (!above->isVisible()) {
                    return false;
                }
                if (above == top) {
                    break;
                }
            }
            return true;
        }
    }

    bool SwatchesTab::tileDrawn(std::size_t index) const {
        return index < tiles_.size() && index < shownTiles_ && drawn(tiles_[index].view, root_) && drawn(tiles_[index].chip, root_);
    }

    bool SwatchesTab::paletteStripDrawn(std::size_t row) const {
        if (row >= paletteViews_.size() || row >= shownRows_ || !drawn(paletteViews_[row].view, root_) || !drawn(paletteViews_[row].strip, root_)) {
            return false;
        }
        return std::any_of(paletteViews_[row].segments.begin(), paletteViews_[row].segments.end(), [this](const SubView* segment) { return drawn(segment, root_); });
    }

    bool SwatchesTab::inUseChipDrawn(std::size_t index) const {
        return index < useRows_.size() && index < shownUses_ && drawn(useRows_[index].view, root_) && drawn(useRows_[index].chip, root_);
    }

    bool SwatchesTab::hasSize() const {
        return scroll_->bounds().width() > 0.0f && scroll_->bounds().height() > 0.0f;
    }

    std::string SwatchesTab::emptyNoteText() const {
        return emptyNote_->text();
    }

    bool SwatchesTab::emptyNoteShown() const {
        return emptyNote_->isVisible();
    }

    std::string SwatchesTab::statusText() const {
        return status_->text();
    }

}
