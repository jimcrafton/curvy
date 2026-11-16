#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace pybind11 {
    class object;
}

namespace curvy {

    class SketchDocument;
    class SelectionModel;

    // Registers the `curvy` Python module (Document, Object, Selection, Swatch, Palette) in sys.modules. Needs the interpreter to be running and is
    // called once, by ScriptConsole, on the main thread.
    void registerCurvyModule();

    // Runs `task` on the main thread, later; callable from any thread (the application passes RunLoop::post).
    using MainThreadPoster = std::function<void(std::function<void()>)>;

    // Scripts that run on a worker thread reach the document through these: every call into a `curvy` or `jitnodes` class is handed to the
    // main thread through the poster and the script waits for it, so the document is only ever touched from one thread. Without a poster,
    // or on the main thread itself, calls run directly.
    void setScriptMainThreadPoster(MainThreadPoster poster);
    bool hasScriptMainThreadPoster();
    // While set, a script waiting for a call the main thread has not started gives up with curvy.Cancelled (the window is closing).
    void setScriptDispatchAborting(bool aborting);

    // Told, on the main thread, when a script saved the document to a file (contentReplaced false: the file name or the modified marker may
    // have changed) or opened a file into it (true: everything the window shows of the document is stale). The window sets this so it
    // can refresh; none by default.
    using ScriptDocumentFileHook = std::function<void(bool contentReplaced)>;
    void setScriptDocumentFileHook(ScriptDocumentFileHook hook);

    // What `curvy.progress(fraction, message)` last reported; fraction is negative until a script reports one.
    struct ScriptProgress {
        float fraction = -1.0f;
        std::string message;
    };
    ScriptProgress scriptProgress();
    void resetScriptProgress();

    // Makes everything one script run changes in a document a single undo step: begin() notes the depth of the document's undo stack, end()
    // merges the steps pushed since. The script's own undo, redo and new_blank end the step first and start a new one after (they move or
    // clear the history, so a depth taken before would mean nothing). One per console; used on the main thread only.
    // @reflect ignore=true
    class ScriptTransaction {
    public:
        void begin(SketchDocument& document);
        // Safe when the document was closed meanwhile.
        void end();
        bool covers(const SketchDocument& document) const;

    private:
        SketchDocument* document_ = nullptr;
        std::weak_ptr<void> alive_;
        std::size_t mark_ = 0;
    };

    // The `curvy.Document` a script sees as `doc`. It reaches the document through its lifetime token, so a script that keeps it after the
    // document closed gets an error instead of a crash. `transaction` must outlive every script object (the console owns both).
    // `selection` (the editor's, may be null) becomes `doc.selection`; it must outlive the script objects too.
    pybind11::object makeScriptDocument(SketchDocument& document, ScriptTransaction& transaction, SelectionModel* selection = nullptr);

}
