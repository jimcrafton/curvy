#include "curvy/scriptconsole.h"

#include "curvy/scriptmodule.h"
#include "curvy/sketchdocument.h"

#include <pyHandles.h>
#include <pyRuntime.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace py = pybind11;

namespace curvy {

    namespace {

        bool escapePressedInThisProcess() {
            if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) == 0) {
                return false;
            }
            DWORD owner = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &owner);
            return owner == GetCurrentProcessId();
        }

    }

    std::filesystem::path executableDirectory() {
        std::wstring path(MAX_PATH, L'\0');
        for (;;) {
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0) {
                return {};
            }
            if (length < path.size()) {
                path.resize(length);
                break;
            }
            path.resize(path.size() * 2);
        }
        return std::filesystem::path(path).parent_path();
    }

    std::filesystem::path findPythonHome(const std::filesystem::path& executableDir, const std::filesystem::path& developmentHome) {
        const std::filesystem::path bundled = executableDir / "python";
        std::error_code ignored;
        if (!executableDir.empty() && std::filesystem::exists(bundled / "Lib" / "os.py", ignored)) {
            return bundled;
        }
        return developmentHome;
    }

    struct ScriptConsole::Impl {
        explicit Impl(const std::filesystem::path& home) : runtime({home, {}}) {
            registerCurvyModule();
            scope = runtime.newScope();
            py::module_::import("jitnodes");
            py::module_ module = py::module_::import("curvy");
            scope["curvy"] = module;
            cancelledType = module.attr("Cancelled");
        }

        // Raises curvy.Cancelled in the script's thread until the run ends. A thread that does not hold the GIL cannot touch the interpreter,
        // so this takes it; the script's thread gives it up every few milliseconds while it runs Python code.
        void watch(unsigned long scriptThread, std::atomic<bool>& stop) {
            while (!stop.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                const bool wanted = cancelRequested.load() || (cancelOnEscape.load() && escapePressedInThisProcess());
                if (!wanted || stop.load()) {
                    continue;
                }
                py::gil_scoped_acquire gil;
                if (!stop.load()) {
                    PyThreadState_SetAsyncExc(scriptThread, cancelledType.ptr());
                }
            }
        }

        // The script objects point at this, so it is declared before them and goes after them.
        ScriptTransaction transaction;
        // Declared in this order so the scope (Python objects) goes before the interpreter.
        jitnodes::python::PythonRuntime runtime;
        py::dict scope;
        py::object cancelledType;
        std::atomic<bool> running{false};
        std::atomic<bool> cancelRequested{false};
        std::atomic<bool> cancelOnEscape{true};
    };

    ScriptConsole::ScriptConsole(const std::filesystem::path& pythonHome) : impl_(std::make_unique<Impl>(pythonHome)) {}

    ScriptConsole::~ScriptConsole() = default;

    std::string ScriptConsole::version() const {
        return impl_->runtime.version();
    }

    void ScriptConsole::cancel() {
        if (impl_->running.load()) {
            impl_->cancelRequested = true;
        }
    }

    void ScriptConsole::setCancelOnEscape(bool enabled) {
        impl_->cancelOnEscape = enabled;
    }

    ScriptResult ScriptConsole::run(const std::string& code, SketchDocument* document) {
        ScriptResult result;
        py::module_ sys = py::module_::import("sys");
        py::object oldOut = sys.attr("stdout");
        py::object oldErr = sys.attr("stderr");
        py::object capture = py::module_::import("io").attr("StringIO")();
        sys.attr("stdout") = capture;
        sys.attr("stderr") = capture;

        const unsigned long scriptThread = PyThread_get_thread_ident();
        std::atomic<bool> stopWatcher{false};
        impl_->cancelRequested = false;
        impl_->running = true;
        if (document != nullptr) {
            impl_->transaction.begin(*document);
        }
        std::thread watcher([this, scriptThread, &stopWatcher] { impl_->watch(scriptThread, stopWatcher); });

        try {
            if (document != nullptr) {
                impl_->scope["doc"] = makeScriptDocument(*document, impl_->transaction);
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
            if (e.matches(impl_->cancelledType)) {
                result.cancelled = true;
                result.error = "Script cancelled\n";
            } else {
                result.error = e.what();
            }
        } catch (const std::exception& e) {
            result.ok = false;
            result.error = e.what();
        }

        impl_->transaction.end();

        // The watcher may be waiting for the GIL, so give it up while joining. An exception it raised after the script ended is withdrawn,
        // or it would fire in whatever Python code runs next.
        stopWatcher = true;
        {
            py::gil_scoped_release release;
            watcher.join();
        }
        impl_->running = false;
        PyThreadState_SetAsyncExc(scriptThread, nullptr);

        result.output = py::str(capture.attr("getvalue")());
        sys.attr("stdout") = oldOut;
        sys.attr("stderr") = oldErr;
        return result;
    }

}
