#pragma once

#include "curvy/scriptmodule.h"
#include "curvy/syntaxproblem.h"   // where Python refuses to compile a script (checkSyntax)

#include <chrono>
#include <filesystem>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    class SketchDocument;
    class SelectionModel;

    // The folder of the running executable.
    std::filesystem::path executableDirectory();

    // Where the Python to embed lives: the `python` folder next to the executable when it holds a standard library (an installed or bundled
    // build), otherwise `developmentHome` (the 3rdparty/python the build was configured with). Never looks at the PATH, the registry or
    // PYTHONHOME.
    std::filesystem::path findPythonHome(const std::filesystem::path& executableDir, const std::filesystem::path& developmentHome);

    struct ScriptResult {
        bool ok = true;
        // What the code printed, then the repr of its value when it was an expression that is not None.
        std::string output;
        // The traceback when the code raised.
        std::string error;
        // True when the run was stopped by cancel() or the Escape key (ok is false and error says so). Whatever the script changed before
        // that stays changed, as the run's single undo step.
        bool cancelled = false;
        // True when the run was stopped because it passed the time limit (cancelled is true then too).
        bool timedOut = false;
    };

    // The embedded Python interpreter and the one scope console input runs in (names persist from one run to the next). Create it on the
    // thread that runs scripts (the main thread) and destroy it there. One per process: CPython cannot be restarted.
    //
    // Scripts see `curvy.Document` as `doc` (page, undoable graph edits, undo and redo) and the document's graph as `network`. Edits
    // made on `network` directly are not undo steps; `doc` is the way to change the document.
    //
    // Scripts run either with run(), on the calling thread, which it blocks, or with runInBackground(), on a worker thread: the window
    // stays alive, and every call a script makes into `curvy` or `jitnodes` is executed on the main thread (see setMainThreadPoster), so
    // the document is never touched from two threads. cancel() and Escape stop one that runs too long.
    // @reflect ignore=true
    class ScriptConsole {
    public:
        // `pythonHome` is the bundled Python folder. Throws std::runtime_error when it does not start.
        explicit ScriptConsole(const std::filesystem::path& pythonHome);
        ~ScriptConsole();

        ScriptConsole(const ScriptConsole&) = delete;
        ScriptConsole& operator=(const ScriptConsole&) = delete;

        // Runs `code` with `doc` and `network` bound to `document` (both None when it is null), and `selection` (also `doc.selection`) to the
        // editor's selection when one is given (None otherwise). Never throws for a script error. Returns a failed result at once when a
        // background script is running.
        ScriptResult run(const std::string& code, SketchDocument* document, SelectionModel* selection = nullptr);

        // Runs the script `event` of the asset `asset` for the instance `node` (a node of the network of scope `scope`, as in NodeRef) with
        // `kwargs` = {"node": the handle, plus `values`}, `module` bound to the asset's shared module (run once per session and version),
        // and the event's runtime restriction in force (jitnodes.assets.restricted). Does nothing, ok, when the asset has no such script. Whatever
        // the script changes joins the undo step already open in `document` (the caller merges it). Main thread, never in a background run.
        ScriptResult runAssetEvent(SketchDocument& document, SelectionModel* selection, std::uint32_t scope, std::uint32_t node,
                                   const std::string& asset, const std::string& event, const std::map<std::string, std::string>& values);

        // The list scripts see as `args` (empty by default): what follows `--` on the command line of a `--run`. Takes effect for the
        // runs that start after the call.
        void setArguments(std::vector<std::string> arguments);

        // How a script thread reaches the main thread: `post` must run its task on the main thread later, from any thread (the application
        // passes RunLoop::post; a test passes a queue it drains). Without one runInBackground() is unavailable.
        void setMainThreadPoster(MainThreadPoster poster);
        bool canRunInBackground() const;

        // As run(), on a worker thread, and returns at once (false when one already runs or there is no poster). `done` is called on the
        // main thread, through the poster, with the result. Everything below, and the document, belong to the main thread meanwhile; the
        // script's edits are one undo step that ends before `done`. Main thread only.
        bool runInBackground(const std::string& code, SketchDocument* document, SelectionModel* selection, std::function<void(const ScriptResult&)> done);

        // True from the start of either kind of run until its result is ready.
        bool running() const;
        // What the running script printed so far, and what it reported with curvy.progress().
        std::string outputSoFar() const;
        ScriptProgress progress() const;

        // Cancels a background script and waits for its thread; `done` is not called. The owner calls this before the document or the
        // poster goes away. Main thread only; does nothing when no background script runs.
        void abort();

        // Stops the running script (from any thread; does nothing when none runs) by raising `curvy.Cancelled`, a BaseException, in it at the
        // next Python instruction, so `except Exception` does not swallow it. A long call into C++ (a cook, a render) finishes first.
        void cancel();

        // Called on the main thread when a script saved the document to a file or opened one into it (see setScriptDocumentFileHook), so
        // the window can refresh its title or everything it shows of the document.
        void setDocumentFileHook(ScriptDocumentFileHook hook);

        // Stops a script that runs longer than this, as cancel() does (and with the same limits: a long call into C++ finishes first). Zero,
        // the default, means no limit. Applies to the runs that start after the call.
        void setTimeout(std::chrono::seconds limit);

        // While a script runs, Escape pressed with one of this process's windows in front cancels it (a watcher thread polls the key; the
        // script itself runs at full speed). On by default.
        void setCancelOnEscape(bool enabled);

        // Compiles `code` without running it and returns the first syntax error, or nothing when it compiles. Nothing too while a script runs
        // (the interpreter is busy). Main thread only, like run().
        std::optional<SyntaxProblem> checkSyntax(const std::string& code);

        std::string version() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}
