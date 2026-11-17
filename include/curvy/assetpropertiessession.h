#pragma once

#include <jitnodes/graph/assetDefinition.h>
#include <jitnodes/graph/network.h>

#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // One thing wrong in an asset's scripts (the rules of docs/assets.md); line and column are 1-based.
    // @reflect ignore=true
    struct ScriptIssue {
        std::string script;
        int line = 1;
        int column = 1;
        std::string message;
    };

    // One run of the Asset Properties dialog (docs/asset-manager-plan.md, docs/mockups/assetProperties.html), without the window: a working copy
    // of an AssetDefinition, what the dialog shows about it, and Apply / Save / Revert. The dialog binds its controls to the setters and reads the
    // state back; tests drive the same calls.
    //
    // The dialog opens in one of three ways: on a registered asset, on a blank definition (`+` with nothing selected; Save is then "Create"), or on a
    // definition made from a subnet of the graph (`+` with a subnet selected, or Match node). Nothing reaches the library before apply() or save(), and
    // both refuse (returning the message) a definition that validate() rejects, scripts included.
    // @reflect ignore=true
    class AssetPropertiesSession {
    public:
        enum class Kind { asset, blank, subnet };

        // The graph the instance count of the footer note is taken from; may be empty.
        using NetworkProvider = std::function<jitnodes::Network*()>;

        AssetPropertiesSession(jitnodes::AssetDefinition definition, Kind kind, NetworkProvider network = {});

        // Throws std::runtime_error for an unknown asset name or a node that is not a subnet.
        static AssetPropertiesSession forAsset(const std::string& name, NetworkProvider network = {});
        static AssetPropertiesSession blank(NetworkProvider network = {});
        static AssetPropertiesSession fromSubnet(const jitnodes::Network& net, jitnodes::NodeId subnet, const std::string& typeName,
                                                 NetworkProvider network = {});

        Kind kind() const { return kind_; }
        const jitnodes::AssetDefinition& working() const { return working_; }

        // Basic.
        void setName(const std::string& name);
        void setLabel(const std::string& label);
        void setCategory(const std::string& category);
        void setAuthor(const std::string& author);
        void setDescription(const std::string& description);
        void setHelp(const std::string& help);
        void setInputs(int inputs);
        // The type names of the library's categories, for the dropdown.
        static std::vector<std::string> knownCategories();

        // Parameters.
        std::size_t paramCount() const { return working_.params.size(); }
        const jitnodes::AssetParam& param(std::size_t index) const { return working_.params.at(index); }
        // Adds "param1", "param2", ... (the first free one) and returns its index.
        std::size_t addParam();
        void removeParam(std::size_t index);
        void moveParam(std::size_t from, std::size_t to);
        // Replaces a parameter. A new name renames it: the contents follow, and a parameter an earlier version had records the rename for older
        // instances. Returns an error message (changing nothing), or "".
        std::string setParam(std::size_t index, const jitnodes::AssetParam& param);
        // referenceWidth and guideLength together make an asset a graph brush.
        bool isBrush() const;
        bool touchesBrushNames(const std::string& name) const;

        // Scripts.
        static const std::vector<std::string>& scriptNames();
        bool hasScript(const std::string& name) const;
        std::string script(const std::string& name) const;
        // Empty text removes the script.
        void setScript(const std::string& name, const std::string& text);
        // The problems of every script (the rules of docs/assets.md) as the commit check finds them; empty when the Python check is not running.
        std::vector<ScriptIssue> scriptProblems() const;
        // The first problem of one script as "line:column message", or "".
        std::string scriptProblem(const std::string& name) const;

        // Extra files.
        void setFile(const std::string& name, const std::string& text);
        void removeFile(const std::string& name);

        // Contents.
        std::size_t nodeCount() const;
        // Replaces the contents and parameters with those of a subnet of the graph ("Match node").
        std::string matchSubnet(const jitnodes::Network& net, jitnodes::NodeId subnet);

        // State.
        bool dirty() const;
        // validate()'s first problem, or "".
        std::string problem() const;
        // The footer text: the problem, else "Unsaved changes" with how many instances a save will leave outdated, else "No changes".
        std::string footerNote() const;
        // "2 (Save writes 3)" for a registered asset, "1 (new)" for a new one.
        std::string versionText() const;
        const char* saveLabel() const { return kind_ == Kind::asset ? "Save" : "Create"; }
        std::string suggestedFileName() const;
        std::size_t instanceCount() const;

        // Registers the working copy in memory; the original becomes the working copy. Returns an error message or "".
        std::string apply();
        // Writes the file and registers it (one version up); `path` empty uses the current source. Returns an error message or "".
        std::string save(const std::string& path);
        // Back to what the dialog opened with (a blank definition goes back to blank).
        void revert();

    private:
        void adopt();

        jitnodes::AssetDefinition working_;
        jitnodes::AssetDefinition original_;
        Kind kind_;
        NetworkProvider network_;
    };

}
