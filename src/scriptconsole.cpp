#include "curvy/scriptconsole.h"

#include "curvy/scriptmodule.h"
#include "curvy/sketchdocument.h"

#include <pyHandles.h>
#include <pyRuntime.h>

namespace py = pybind11;

namespace curvy {

    struct ScriptConsole::Impl {
        explicit Impl(const std::filesystem::path& home) : runtime({home, {}}) {
            registerCurvyModule();
            scope = runtime.newScope();
            py::module_::import("jitnodes");
            scope["curvy"] = py::module_::import("curvy");
        }

        // Declared in this order so the scope (Python objects) goes before the interpreter.
        jitnodes::python::PythonRuntime runtime;
        py::dict scope;
    };

    ScriptConsole::ScriptConsole(const std::filesystem::path& pythonHome) : impl_(std::make_unique<Impl>(pythonHome)) {}

    ScriptConsole::~ScriptConsole() = default;

    std::string ScriptConsole::version() const {
        return impl_->runtime.version();
    }

    ScriptResult ScriptConsole::run(const std::string& code, SketchDocument* document) {
        ScriptResult result;
        py::module_ sys = py::module_::import("sys");
        py::object oldOut = sys.attr("stdout");
        py::object oldErr = sys.attr("stderr");
        py::object capture = py::module_::import("io").attr("StringIO")();
        sys.attr("stdout") = capture;
        sys.attr("stderr") = capture;

        try {
            if (document != nullptr) {
                impl_->scope["doc"] = makeScriptDocument(*document);
                impl_->scope["network"] = jitnodes::python::wrapNetwork(document->network());
            } else {
                impl_->scope["doc"] = py::none();
                impl_->scope["network"] = py::none();
            }

            py::object builtins = py::module_::import("builtins");
            py::object value = py::none();
            bool isExpression = true;
            py::object compiled;
            try {
                compiled = builtins.attr("compile")(code, "<console>", "eval");
            } catch (const py::error_already_set& e) {
                if (!e.matches(PyExc_SyntaxError)) {
                    throw;
                }
                isExpression = false;
                compiled = builtins.attr("compile")(code, "<console>", "exec");
            }
            value = builtins.attr("eval")(compiled, impl_->scope);
            if (isExpression && !value.is_none()) {
                py::print(builtins.attr("repr")(value));
            }
        } catch (const py::error_already_set& e) {
            result.ok = false;
            result.error = e.what();
        } catch (const std::exception& e) {
            result.ok = false;
            result.error = e.what();
        }

        result.output = py::str(capture.attr("getvalue")());
        sys.attr("stdout") = oldOut;
        sys.attr("stderr") = oldErr;
        return result;
    }

}
