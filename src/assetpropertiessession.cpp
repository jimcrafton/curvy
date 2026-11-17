#include "curvy/assetpropertiessession.h"

#include <pyAssets.h>

#include <jitnodes/graph/assetLibrary.h>
#include <jitnodes/graph/serialize.h>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace curvy {

    namespace {

        constexpr const char* kEmptyGraph = "{ \"nodes\": [] }";

    }

    AssetPropertiesSession::AssetPropertiesSession(jitnodes::AssetDefinition definition, Kind kind, NetworkProvider network)
        : working_(definition), original_(std::move(definition)), kind_(kind), network_(std::move(network)) {}

    AssetPropertiesSession AssetPropertiesSession::forAsset(const std::string& name, NetworkProvider network) {
        return AssetPropertiesSession(jitnodes::AssetLibrary::instance().definition(name), Kind::asset, std::move(network));
    }

    AssetPropertiesSession AssetPropertiesSession::blank(NetworkProvider network) {
        jitnodes::AssetDefinition definition;
        definition.inputs = 0;
        definition.graphText = kEmptyGraph;
        return AssetPropertiesSession(std::move(definition), Kind::blank, std::move(network));
    }

    AssetPropertiesSession AssetPropertiesSession::fromSubnet(const jitnodes::Network& net, jitnodes::NodeId subnet, const std::string& typeName,
                                                              NetworkProvider network) {
        return AssetPropertiesSession(jitnodes::AssetDefinition::fromNode(net, subnet, typeName, 1), Kind::subnet, std::move(network));
    }

    void AssetPropertiesSession::setName(const std::string& name) { working_.name = name; }
    void AssetPropertiesSession::setLabel(const std::string& label) { working_.label = label; }
    void AssetPropertiesSession::setCategory(const std::string& category) { working_.category = category; }
    void AssetPropertiesSession::setAuthor(const std::string& author) { working_.author = author; }
    void AssetPropertiesSession::setDescription(const std::string& description) { working_.description = description; }
    void AssetPropertiesSession::setHelp(const std::string& help) { working_.help = help; }

    void AssetPropertiesSession::setInputs(int inputs) {
        working_.inputs = inputs;
        if (!working_.inputLabels.empty() && inputs >= 0) {
            working_.inputLabels.resize(std::size_t(inputs));
        }
    }

    std::vector<std::string> AssetPropertiesSession::knownCategories() {
        std::set<std::string> names;
        for (const jitnodes::AssetEntry& entry : jitnodes::AssetLibrary::instance().list()) {
            if (!entry.category.empty()) {
                names.insert(entry.category);
            }
        }
        return std::vector<std::string>(names.begin(), names.end());
    }

    std::size_t AssetPropertiesSession::addParam() {
        for (int number = 1;; ++number) {
            const std::string name = "param" + std::to_string(number);
            const bool taken = std::any_of(working_.params.begin(), working_.params.end(), [&](const jitnodes::AssetParam& p) { return p.name == name; });
            if (!taken) {
                jitnodes::AssetParam added;
                added.name = name;
                working_.params.push_back(std::move(added));
                return working_.params.size() - 1;
            }
        }
    }

    void AssetPropertiesSession::removeParam(std::size_t index) {
        if (index < working_.params.size()) {
            working_.params.erase(working_.params.begin() + std::ptrdiff_t(index));
        }
    }

    void AssetPropertiesSession::moveParam(std::size_t from, std::size_t to) {
        if (from >= working_.params.size() || to >= working_.params.size() || from == to) {
            return;
        }
        jitnodes::AssetParam moved = std::move(working_.params[from]);
        working_.params.erase(working_.params.begin() + std::ptrdiff_t(from));
        working_.params.insert(working_.params.begin() + std::ptrdiff_t(to), std::move(moved));
    }

    std::string AssetPropertiesSession::setParam(std::size_t index, const jitnodes::AssetParam& param) {
        if (index >= working_.params.size()) {
            return "No such parameter";
        }
        const std::string old = working_.params[index].name;
        if (param.name != old) {
            // Only a registered version can have instances that need the rename; a definition still being made has none.
            const bool known = kind_ == Kind::asset &&
                               std::any_of(original_.params.begin(), original_.params.end(), [&](const jitnodes::AssetParam& p) { return p.name == old; });
            try {
                working_.renameParam(old, param.name, known);
            } catch (const std::exception& e) {
                return e.what();
            }
        }
        working_.params[index] = param;
        return {};
    }

    bool AssetPropertiesSession::isBrush() const {
        const auto has = [this](const char* name) {
            return std::any_of(working_.params.begin(), working_.params.end(), [name](const jitnodes::AssetParam& p) { return p.name == name; });
        };
        return has("referenceWidth") && has("guideLength");
    }

    bool AssetPropertiesSession::touchesBrushNames(const std::string& name) const {
        return isBrush() && (name == "referenceWidth" || name == "guideLength");
    }

    const std::vector<std::string>& AssetPropertiesSession::scriptNames() {
        return jitnodes::assetScriptNames();
    }

    bool AssetPropertiesSession::hasScript(const std::string& name) const {
        return working_.scripts.count(name) != 0;
    }

    std::string AssetPropertiesSession::script(const std::string& name) const {
        const auto found = working_.scripts.find(name);
        return found == working_.scripts.end() ? std::string() : found->second;
    }

    void AssetPropertiesSession::setScript(const std::string& name, const std::string& text) {
        if (text.empty()) {
            working_.scripts.erase(name);
        } else {
            working_.scripts[name] = text;
        }
    }

    std::vector<ScriptIssue> AssetPropertiesSession::scriptProblems() const {
        std::vector<ScriptIssue> issues;
        if (working_.scripts.empty() || !Py_IsInitialized()) {
            return issues;
        }
        try {
            for (const jitnodes::python::ScriptProblem& problem : jitnodes::python::checkAssetScripts(working_.scripts)) {
                issues.push_back({problem.script, problem.line, problem.column, problem.message});
            }
        } catch (const std::exception&) {
            issues.clear();
        }
        return issues;
    }

    std::string AssetPropertiesSession::scriptProblem(const std::string& name) const {
        for (const ScriptIssue& problem : scriptProblems()) {
            if (problem.script == name) {
                return std::to_string(problem.line) + ":" + std::to_string(problem.column) + " " + problem.message;
            }
        }
        return {};
    }

    void AssetPropertiesSession::setFile(const std::string& name, const std::string& text) {
        working_.files[name] = text;
    }

    void AssetPropertiesSession::removeFile(const std::string& name) {
        working_.files.erase(name);
    }

    std::size_t AssetPropertiesSession::nodeCount() const {
        try {
            jitnodes::Network scratch(1);
            jitnodes::loadGraphText(scratch, working_.graphText);
            return scratch.nodeIds().size();
        } catch (const std::exception&) {
            return 0;
        }
    }

    std::string AssetPropertiesSession::matchSubnet(const jitnodes::Network& net, jitnodes::NodeId subnet) {
        try {
            const jitnodes::AssetDefinition fromNode =
                jitnodes::AssetDefinition::fromNode(net, subnet, working_.name.empty() ? std::string("scratch") : working_.name, working_.version);
            std::vector<jitnodes::AssetParam> merged;
            for (const jitnodes::AssetParam& incoming : fromNode.params) {
                const auto kept = std::find_if(working_.params.begin(), working_.params.end(),
                                               [&](const jitnodes::AssetParam& p) { return p.name == incoming.name; });
                merged.push_back(kept != working_.params.end() ? *kept : incoming);
            }
            working_.params = std::move(merged);
            working_.graphText = fromNode.graphText;
            setInputs(fromNode.inputs);
            return {};
        } catch (const std::exception& e) {
            return e.what();
        }
    }

    bool AssetPropertiesSession::dirty() const {
        if (kind_ == Kind::subnet) {
            return true;
        }
        return working_.toText() != original_.toText();
    }

    std::string AssetPropertiesSession::problem() const {
        return working_.validate();
    }

    std::size_t AssetPropertiesSession::instanceCount() const {
        jitnodes::Network* network = network_ ? network_() : nullptr;
        if (network == nullptr || original_.name.empty()) {
            return 0;
        }
        for (const jitnodes::AssetEntry& entry : jitnodes::AssetLibrary::instance().list(network)) {
            if (entry.name == original_.name) {
                return entry.instances;
            }
        }
        return 0;
    }

    std::string AssetPropertiesSession::footerNote() const {
        const std::string found = problem();
        if (!found.empty()) {
            return found;
        }
        if (!dirty()) {
            return "No changes";
        }
        std::string note = "Unsaved changes";
        const std::size_t instances = kind_ == Kind::asset ? instanceCount() : 0;
        if (instances > 0) {
            note += ": " + std::to_string(instances) + (instances == 1 ? " instance" : " instances") + " will be outdated";
        }
        return note;
    }

    std::string AssetPropertiesSession::versionText() const {
        const int registered = jitnodes::NodeRegistry::instance().assetVersion(working_.name);
        if (registered > 0) {
            return std::to_string(registered) + " (Save writes " + std::to_string(registered + 1) + ")";
        }
        return "1 (new)";
    }

    std::string AssetPropertiesSession::suggestedFileName() const {
        return (working_.name.empty() ? std::string("asset") : working_.name) + ".asset.json";
    }

    void AssetPropertiesSession::adopt() {
        original_ = working_;
        kind_ = Kind::asset;
    }

    std::string AssetPropertiesSession::apply() {
        const std::string found = problem();
        if (!found.empty()) {
            return found;
        }
        try {
            jitnodes::AssetLibrary::instance().apply(working_);
        } catch (const std::exception& e) {
            return e.what();
        }
        adopt();
        return {};
    }

    std::string AssetPropertiesSession::save(const std::string& path) {
        const std::string target = path.empty() ? working_.source : path;
        if (target.empty()) {
            return "Choose a file to save to";
        }
        jitnodes::AssetDefinition next = working_;
        try {
            jitnodes::AssetLibrary::instance().save(next, target);
        } catch (const std::exception& e) {
            return e.what();
        }
        working_ = std::move(next);
        adopt();
        return {};
    }

    void AssetPropertiesSession::revert() {
        working_ = original_;
    }

}
