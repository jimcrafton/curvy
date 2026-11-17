#pragma once

#include <jitnodes/graph/assetDefinition.h>
#include <jitnodes/graph/assetLibrary.h>
#include <jitnodes/graph/network.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // What the Asset Manager needs from the document and the editor. Each hook may be empty (then the command that needs it reports that it is not
    // available), so the model can be driven without a window.
    // @reflect ignore=true
    struct AssetManagerHooks {
        // The graph the instance counts are taken from; null while there is no document.
        std::function<jitnodes::Network*()> network;
        // Adds an instance of the asset type to the graph being edited, wired from the selected node; returns an error message or "".
        std::function<std::string(const std::string& type)> addInstance;
        // Rebuilds the outdated instances of one asset (all when empty) as one undo step; sets `count`, returns an error message or "".
        std::function<std::string(const std::string& type, std::size_t& count)> updateInstances;
        // Turns the selected asset instance in the graph back into a subnet; returns an error message or "".
        std::function<std::string()> unlockSelected;
    };

    // The state and commands behind the Asset Manager panel (docs/asset-manager-plan.md, docs/mockups/assetManager.html): the library grouped by
    // category with a search and a category filter, the selected asset's details, the "outdated instances" banner, and one function per button. A command
    // returns an error message, or an empty string when it worked; none of them throws. The model listens to the asset library and calls onChanged
    // after anything changed, so a panel only has to redraw.
    // @reflect ignore=true
    class AssetManagerModel {
    public:
        static constexpr const char* kAllCategories = "All";
        static constexpr const char* kEmbeddedCategory = "Embedded";
        static constexpr const char* kEmbeddedTitle = "Embedded in this document";

        explicit AssetManagerModel(AssetManagerHooks hooks);
        ~AssetManagerModel();
        AssetManagerModel(const AssetManagerModel&) = delete;
        AssetManagerModel& operator=(const AssetManagerModel&) = delete;

        // @reflect ignore=true
        struct Group {
            std::string title;
            std::vector<jitnodes::AssetEntry> entries;
        };

        // @reflect ignore=true
        struct Details {
            std::string name;
            std::string label;
            int version = 0;
            int inputs = 0;
            std::vector<std::string> params;
            std::size_t instances = 0;
            std::size_t outdated = 0;
            std::string source;
            bool embedded = false;
            bool brush = false;
        };

        // The library as the panel lists it: filtered by the search text (name or label, case-insensitive) and the category, grouped by category
        // (an asset that promotes referenceWidth and guideLength is a brush and, without a category, is listed under Brushes; embedded assets
        // are listed last under kEmbeddedTitle).
        std::vector<Group> groups() const;
        // kAllCategories, the categories in use, and kEmbeddedCategory when there is an embedded asset.
        std::vector<std::string> categories() const;
        void setFilterText(const std::string& text);
        void setCategory(const std::string& category);
        const std::string& filterText() const { return filterText_; }
        const std::string& category() const { return category_; }

        const std::string& selected() const { return selected_; }
        void select(const std::string& name);
        std::optional<Details> details() const;

        // How many instances in the document are older than their asset, over all assets.
        std::size_t outdatedCount() const;
        // "N instance(s) older than vX" for the selected asset; empty when none is.
        std::string bannerText() const;
        // "12 assets, 1 outdated".
        std::string statusText() const;
        // An asset that promotes both referenceWidth and guideLength is a graph brush (docs/graph-brushes.md).
        static bool isBrush(const jitnodes::AssetEntry& entry);

        // Re-reads the library and the document's instance counts; the panel calls it when the document changed.
        void refresh();

        std::string addInstance();
        std::string reload();
        std::string updateInstances();
        std::string unlock();
        // Asks the library to forget the selected asset; refused while the document holds instances (unlock or delete them first).
        std::string uninstall();
        // Writes an embedded or in-memory asset to a file (the next version) and makes the file its source.
        std::string saveToLibrary(const std::string& path);
        std::string installFile(const std::string& path);
        // Registers every *.asset.json in the folder; `loaded` gets how many.
        std::string installFolder(const std::string& folder, std::size_t* loaded = nullptr);

        // Fired after the list, the selection, a filter or an instance count changed.
        std::function<void()> onChanged;

        const AssetManagerHooks& hooks() const { return hooks_; }

    private:
        void rebuild();
        void changed();
        bool matches(const jitnodes::AssetEntry& entry) const;
        std::string groupTitle(const jitnodes::AssetEntry& entry) const;

        AssetManagerHooks hooks_;
        std::vector<jitnodes::AssetEntry> entries_;
        std::string selected_;
        std::string filterText_;
        std::string category_ = kAllCategories;
        int listener_ = 0;
        bool rebuilding_ = false;
    };

}
