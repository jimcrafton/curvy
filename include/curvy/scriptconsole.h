#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace curvy {

    class SketchDocument;

    struct ScriptResult {
        bool ok = true;
        // What the code printed, then the repr of its value when it was an expression that is not None.
        std::string output;
        // The traceback when the code raised.
        std::string error;
    };

    // The embedded Python interpreter and the one scope console input runs in (names persist from one run to the next). Create it on the
    // thread that runs scripts (the main thread) and destroy it there. One per process: CPython cannot be restarted.
    //
    // Scripts see `curvy.Document` as `doc` (page, undoable graph edits, undo and redo) and the document's graph as `network`. Edits
    // made on `network` directly are not undo steps; `doc` is the way to change the document.
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

        std::string version() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}
