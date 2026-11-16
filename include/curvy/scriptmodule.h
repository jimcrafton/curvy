#pragma once

#include <cstddef>
#include <memory>

namespace pybind11 {
    class object;
}

namespace curvy {

    class SketchDocument;
    class SelectionModel;

    // Registers the `curvy` Python module (Document, Object, Selection, Swatch, Palette) in sys.modules. Needs the interpreter to be running and is
    // called once, by ScriptConsole.
    void registerCurvyModule();

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
