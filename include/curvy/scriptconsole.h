#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace curvy {

    class SketchDocument;

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
    };

    // The embedded Python interpreter and the one scope console input runs in (names persist from one run to the next). Create it on the
    // thread that runs scripts (the main thread) and destroy it there. One per process: CPython cannot be restarted.
    //
    // Scripts see `curvy.Document` as `doc` (page, undoable graph edits, undo and redo) and the document's graph as `network`. Edits
    // made on `network` directly are not undo steps; `doc` is the way to change the document.
    //
    // Scripts run on the calling thread and block it; cancel() and Escape are how to stop one that runs too long.
    // @reflect ignore=true
    class ScriptConsole {
    public:
        // `pythonHome` is the bundled Python folder. Throws std::runtime_error when it does not start.
        explicit ScriptConsole(const std::filesystem::path& pythonHome);
        ~ScriptConsole();

        ScriptConsole(const ScriptConsole&) = delete;
        ScriptConsole& operator=(const ScriptConsole&) = delete;

        // Runs `code` with `doc` and `network` bound to `document` (both None when it is null). Never throws for a script error.
        ScriptResult run(const std::string& code, SketchDocument* document);

        // Stops the running script (from any thread; does nothing when none runs) by raising `curvy.Cancelled`, a BaseException, in it at the
        // next Python instruction, so `except Exception` does not swallow it. A long call into C++ (a cook, a render) finishes first.
        void cancel();

        // While a script runs, Escape pressed with one of this process's windows in front cancels it (a watcher thread polls the key; the
        // script itself runs at full speed). On by default.
        void setCancelOnEscape(bool enabled);

        std::string version() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}
