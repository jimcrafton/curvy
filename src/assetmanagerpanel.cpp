#include "curvy/assetmanagerpanel.h"

#include <newui/bundle.h>
#include <newui/cursor.h>
#include <newui/models.h>
#include <newui/mouse_constants.h>

#include <algorithm>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kPad = 8.0f;
        constexpr float kTitleRow = 22.0f;
        constexpr float kAssetRow = 26.0f;
        constexpr float kMinHeight = 420.0f;
        constexpr float kPreferredHeight = 520.0f;
        constexpr float kActionsHeight = 62.0f;
        constexpr float kActionHeight = 22.0f;
        constexpr float kMinList = 80.0f;
        constexpr std::uint32_t kSelected = 0xFF3A2F1Fu;
        constexpr std::uint32_t kAmber = 0xFFFFB454u;
        constexpr std::uint32_t kMuted = 0xFFA0A0A0u;

        const char* const kActionButtons[] = {"addInstanceButton", "editButton", "unlockButton", "reloadButton", "saveToLibraryButton", "uninstallButton"};

        std::wstring widen(const std::string& text) {
            return std::wstring(text.begin(), text.end());
        }

        std::string narrow(const std::wstring& text) {
            return std::string(text.begin(), text.end());
        }

        std::string upper(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            return text;
        }

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
            label->setTextColor(BLRgba32(muted ? kMuted : 0xFFF0F0F0u));
            parent.addChild(label);
            return label;
        }
    }

    AssetManagerPanel::AssetManagerPanel(std::shared_ptr<AssetManagerModel> model)
        : Panel("assets", "Assets"), modelHolder_(std::move(model)), model_(*modelHolder_) {
        View* loaded = Bundle::instance().loadView("assetManager");
        root_ = dynamic_cast<SubView*>(loaded);
        if (root_ == nullptr) {
            delete loaded;
            throw std::runtime_error("AssetManagerPanel: Resources/assetManager.newui did not load");
        }
        body().addChild(root_);
        root_->setLayout(nullptr);

        // The mockup wraps everything in one more box; the rest is stacked by hand because the list's height is what is left over.
        auto* content = find<SubView>("assetPanel");
        header_ = find<SubView>("assetHeader");
        filter_ = find<SubView>("assetFilterBar");
        list_ = find<SubView>("assetList");
        banner_ = find<SubView>("detailsBanner");
        details_ = find<SubView>("assetDetails");
        actions_ = find<SubView>("assetActions");
        status_ = find<Label>("assetStatus");
        bannerText_ = find<Label>("detailsBannerText");
        search_ = find<TextField>("assetSearch");
        category_ = find<DropDownList>("assetCategory");
        if (content == nullptr || header_ == nullptr || filter_ == nullptr || list_ == nullptr || banner_ == nullptr || details_ == nullptr ||
            actions_ == nullptr || status_ == nullptr) {
            throw std::runtime_error("AssetManagerPanel: Resources/assetManager.newui lacks a view the panel needs");
        }
        content->setLayout(nullptr);
        headerHeight_ = header_->bounds().height();
        filterHeight_ = filter_->bounds().height();
        bannerHeight_ = banner_->bounds().height();
        detailsHeight_ = details_->bounds().height();
        statusHeight_ = (std::max)(status_->bounds().height(), 20.0f);

        for (SubView* child : std::vector<SubView*>(list_->childViews())) {   // the mockup's sample rows
            list_->removeChild(child);
            delete child;
        }
        list_->setLayout(nullptr);
        listScroll_ = new ScrollView();
        list_->addChild(listScroll_);
        rowsRoot_ = new SubView();
        rowsRoot_->setVisible(true);
        listScroll_->addChild(rowsRoot_);
        actions_->setLayout(nullptr);
        setFontSize(status_, 11.0f);
        status_->setTextColor(BLRgba32(kMuted));

        wire();
        model_.onChanged = [this]() { refresh(); };
        body().onSizeChanged += [this](View&, const Size&) {
            layoutAll();
            return SyncReturn::Ignored;
        };
        setPreferredBodyHeight(kPreferredHeight);
        refresh();
    }

    AssetManagerPanel::~AssetManagerPanel() {
        model_.onChanged = nullptr;
        if (onDestroyed) {
            onDestroyed();
        }
    }

    void AssetManagerPanel::say(const std::string& message) {
        if (onMessage) {
            onMessage(message);
        }
    }

    void AssetManagerPanel::report(const std::string& error, const std::string& done) {
        if (!error.empty()) {
            say(error);
        } else if (!done.empty()) {
            say(done);
        }
    }

    void AssetManagerPanel::wire() {
        const auto click = [this](const char* name, std::function<void()> action) {
            if (auto* button = find<Button>(name)) {
                button->onClick += [action](Control&) {
                    action();
                    return SyncReturn::Handled;
                };
            }
        };
        click("assetNewButton", [this]() {
            if (onNewRequested) {
                onNewRequested();
            }
        });
        click("assetFolderButton", [this]() {
            auto* more = find<Button>("assetFolderButton");
            if (!presentMenu || more == nullptr) {
                return;
            }
            std::vector<PanelMenuEntry> entries;
            entries.push_back({"Load asset file...", false, false, [this]() {
                if (onLoadFileRequested) {
                    onLoadFileRequested();
                }
            }});
            entries.push_back({"Load assets from folder...", false, false, [this]() {
                if (onLoadFolderRequested) {
                    onLoadFolderRequested();
                }
            }});
            presentMenu(entries, more->localToScreen(Point(0.0f, more->bounds().height())));
        });
        click("addInstanceButton", [this]() {
            const std::string name = model_.selected();
            report(model_.addInstance(), "Added " + name + " to the graph");
        });
        click("editButton", [this]() {
            if (onEditRequested) {
                onEditRequested();
            }
        });
        click("unlockButton", [this]() { report(model_.unlock(), "Unlocked the instance"); });
        click("reloadButton", [this]() { report(model_.reload(), "Reloaded " + model_.selected()); });
        click("saveToLibraryButton", [this]() {
            if (onSaveToLibraryRequested) {
                onSaveToLibraryRequested();
            }
        });
        click("uninstallButton", [this]() {
            const std::string name = model_.selected();
            report(model_.uninstall(), "Uninstalled " + name + " (its file is kept)");
        });
        click("updateInstancesButton", [this]() { report(model_.updateInstances(), "Updated the instances"); });

        if (search_ != nullptr) {
            search_->model().onChanged.add([this](Model&) {
                if (!updating_) {
                    model_.setFilterText(narrow(search_->text()));
                }
                return SyncReturn::Ignored;
            });
        }
        if (category_ != nullptr) {
            category_->onSelectionChanged += [this](DropDownList& changed) {
                const std::optional<std::size_t> index = changed.selectedIndex();
                if (!updating_ && index.has_value() && *index < categories_.size()) {
                    model_.setCategory(categories_[*index]);
                }
                return SyncReturn::Handled;
            };
        }
    }

    AssetManagerPanel::Row& AssetManagerPanel::rowAt(std::size_t index) {
        while (rows_.size() <= index) {
            const std::size_t at = rows_.size();
            Row row;
            row.view = new SubView();
            row.view->setVisible(true);
            row.view->setCursor(Cursor(CursorKind::Hand));
            rowsRoot_->addChild(row.view);
            row.name = makeLabel(*row.view, false);
            row.version = makeLabel(*row.view, true);
            row.version->setTextAlignment(TextAlignment::Right);
            for (Label*& badge : row.badges) {
                badge = makeLabel(*row.view, true);
                badge->setTextAlignment(TextAlignment::Center);
                setFontSize(badge, 10.0f);
                badge->style().setBorderWidth(1.0f);
                badge->style().setBackgroundColor(BLRgba32(0xFF2E2E2Eu));
            }
            const auto choose = [this, at](View&, const Point&, std::uint32_t buttons, std::uint32_t) {
                if ((buttons & mbmLeftButton) != 0 && at < rows_.size() && !rows_[at].asset.empty()) {
                    model_.select(rows_[at].asset);
                    return SyncReturn(SyncReturn::Handled);
                }
                return SyncReturn(SyncReturn::Ignored);
            };
            row.view->onMouseDown += choose;
            row.name->onMouseDown += choose;
            row.version->onMouseDown += choose;
            for (Label* badge : row.badges) {
                badge->onMouseDown += choose;
            }
            rows_.push_back(row);
        }
        return rows_[index];
    }

    void AssetManagerPanel::refresh() {
        updating_ = true;
        refreshCategories();
        refreshRows();
        refreshDetails();
        updating_ = false;
        layoutAll();
    }

    void AssetManagerPanel::refreshCategories() {
        if (category_ == nullptr) {
            return;
        }
        const std::vector<std::string> next = model_.categories();
        if (next != categories_) {
            categories_ = next;
            auto items = std::make_unique<StringListModel>();
            for (const std::string& title : categories_) {
                items->addItem(title);
            }
            category_->setModel(std::move(items));
        }
        const auto at = std::find(categories_.begin(), categories_.end(), model_.category());
        category_->setSelectedIndex(at == categories_.end() ? std::size_t(0) : std::size_t(at - categories_.begin()));
        if (search_ != nullptr && narrow(search_->text()) != model_.filterText()) {
            search_->setText(widen(model_.filterText()));
        }
    }

    void AssetManagerPanel::refreshRows() {
        std::size_t index = 0;
        for (const AssetManagerModel::Group& group : model_.groups()) {
            Row& heading = rowAt(index++);
            heading.asset.clear();
            heading.view->setName({});
            heading.badgeTexts.clear();
            heading.name->setText(upper(group.title));
            heading.name->setTextColor(BLRgba32(kMuted));
            setFontSize(heading.name, 10.0f);
            heading.version->setText({});
            heading.view->style().setBackgroundColor(BLRgba32(0x00000000u));
            for (Label* badge : heading.badges) {
                badge->setVisible(false);
            }
            for (const jitnodes::AssetEntry& entry : group.entries) {
                Row& row = rowAt(index++);
                row.asset = entry.name;
                row.view->setName("assetRow_" + entry.name);   // a GUI test finds the row by it
                row.name->setText(entry.name);
                row.name->setTextColor(BLRgba32(0xFFF0F0F0u));
                setFontSize(row.name, 12.0f);
                row.version->setText("v" + std::to_string(entry.version));
                row.badgeTexts.clear();
                if (AssetManagerModel::isBrush(entry)) {
                    row.badgeTexts.push_back("Brush");
                }
                if (entry.outdated > 0) {
                    row.badgeTexts.push_back("Outdated " + std::to_string(entry.outdated));
                }
                for (std::size_t b = 0; b < 2; ++b) {
                    const bool shown = b < row.badgeTexts.size();
                    row.badges[b]->setVisible(shown);
                    if (shown) {
                        row.badges[b]->setText(row.badgeTexts[b]);
                        row.badges[b]->setTextColor(BLRgba32(row.badgeTexts[b].rfind("Outdated", 0) == 0 ? kAmber : kMuted));
                    }
                }
                row.view->style().setBackgroundColor(BLRgba32(entry.name == model_.selected() ? kSelected : 0x00000000u));
            }
        }
        shownRows_ = index;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            rows_[i].view->setVisible(i < shownRows_);
        }
    }

    void AssetManagerPanel::refreshDetails() {
        const std::optional<AssetManagerModel::Details> details = model_.details();
        const auto set = [this](const char* name, const std::string& text) {
            if (auto* label = find<Label>(name)) {
                label->setText(text);
            }
        };
        if (!details) {
            set("detailsName", "No asset selected");
            for (const char* name : {"detailsLabel", "detailsInputs", "detailsParams", "detailsInstances", "detailsSource"}) {
                set(name, {});
            }
        } else {
            std::string params;
            for (const std::string& p : details->params) {
                params += (params.empty() ? "" : ", ") + p;
            }
            set("detailsName", details->name + "  v" + std::to_string(details->version));
            set("detailsLabel", details->label);
            set("detailsInputs", std::to_string(details->inputs));
            set("detailsParams", params);
            set("detailsInstances", std::to_string(details->instances) + " in this document");
            set("detailsSource", details->embedded ? std::string("Embedded in this document") : details->source);
        }
        const std::string banner = model_.bannerText();
        banner_->setVisible(!banner.empty());
        if (bannerText_ != nullptr) {
            bannerText_->setText(banner);
        }
        status_->setText(model_.statusText());

        const bool has = details.has_value();
        const auto enable = [this](const char* name, bool on) {
            if (auto* button = find<Button>(name)) {
                button->setEnabled(on);
            }
        };
        enable("addInstanceButton", has);
        enable("editButton", has);
        enable("unlockButton", has && details->instances > 0);
        enable("reloadButton", has && !details->embedded);
        enable("saveToLibraryButton", has && details->embedded);
        enable("uninstallButton", has);
    }

    void AssetManagerPanel::layoutAll() {
        const Rect own = body().bounds();
        const float width = own.width();
        const float height = (std::max)(own.height(), kMinHeight);
        root_->setBounds(Rect(0.0f, 0.0f, width, height));
        if (auto* content = find<SubView>("assetPanel")) {
            content->setBounds(Rect(0.0f, 0.0f, width, height));
        }
        body().setContentSize(Size(width, height));

        const bool bannerShown = banner_->isVisible();
        const float fixed = headerHeight_ + filterHeight_ + (bannerShown ? bannerHeight_ + kPad : 0.0f) + detailsHeight_ + kActionsHeight + statusHeight_;
        const float listHeight = (std::max)(height - fixed, kMinList);
        float y = 0.0f;
        header_->setBounds(Rect(0.0f, y, width, headerHeight_));
        y += headerHeight_;
        filter_->setBounds(Rect(0.0f, y, width, filterHeight_));
        y += filterHeight_;
        list_->setBounds(Rect(kPad, y, width - 2.0f * kPad, listHeight));
        y += listHeight;
        if (bannerShown) {
            y += kPad * 0.5f;
            banner_->setBounds(Rect(kPad, y, width - 2.0f * kPad, bannerHeight_));
            y += bannerHeight_ + kPad * 0.5f;
        }
        details_->setBounds(Rect(kPad, y, width - 2.0f * kPad, detailsHeight_));
        y += detailsHeight_;
        actions_->setBounds(Rect(0.0f, y, width, kActionsHeight));
        y += kActionsHeight;
        status_->setBounds(Rect(kPad, y, width - 2.0f * kPad, statusHeight_));

        // Six buttons in two rows of three.
        const float cell = (width - 2.0f * kPad - 2.0f * 6.0f) / 3.0f;
        for (std::size_t i = 0; i < std::size(kActionButtons); ++i) {
            if (auto* button = find<Button>(kActionButtons[i])) {
                button->setBounds(Rect(kPad + float(i % 3) * (cell + 6.0f), 6.0f + float(i / 3) * (kActionHeight + 6.0f), cell, kActionHeight));
            }
        }
        layoutRows(width - 2.0f * kPad);
        listScroll_->setBounds(Rect(0.0f, 0.0f, width - 2.0f * kPad, listHeight));
        root_->updateLayout();
    }

    void AssetManagerPanel::layoutRows(float width) {
        float y = 0.0f;
        for (std::size_t i = 0; i < shownRows_; ++i) {
            Row& row = rows_[i];
            const bool heading = row.asset.empty();
            const float rowHeight = heading ? kTitleRow : kAssetRow;
            row.view->setBounds(Rect(0.0f, y, width, rowHeight));
            float right = width - 8.0f;
            if (!heading) {
                row.version->setBounds(Rect(right - 30.0f, 4.0f, 30.0f, 18.0f));
                right -= 36.0f;
                for (std::size_t b = row.badgeTexts.size(); b-- > 0;) {
                    const float badgeWidth = 12.0f + 6.5f * float(row.badgeTexts[b].size());
                    row.badges[b]->setBounds(Rect(right - badgeWidth, 5.0f, badgeWidth, 16.0f));
                    right -= badgeWidth + 4.0f;
                }
            }
            row.name->setBounds(Rect(8.0f, heading ? 6.0f : 4.0f, (std::max)(right - 12.0f, 20.0f), 18.0f));
            y += rowHeight;
        }
        rowsRoot_->setBounds(Rect(0.0f, 0.0f, width, (std::max)(y, 1.0f)));
        listScroll_->setContentSize(Size(width, (std::max)(y, 1.0f)));
    }

    std::vector<std::string> AssetManagerPanel::shownNames() const {
        std::vector<std::string> names;
        for (std::size_t i = 0; i < shownRows_; ++i) {
            if (!rows_[i].asset.empty()) {
                names.push_back(rows_[i].asset);
            }
        }
        return names;
    }

    std::vector<std::string> AssetManagerPanel::shownTitles() const {
        std::vector<std::string> titles;
        for (std::size_t i = 0; i < shownRows_; ++i) {
            if (rows_[i].asset.empty()) {
                titles.push_back(rows_[i].name->text());
            }
        }
        return titles;
    }

    std::vector<std::string> AssetManagerPanel::badgesOf(const std::string& name) const {
        for (std::size_t i = 0; i < shownRows_; ++i) {
            if (rows_[i].asset == name) {
                return rows_[i].badgeTexts;
            }
        }
        return {};
    }

    std::string AssetManagerPanel::detail(const std::string& key) const {
        static const std::pair<const char*, const char*> kKeys[] = {{"name", "detailsName"},           {"label", "detailsLabel"},
                                                                    {"inputs", "detailsInputs"},       {"params", "detailsParams"},
                                                                    {"instances", "detailsInstances"}, {"file", "detailsSource"}};
        for (const auto& [shortName, viewName] : kKeys) {
            if (key == shortName) {
                const auto* label = find<Label>(viewName);
                return label == nullptr ? std::string() : label->text();
            }
        }
        return {};
    }

    bool AssetManagerPanel::bannerShown() const { return banner_->isVisible(); }
    std::string AssetManagerPanel::bannerText() const { return bannerText_ == nullptr ? std::string() : bannerText_->text(); }
    std::string AssetManagerPanel::statusText() const { return status_->text(); }

    bool AssetManagerPanel::buttonEnabled(const char* name) const {
        const auto* button = find<Button>(name);
        return button != nullptr && button->isEnabled();
    }

    void AssetManagerPanel::clickRow(const std::string& name) {
        for (std::size_t i = 0; i < shownRows_; ++i) {
            if (rows_[i].asset == name) {
                model_.select(name);
                return;
            }
        }
    }

    void AssetManagerPanel::clickButton(const char* name) {
        if (auto* button = find<Button>(name)) {
            if (button->isEnabled()) {
                button->onClick(*button);
            }
        }
    }

    void AssetManagerPanel::typeSearch(const std::string& text) {
        if (search_ != nullptr) {
            search_->setText(widen(text));
        }
    }

    void AssetManagerPanel::chooseCategory(const std::string& category) {
        const auto at = std::find(categories_.begin(), categories_.end(), category);
        if (category_ != nullptr && at != categories_.end()) {
            category_->setSelectedIndex(std::size_t(at - categories_.begin()));
        }
    }

}
