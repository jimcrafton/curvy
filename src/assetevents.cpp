#include "curvy/assetevents.h"

#include "curvy/scriptconsole.h"
#include "curvy/sketchdocument.h"

#include <jitnodes/graph/assetLibrary.h>

#include <cstdint>
#include <fstream>

namespace curvy {

    namespace {

        bool isInside(const std::filesystem::path& file, const std::filesystem::path& folder) {
            std::error_code ignored;
            const std::filesystem::path canonicalFile = std::filesystem::weakly_canonical(file, ignored);
            const std::filesystem::path canonicalFolder = std::filesystem::weakly_canonical(folder, ignored);
            const std::filesystem::path relative = canonicalFile.lexically_relative(canonicalFolder);
            return !relative.empty() && *relative.begin() != "..";
        }

    }

    void AssetEvents::addTrustedFolder(const std::filesystem::path& folder) {
        folders_.push_back(folder);
    }

    void AssetEvents::setTrustFile(const std::filesystem::path& file) {
        trustFile_ = file;
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) {
                trusted_.insert(line);
            }
        }
    }

    std::string AssetEvents::scriptsHash(const jitnodes::AssetDefinition& definition) {
        if (definition.scripts.empty()) {
            return {};
        }
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](const std::string& text) {
            for (const char c : text) {
                hash ^= static_cast<unsigned char>(c);
                hash *= 1099511628211ull;
            }
            hash ^= 0xff;
            hash *= 1099511628211ull;
        };
        for (const auto& [name, source] : definition.scripts) {
            mix(name);
            mix(source);
        }
        static const char* digits = "0123456789abcdef";
        std::string text(16, '0');
        for (int i = 15; i >= 0; --i) {
            text[static_cast<std::size_t>(i)] = digits[hash & 0xf];
            hash >>= 4;
        }
        return text;
    }

    bool AssetEvents::isTrusted(const std::string& asset) const {
        const jitnodes::AssetLibrary& library = jitnodes::AssetLibrary::instance();
        if (!library.has(asset)) {
            return false;
        }
        const jitnodes::AssetDefinition definition = library.definition(asset);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (trusted_.count(scriptsHash(definition)) != 0) {
                return true;
            }
        }
        const std::string source = library.source(asset);
        if (source.empty()) {
            return false;
        }
        for (const std::filesystem::path& folder : folders_) {
            if (isInside(std::filesystem::u8path(source), folder)) {
                return true;
            }
        }
        return false;
    }

    bool AssetEvents::allowed(const std::string& asset, const jitnodes::AssetDefinition& definition) {
        if (isTrusted(asset)) {
            return true;
        }
        const std::string hash = scriptsHash(definition);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (declined_.count(hash) != 0 || !prompt_) {
                return false;
            }
        }
        std::vector<std::string> names;
        for (const auto& [name, source] : definition.scripts) {
            if (!source.empty()) {
                names.push_back(name);
            }
        }
        if (!prompt_(asset, names)) {
            std::lock_guard<std::mutex> lock(mutex_);
            declined_.insert(hash);
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        trusted_.insert(hash);
        if (!trustFile_.empty()) {
            std::error_code ignored;
            std::filesystem::create_directories(trustFile_.parent_path(), ignored);
            std::ofstream out(trustFile_, std::ios::app);
            out << hash << "\n";
        }
        return true;
    }

    std::string AssetEvents::fire(SketchDocument& document, SelectionModel* selection, const NodeRef& node, const std::string& event,
                                  const std::map<std::string, std::string>& values) {
        if (console_ == nullptr || !document.hasNode(node)) {
            return {};
        }
        const jitnodes::Node& instance = document.node(node);
        if (!instance.isAssetInstance()) {
            return {};
        }
        return fireAsset(document, selection, instance.typeName(), node, event, values);
    }

    std::string AssetEvents::fireAsset(SketchDocument& document, SelectionModel* selection, const std::string& asset, const NodeRef& node,
                                       const std::string& event, const std::map<std::string, std::string>& values) {
        const jitnodes::AssetLibrary& library = jitnodes::AssetLibrary::instance();
        if (!library.has(asset)) {
            return {};
        }
        const jitnodes::AssetDefinition definition = library.definition(asset);
        const auto script = definition.scripts.find(event);
        if (script == definition.scripts.end() || script->second.empty() || !allowed(asset, definition)) {
            return {};
        }
        const ScriptResult result = console_->runAssetEvent(document, selection, node.scope, node.id, asset, event, values);
        // node.id is invalidNodeId for an event that belongs to the asset and not to an instance (onInstalled)
        if (result.ok) {
            return {};
        }
        return asset + "." + event + ": " + result.error;
    }

    namespace {

        void collectInstances(SketchDocument& document, std::uint32_t scope, const std::vector<jitnodes::NodeId>& ids, std::vector<NodeRef>& out) {
            const jitnodes::Network* network = document.findNetwork(scope);
            if (network == nullptr) {
                return;
            }
            for (const jitnodes::NodeId id : ids) {
                if (!network->hasNode(id)) {
                    continue;
                }
                const jitnodes::Node& node = network->node(id);
                if (!node.isAssetInstance() && node.childNetwork() != nullptr) {
                    const std::uint32_t inner = document.scopeOfSubnet(NodeRef(scope, id));
                    if (const jitnodes::Network* contents = document.findNetwork(inner)) {
                        collectInstances(document, inner, contents->nodeIds(), out);
                    }
                }
                if (node.isAssetInstance()) {
                    out.push_back(NodeRef(scope, id));
                }
            }
        }

    }

    std::string AssetEvents::fireForInstances(SketchDocument& document, SelectionModel* selection, std::uint32_t scope,
                                              const std::vector<jitnodes::NodeId>& ids, const std::string& event,
                                              const std::map<std::string, std::string>& values) {
        std::vector<NodeRef> instances;
        collectInstances(document, scope, ids, instances);
        std::string errors;
        for (const NodeRef& instance : instances) {
            const std::string error = fire(document, selection, instance, event, values);
            if (!error.empty()) {
                errors += (errors.empty() ? "" : "\n") + error;
            }
        }
        return errors;
    }

    std::string AssetEvents::fireCreatedFor(SketchDocument& document, SelectionModel* selection, std::uint32_t scope,
                                            const std::vector<jitnodes::NodeId>& ids, const std::string& reason) {
        return fireForInstances(document, selection, scope, ids, "onCreated", {{"reason", reason}});
    }

    std::string AssetEvents::fireDeletedFor(SketchDocument& document, SelectionModel* selection, const std::vector<NodeRef>& nodes) {
        std::string errors;
        for (const NodeRef& node : nodes) {
            if (!document.hasNode(node)) {
                continue;
            }
            const std::string error = fireForInstances(document, selection, node.scope, {node.id}, "onDeleted", {});
            if (!error.empty()) {
                errors += (errors.empty() ? "" : "\n") + error;
            }
        }
        return errors;
    }

    std::string AssetEvents::fireFrameChanged(SketchDocument& document, SelectionModel* selection, int frame, bool playing) {
        return fireForInstances(document, selection, 0, document.network().nodeIds(), "onFrameChanged",
                                {{"frame", std::to_string(frame)}, {"playing", playing ? "1" : "0"}});
    }

    std::string AssetEvents::fireInstalled(SketchDocument& document, SelectionModel* selection, const std::string& asset) {
        if (console_ == nullptr || !jitnodes::AssetLibrary::instance().has(asset)) {
            return {};
        }
        const int version = jitnodes::AssetLibrary::instance().definition(asset).version;
        return fireAsset(document, selection, asset, NodeRef(), "onInstalled", {{"name", asset}, {"version", std::to_string(version)}});
    }

    namespace {

        struct InstanceWatch {
            std::vector<jitnodes::NodeId> inputs;
            std::map<std::string, float> params;
        };

        bool hasScript(const jitnodes::AssetDefinition& definition, const char* event) {
            const auto found = definition.scripts.find(event);
            return found != definition.scripts.end() && !found->second.empty();
        }

        // Only instances of assets that have an input or parameter script are watched.
        void recordWatch(SketchDocument& document, const NodeRef& ref, std::map<NodeRef, InstanceWatch>& into) {
            const jitnodes::AssetLibrary& library = jitnodes::AssetLibrary::instance();
            const jitnodes::Node& node = document.node(ref);
            if (!library.has(node.typeName())) {
                return;
            }
            const jitnodes::AssetDefinition definition = library.definition(node.typeName());
            if (!hasScript(definition, "onInputChanged") && !hasScript(definition, "onParamChanged")) {
                return;
            }
            InstanceWatch watch;
            const jitnodes::Network& network = *document.findNetwork(ref.scope);
            for (int i = 0; i < node.inputCount(); ++i) {
                watch.inputs.push_back(network.inputOf(ref.id, i));
            }
            for (const jitnodes::AssetParam& param : definition.params) {
                if (node.hasFloat(param.name)) {
                    watch.params[param.name] = node.baseFloat(param.name);
                }
            }
            into[ref] = std::move(watch);
        }

    }

    struct AssetEvents::Watched {
        std::map<NodeRef, InstanceWatch> instances;
    };

    AssetEvents::AssetEvents(ScriptConsole* console) : console_(console) {}

    namespace {
        AssetEvents* migrationOwner = nullptr;
    }

    AssetEvents::~AssetEvents() {
        if (migrationOwner == this) {
            jitnodes::NodeRegistry::instance().setAssetMigrationFallback(nullptr);
            migrationOwner = nullptr;
        }
    }

    void AssetEvents::installMigrationHook() {
        migrationOwner = this;
        jitnodes::NodeRegistry::instance().setAssetMigrationFallback([this](jitnodes::AssetUpgrade& upgrade) { migrate(upgrade); });
    }

    // Called by the core while an instance is rebuilt from a newer version of its asset: on the main thread for Update, on the loading thread for
    // Open. A problem never stops the rebuild (the core's own result stays); it is remembered for the user.
    void AssetEvents::migrate(jitnodes::AssetUpgrade& upgrade) {
        const std::string asset = upgrade.fresh.typeName();
        const jitnodes::AssetLibrary& library = jitnodes::AssetLibrary::instance();
        if (console_ == nullptr || !library.has(asset) || !hasScript(library.definition(asset), "onVersionUpdated")) {
            return;
        }
        std::string problem;
        if (!isTrusted(asset)) {
            std::lock_guard<std::mutex> lock(mutex_);
            untrusted_.insert(asset);
            problem = asset + ".onVersionUpdated did not run: the asset's scripts are not allowed yet";
        } else {
            const std::string error = console_->runMigration(asset, upgrade);
            if (!error.empty()) {
                problem = asset + ".onVersionUpdated: " + error;
            }
        }
        if (!problem.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            migrationProblems_.push_back(problem);
        }
    }

    std::string AssetEvents::takeMigrationProblems() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string text;
        for (const std::string& problem : migrationProblems_) {
            text += (text.empty() ? "" : "\n") + problem;
        }
        migrationProblems_.clear();
        return text;
    }

    std::vector<std::string> AssetEvents::takeUntrustedAssets() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> assets(untrusted_.begin(), untrusted_.end());
        untrusted_.clear();
        return assets;
    }

    bool AssetEvents::askTrust(const std::string& asset) {
        const jitnodes::AssetLibrary& library = jitnodes::AssetLibrary::instance();
        return library.has(asset) && allowed(asset, library.definition(asset));
    }

    void AssetEvents::remember(SketchDocument& document) {
        watched_ = std::make_unique<Watched>();
        for (const NodeRef& instance : instancesOf(document)) {
            recordWatch(document, instance, watched_->instances);
        }
    }

    std::string AssetEvents::fireChanges(SketchDocument& document, SelectionModel* selection) {
        if (!watched_ || console_ == nullptr || console_->running()) {
            return {};
        }
        Watched now;
        for (const NodeRef& instance : instancesOf(document)) {
            recordWatch(document, instance, now.instances);
        }
        struct Change {
            NodeRef node;
            std::string event;
            std::map<std::string, std::string> values;
        };
        std::vector<Change> changes;
        for (const auto& [ref, current] : now.instances) {
            const auto before = watched_->instances.find(ref);
            if (before == watched_->instances.end()) {
                continue;   // a new instance: onCreated is its event
            }
            for (std::size_t i = 0; i < current.inputs.size(); ++i) {
                if (i >= before->second.inputs.size() || before->second.inputs[i] != current.inputs[i]) {
                    changes.push_back({ref, "onInputChanged", {{"index", std::to_string(i)}}});
                }
            }
            for (const auto& [name, value] : current.params) {
                const auto old = before->second.params.find(name);
                if (old != before->second.params.end() && old->second != value) {
                    changes.push_back({ref, "onParamChanged", {{"name", name}, {"value", std::to_string(value)}}});
                }
            }
        }
        watched_->instances = std::move(now.instances);
        std::string errors;
        for (const Change& change : changes) {
            const std::string error = fire(document, selection, change.node, change.event, change.values);
            if (!error.empty()) {
                errors += (errors.empty() ? "" : "\n") + error;
            }
        }
        // What the scripts changed is not reported back to them: an event cannot start itself.
        remember(document);
        return errors;
    }

    std::set<NodeRef> AssetEvents::instancesOf(SketchDocument& document) {
        std::vector<NodeRef> found;
        collectInstances(document, 0, document.network().nodeIds(), found);
        return std::set<NodeRef>(found.begin(), found.end());
    }

    std::string AssetEvents::fireCreatedForNew(SketchDocument& document, SelectionModel* selection, const std::set<NodeRef>& before,
                                               const std::string& reason) {
        std::vector<NodeRef> found;
        collectInstances(document, 0, document.network().nodeIds(), found);
        std::string errors;
        for (const NodeRef& instance : found) {
            if (before.count(instance) != 0) {
                continue;
            }
            const std::string error = fireCreated(document, selection, instance, reason);
            if (!error.empty()) {
                errors += (errors.empty() ? "" : "\n") + error;
            }
        }
        return errors;
    }

    std::string AssetEvents::fireCreatedInDocument(SketchDocument& document, SelectionModel* selection, const std::string& reason) {
        return fireCreatedFor(document, selection, 0, document.network().nodeIds(), reason);
    }

    std::string AssetEvents::fireCreated(SketchDocument& document, SelectionModel* selection, const NodeRef& node, const std::string& reason) {
        return fire(document, selection, node, "onCreated", {{"reason", reason}});
    }

}
