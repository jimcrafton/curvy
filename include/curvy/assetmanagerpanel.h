#pragma once

#include "curvy/assetmanagermodel.h"
#include "curvy/panel.h"
#include "curvy/panelsmenu.h"

#include <newui/controls.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace curvy {

    // The Assets panel of the right dock (docs/asset-manager-plan.md, docs/mockups/assetManager.html, Resources/assetManager.newui): a toolbar with
    // `+` and a "..." menu, a search field and category dropdown, the library as rows grouped by category, the "N instances are older" banner, the
    // selected asset's details and the action buttons. It shows and drives an AssetManagerModel (which must outlive it) and does nothing of its own
    // to the document: the owner supplies the model's hooks and the callbacks below (file dialogs, the properties dialog).
    // @reflect ignore=true
    class AssetManagerPanel : public Panel {
    public:
        explicit AssetManagerPanel(std::shared_ptr<AssetManagerModel> model);
        ~AssetManagerPanel() override;

        // Reads the model again and shows it.
        void refresh();

        // `+`: make a new asset (the owner opens the properties dialog on a blank definition or on the selected subnet).
        std::function<void()> onNewRequested;
        // Edit...: open the properties dialog on the selected asset.
        std::function<void()> onEditRequested;
        // Save to library, and the two lines of the "..." menu: the owner asks for the file or folder and calls the model.
        std::function<void()> onSaveToLibraryRequested;
        std::function<void()> onLoadFileRequested;
        std::function<void()> onLoadFolderRequested;
        // A command failed or finished: the text for the status line of the window.
        std::function<void(const std::string& message)> onMessage;
        // Shows a menu at a screen point (the "..." button); without it the menu is not shown.
        std::function<void(const std::vector<PanelMenuEntry>&, const newui::Point&)> presentMenu;
        std::function<void()> onDestroyed;

        // What is on show, for tests. Rows are the assets in list order, titles the group headings.
        std::vector<std::string> shownNames() const;
        std::vector<std::string> shownTitles() const;
        std::vector<std::string> badgesOf(const std::string& name) const;
        std::string detail(const std::string& key) const;   // name label inputs params instances file
        bool bannerShown() const;
        std::string bannerText() const;
        std::string statusText() const;
        bool buttonEnabled(const char* name) const;
        // The panel's own clicks, as the mouse handlers run them.
        void clickRow(const std::string& name);
        void clickButton(const char* name);
        void typeSearch(const std::string& text);
        void chooseCategory(const std::string& category);

    private:
        struct Row {
            newui::SubView* view = nullptr;
            newui::Label* name = nullptr;
            newui::Label* version = nullptr;
            newui::Label* badges[2] = {nullptr, nullptr};
            std::string asset;   // empty for a group heading
            std::vector<std::string> badgeTexts;
        };

        template <typename T>
        T* find(const char* name) const {
            return dynamic_cast<T*>(root_->findView(name));
        }

        Row& rowAt(std::size_t index);
        void wire();
        void say(const std::string& message);
        void refreshCategories();
        void refreshRows();
        void refreshDetails();
        void layoutAll();
        void layoutRows(float width);
        void report(const std::string& error, const std::string& done = {});

        std::shared_ptr<AssetManagerModel> modelHolder_;   // the model may be gone before the window's views are
        AssetManagerModel& model_;
        newui::SubView* root_ = nullptr;
        newui::SubView* header_ = nullptr;
        newui::SubView* filter_ = nullptr;
        newui::SubView* list_ = nullptr;
        newui::ScrollView* listScroll_ = nullptr;
        newui::SubView* rowsRoot_ = nullptr;
        newui::SubView* banner_ = nullptr;
        newui::SubView* details_ = nullptr;
        newui::SubView* actions_ = nullptr;
        newui::Label* status_ = nullptr;
        newui::Label* bannerText_ = nullptr;
        newui::TextField* search_ = nullptr;
        newui::DropDownList* category_ = nullptr;
        std::vector<Row> rows_;
        std::size_t shownRows_ = 0;
        std::vector<std::string> categories_;
        float headerHeight_ = 0.0f;
        float filterHeight_ = 0.0f;
        float bannerHeight_ = 0.0f;
        float detailsHeight_ = 0.0f;
        float statusHeight_ = 0.0f;
        bool updating_ = false;
    };

}
