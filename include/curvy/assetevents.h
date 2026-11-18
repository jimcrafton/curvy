#pragma once

#include "curvy/noderef.h"

#include <jitnodes/graph/assetDefinition.h>
#include <jitnodes/graph/network.h>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace curvy {

    class ScriptConsole;
    class SelectionModel;
    class SketchDocument;

    // Runs the Python events of asset instances (docs/asset-manager-plan.md, section 5a) and decides whether an asset's scripts may run at all.
    // An asset's scripts run when it was installed from one of the trusted folders (the application's own assets and the user's library), or
    // when the user allowed that exact text: the allowance is remembered by a hash of the scripts, so an edited script asks again. Anything
    // else, an asset embedded in a document included, asks once per session through the trust prompt; without a prompt it does not run.
    // @reflect ignore=true
    class AssetEvents {
    public:
        // Asks whether the scripts of `asset` may run; `scriptNames` lists the scripts that have code. Returns true to allow.
        using TrustPrompt = std::function<bool(const std::string& asset, const std::vector<std::string>& scriptNames)>;

        explicit AssetEvents(ScriptConsole* console);
        ~AssetEvents();

        void addTrustedFolder(const std::filesystem::path& folder);
        void setTrustPrompt(TrustPrompt prompt) { prompt_ = std::move(prompt); }
        // The allowed hashes live in a text file, one per line; read now and rewritten whenever the user allows one more.
        void setTrustFile(const std::filesystem::path& file);

        // A stable hash of the names and texts of the scripts (empty when the definition has none).
        static std::string scriptsHash(const jitnodes::AssetDefinition& definition);
        // True when the scripts may run without asking.
        bool isTrusted(const std::string& asset) const;

        // Runs `event` of the asset instance `node` when its asset has that script and is trusted (asking once when it is not). `values` become
        // string entries of the script's kwargs. Returns an error message to show the user, or "" (also when nothing needed to run).
        std::string fire(SketchDocument& document, SelectionModel* selection, const NodeRef& node, const std::string& event,
                         const std::map<std::string, std::string>& values);

        // onCreated with reason one of "added", "pasted", "undo", "loaded".
        std::string fireCreated(SketchDocument& document, SelectionModel* selection, const NodeRef& node, const std::string& reason);

        // onCreated for the asset instances among `ids` (nodes of the network of `scope`), and for those inside the subnets among them, inner ones
        // first so an instance's contents are done before the subnet around it. The nodes are collected before the first script runs, so a script
        // that edits the graph does not disturb the walk; one that deletes a node meant for later just skips it. Returns the errors, one per line.
        std::string fireCreatedFor(SketchDocument& document, SelectionModel* selection, std::uint32_t scope, const std::vector<jitnodes::NodeId>& ids,
                                   const std::string& reason);
        // The same for every node of the document (a document that was just opened).
        std::string fireCreatedInDocument(SketchDocument& document, SelectionModel* selection, const std::string& reason);

        // Every asset instance of the document (not those inside instances). Taken before an undo or redo and handed to fireCreatedForNew after it.
        static std::set<NodeRef> instancesOf(SketchDocument& document);
        // onCreated for the instances that exist now but were not in `before`: ones an undo (or a redo) brought back.
        std::string fireCreatedForNew(SketchDocument& document, SelectionModel* selection, const std::set<NodeRef>& before, const std::string& reason);

        // onDeleted for the asset instances among `nodes` and inside the subnets among them; call it while they are still in the document.
        std::string fireDeletedFor(SketchDocument& document, SelectionModel* selection, const std::vector<NodeRef>& nodes);
        // onFrameChanged for every instance in the document, after the frame is shown.
        std::string fireFrameChanged(SketchDocument& document, SelectionModel* selection, int frame, bool playing);
        // onInstalled of the asset (it belongs to no instance: `node` is None in the script).
        std::string fireInstalled(SketchDocument& document, SelectionModel* selection, const std::string& asset);

        // onInputChanged and onParamChanged work by comparison: remember() takes the wiring and the promoted parameter values (stored values, not
        // animated ones) of the instances whose asset has such a script; fireChanges() compares the document with that, fires the events for what
        // differs and takes the new picture. What those scripts change themselves is not reported back to them. Call remember() when a document
        // is opened or created, and fireChanges() after each change of the graph. Nothing fires while a script is running; the difference is
        // reported by the next call.
        void remember(SketchDocument& document);
        std::string fireChanges(SketchDocument& document, SelectionModel* selection);

        // onVersionUpdated, the one event that is not fired on a document but by the core while it rebuilds an instance from a newer version of its
        // asset (Update, or opening a file saved with an older one). installMigrationHook() makes this object the one the core asks. The script runs
        // only for an asset that is trusted without asking (the load may be on another thread, which cannot show a prompt); when it is not, the
        // rebuild goes on with the core's default and the asset is remembered. Problems never stop the rebuild.
        void installMigrationHook();
        // What went wrong with migrations since the last call, one per line, and the assets whose script was not run for want of trust.
        std::string takeMigrationProblems();
        std::vector<std::string> takeUntrustedAssets();
        // Asks the trust prompt about `asset` (once per session unless allowed); true when its scripts may run now.
        bool askTrust(const std::string& asset);

    private:
        struct Watched;

        void migrate(jitnodes::AssetUpgrade& upgrade);
        std::string fireAsset(SketchDocument& document, SelectionModel* selection, const std::string& asset, const NodeRef& node,
                              const std::string& event, const std::map<std::string, std::string>& values);
        std::string fireForInstances(SketchDocument& document, SelectionModel* selection, std::uint32_t scope,
                                     const std::vector<jitnodes::NodeId>& ids, const std::string& event,
                                     const std::map<std::string, std::string>& values);
        bool allowed(const std::string& asset, const jitnodes::AssetDefinition& definition);

        ScriptConsole* console_ = nullptr;
        TrustPrompt prompt_;
        std::vector<std::filesystem::path> folders_;
        std::filesystem::path trustFile_;
        std::set<std::string> trusted_;
        std::set<std::string> declined_;
        std::unique_ptr<Watched> watched_;
        mutable std::mutex mutex_;   // the trust sets and the migration notes: a load runs the hook on its own thread
        std::vector<std::string> migrationProblems_;
        std::set<std::string> untrusted_;
    };

}
