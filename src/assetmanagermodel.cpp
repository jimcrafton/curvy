#include "curvy/assetmanagermodel.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

namespace curvy {

    namespace {

        std::string lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }

        const jitnodes::AssetEntry* findEntry(const std::vector<jitnodes::AssetEntry>& entries, const std::string& name) {
            for (const jitnodes::AssetEntry& entry : entries) {
                if (entry.name == name) {
                    return &entry;
                }
            }
            return nullptr;
        }

    }

    AssetManagerModel::AssetManagerModel(AssetManagerHooks hooks) : hooks_(std::move(hooks)) {
        listener_ = jitnodes::AssetLibrary::instance().addListener([this]() {
            if (!rebuilding_) {
                rebuild();
                changed();
            }
        });
        rebuild();
    }

    AssetManagerModel::~AssetManagerModel() {
        jitnodes::AssetLibrary::instance().removeListener(listener_);
    }

    bool AssetManagerModel::isBrush(const jitnodes::AssetEntry& entry) {
        const auto has = [&entry](const char* name) { return std::find(entry.params.begin(), entry.params.end(), name) != entry.params.end(); };
        return has("referenceWidth") && has("guideLength");
    }

    void AssetManagerModel::rebuild() {
        rebuilding_ = true;
        jitnodes::Network* network = hooks_.network ? hooks_.network() : nullptr;
        entries_ = jitnodes::AssetLibrary::instance().list(network);
        if (!selected_.empty() && findEntry(entries_, selected_) == nullptr) {
            selected_.clear();
        }
        rebuilding_ = false;
    }

    void AssetManagerModel::changed() {
        if (onChanged) {
            onChanged();
        }
    }

    void AssetManagerModel::refresh() {
        rebuild();
        changed();
    }

    std::string AssetManagerModel::groupTitle(const jitnodes::AssetEntry& entry) const {
        if (entry.embedded) {
            return kEmbeddedTitle;
        }
        if (!entry.category.empty()) {
            return entry.category;
        }
        return isBrush(entry) ? "Brushes" : "Other";
    }

    bool AssetManagerModel::matches(const jitnodes::AssetEntry& entry) const {
        if (category_ == kEmbeddedCategory) {
            if (!entry.embedded) {
                return false;
            }
        } else if (category_ != kAllCategories && (entry.embedded || groupTitle(entry) != category_)) {
            return false;
        }
        if (filterText_.empty()) {
            return true;
        }
        const std::string needle = lower(filterText_);
        return lower(entry.name).find(needle) != std::string::npos || lower(entry.label).find(needle) != std::string::npos;
    }

    std::vector<AssetManagerModel::Group> AssetManagerModel::groups() const {
        std::map<std::string, std::vector<jitnodes::AssetEntry>> byTitle;
        for (const jitnodes::AssetEntry& entry : entries_) {
            if (matches(entry)) {
                byTitle[groupTitle(entry)].push_back(entry);
            }
        }
        std::vector<Group> result;
        for (auto& [title, list] : byTitle) {
            if (title != kEmbeddedTitle) {
                result.push_back({title, std::move(list)});
            }
        }
        const auto embedded = byTitle.find(kEmbeddedTitle);
        if (embedded != byTitle.end()) {
            result.push_back({kEmbeddedTitle, std::move(embedded->second)});
        }
        return result;
    }

    std::vector<std::string> AssetManagerModel::categories() const {
        std::vector<std::string> titles;
        bool embedded = false;
        for (const jitnodes::AssetEntry& entry : entries_) {
            if (entry.embedded) {
                embedded = true;
            } else {
                titles.push_back(groupTitle(entry));
            }
        }
        std::sort(titles.begin(), titles.end());
        titles.erase(std::unique(titles.begin(), titles.end()), titles.end());
        titles.insert(titles.begin(), kAllCategories);
        if (embedded) {
            titles.push_back(kEmbeddedCategory);
        }
        return titles;
    }

    void AssetManagerModel::setFilterText(const std::string& text) {
        if (text != filterText_) {
            filterText_ = text;
            changed();
        }
    }

    void AssetManagerModel::setCategory(const std::string& category) {
        if (category != category_) {
            category_ = category;
            changed();
        }
    }

    void AssetManagerModel::select(const std::string& name) {
        const std::string next = findEntry(entries_, name) != nullptr ? name : std::string();
        if (next != selected_) {
            selected_ = next;
            changed();
        }
    }

    std::optional<AssetManagerModel::Details> AssetManagerModel::details() const {
        const jitnodes::AssetEntry* entry = findEntry(entries_, selected_);
        if (entry == nullptr) {
            return std::nullopt;
        }
        Details d;
        d.name = entry->name;
        d.label = entry->label;
        d.version = entry->version;
        d.inputs = entry->inputs;
        d.params = entry->params;
        d.instances = entry->instances;
        d.outdated = entry->outdated;
        d.source = entry->source;
        d.embedded = entry->embedded;
        d.brush = isBrush(*entry);
        return d;
    }

    std::size_t AssetManagerModel::outdatedCount() const {
        std::size_t total = 0;
        for (const jitnodes::AssetEntry& entry : entries_) {
            total += entry.outdated;
        }
        return total;
    }

    std::string AssetManagerModel::bannerText() const {
        const jitnodes::AssetEntry* entry = findEntry(entries_, selected_);
        if (entry == nullptr || entry->outdated == 0) {
            return {};
        }
        const std::string version = "v" + std::to_string(entry->version);
        if (entry->outdated == 1) {
            return "1 instance is older than " + version;
        }
        return std::to_string(entry->outdated) + " instances are older than " + version;
    }

    std::string AssetManagerModel::statusText() const {
        if (entries_.empty()) {
            return "No assets";
        }
        std::string text = std::to_string(entries_.size()) + (entries_.size() == 1 ? " asset" : " assets");
        const std::size_t outdated = outdatedCount();
        if (outdated > 0) {
            text += ", " + std::to_string(outdated) + " outdated";
        }
        return text;
    }

    std::string AssetManagerModel::addInstance() {
        if (selected_.empty()) {
            return "Select an asset first";
        }
        if (!hooks_.addInstance) {
            return "No document is open";
        }
        const std::string error = hooks_.addInstance(selected_);
        refresh();
        return error;
    }

    std::string AssetManagerModel::reload() {
        if (selected_.empty()) {
            return "Select an asset first";
        }
        try {
            jitnodes::AssetLibrary::instance().reload(selected_);
        } catch (const std::exception& e) {
            return e.what();
        }
        return {};
    }

    std::string AssetManagerModel::updateInstances() {
        if (!hooks_.updateInstances) {
            return "No document is open";
        }
        std::size_t count = 0;
        const std::string error = hooks_.updateInstances(selected_, count);
        refresh();
        return error;
    }

    std::string AssetManagerModel::unlock() {
        if (!hooks_.unlockSelected) {
            return "No document is open";
        }
        const std::string error = hooks_.unlockSelected();
        refresh();
        return error;
    }

    std::string AssetManagerModel::uninstall() {
        if (selected_.empty()) {
            return "Select an asset first";
        }
        try {
            jitnodes::Network* network = hooks_.network ? hooks_.network() : nullptr;
            jitnodes::AssetLibrary::instance().uninstall(selected_, network);
        } catch (const std::exception& e) {
            return e.what();
        }
        return {};
    }

    std::string AssetManagerModel::saveToLibrary(const std::string& path) {
        if (selected_.empty()) {
            return "Select an asset first";
        }
        try {
            jitnodes::AssetDefinition definition = jitnodes::AssetLibrary::instance().definition(selected_);
            jitnodes::AssetLibrary::instance().save(definition, path);
        } catch (const std::exception& e) {
            return e.what();
        }
        return {};
    }

    std::string AssetManagerModel::installFile(const std::string& path) {
        try {
            jitnodes::AssetLibrary::instance().installFile(path);
        } catch (const std::exception& e) {
            return e.what();
        }
        for (const jitnodes::AssetEntry& entry : jitnodes::AssetLibrary::instance().list()) {
            if (entry.source == path) {
                select(entry.name);
                break;
            }
        }
        return {};
    }

    std::string AssetManagerModel::installFolder(const std::string& folder, std::size_t* loaded) {
        return jitnodes::AssetLibrary::instance().installDirectory(folder, loaded);
    }

}
