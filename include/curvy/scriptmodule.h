#pragma once

namespace pybind11 {
    class object;
}

namespace curvy {

    class SketchDocument;

    // Registers the `curvy` Python module (Document, Object, Swatch, Palette) in sys.modules. Needs the interpreter to be running and is
    // called once, by ScriptConsole.
    void registerCurvyModule();

    // The `curvy.Document` a script sees as `doc`. It reaches the document through its lifetime token, so a script that keeps it after the
    // document closed gets an error instead of a crash.
    pybind11::object makeScriptDocument(SketchDocument& document);

}
