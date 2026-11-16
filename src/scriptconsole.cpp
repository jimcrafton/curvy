#include "curvy/scriptconsole.h"

#include "curvy/scriptmodule.h"
#include "curvy/sketchdocument.h"

#include <pyHandles.h>
#include <pyRuntime.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
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

    namespace {

        // sys.stdout and sys.stderr of a run: kept in C++ so the main thread can show what a script on another thread printed so far,
        // without the GIL.
        class OutputSink {
        public:
            void write(const std::string& text) {
                std::lock_guard<std::mutex> lock(mutex_);
                text_ += text;
            }

            std::string text() const {
                std::lock_guard<std::mutex> lock(mutex_);
                return text_;
            }

            void clear() {
                std::lock_guard<std::mutex> lock(mutex_);
                text_.clear();
            }

        private:
            mutable std::mutex mutex_;
            std::string text_;
        };

    }

    struct ScriptConsole::Impl {
        explicit Impl(const std::filesystem::path& home) : runtime({home, {}}) {
            registerCurvyModule();
            scope = runtime.newScope();
            py::module_::import("jitnodes");
            py::module_ module = py::module_::import("curvy");
            scope["curvy"] = module;
            cancelledType = module.attr("Cancelled");
            py::class_<OutputSink>(module, "_Output")
                .def("write", [](OutputSink& sink, const std::string& text) {
                    sink.write(text);
                    return text.size();
                })
                .def("flush", [](OutputSink&) {})
                .def("isatty", [](OutputSink&) { return false; });
            sink = new OutputSink();
            sinkObject = py::cast(sink, py::return_value_policy::take_ownership);
        }

        // Setup and teardown of a run; both on the main thread with the GIL.
        void begin(SketchDocument* document, SelectionModel* selection) {
            py::module_ sys = py::module_::import("sys");
            oldOut = sys.attr("stdout");
            oldErr = sys.attr("stderr");
            sink->clear();
            sys.attr("stdout") = sinkObject;
            sys.attr("stderr") = sinkObject;
            cancelRequested = false;
            running = true;
            resetScriptProgress();
            if (document != nullptr) {
                transaction.begin(*document);
                scope["doc"] = makeScriptDocument(*document, transaction, selection);
                scope["network"] = jitnodes::python::wrapNetwork(document->network());
                scope["selection"] = scope["doc"].attr("selection");
            } else {
                scope["doc"] = py::none();
                scope["network"] = py::none();
                scope["selection"] = py::none();
            }
        }

        void end(ScriptResult& result) {
            transaction.end();
            result.output = sink->text();
            py::module_ sys = py::module_::import("sys");
            sys.attr("stdout") = oldOut;
            sys.attr("stderr") = oldErr;
            oldOut = py::object();
            oldErr = py::object();
            running = false;
        }

        // Compiles and evaluates `code`; the GIL is held, on whichever thread runs the script.
        void execute(const std::string& code, ScriptResult& result) {
            const unsigned long scriptThread = PyThread_get_thread_ident();
            std::atomic<bool> stopWatcher{false};
            std::thread watcher([this, scriptThread, &stopWatcher] { watch(scriptThread, stopWatcher); });

            try {
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
                value = builtins.attr("eval")(compiled, scope);
                if (isExpression && !value.is_none()) {
                    py::print(builtins.attr("repr")(value));
                }
            } catch (const py::error_already_set& e) {
                result.ok = false;
                if (e.matches(cancelledType)) {
                    result.cancelled = true;
                    result.error = "Script cancelled\n";
                } else {
                    result.error = e.what();
                }
            } catch (const std::exception& e) {
                result.ok = false;
                result.error = e.what();
            }

            // The watcher may be waiting for the GIL, so give it up while joining. An exception it raised after the script ended is
            // withdrawn, or it would fire in whatever Python code runs next.
            stopWatcher = true;
            {
                py::gil_scoped_release release;
                watcher.join();
            }
            PyThreadState_SetAsyncExc(scriptThread, nullptr);
        }

        // Main thread, no GIL held (the background run's release is active): the worker is over.
        void reapWorker() {
            if (worker.joinable()) {
                worker.join();
            }
            mainRelease.reset();
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
        py::object sinkObject;
        OutputSink* sink = nullptr;
        py::object oldOut;
        py::object oldErr;
        std::atomic<bool> running{false};

        // The background run.
        bool background = false;
        std::uint64_t runId = 0;
        MainThreadPoster poster;
        std::thread worker;
        std::unique_ptr<py::gil_scoped_release> mainRelease;
        std::string code;
        ScriptResult result;
        std::function<void(const ScriptResult&)> done;
        std::atomic<bool> cancelRequested{false};
        std::atomic<bool> cancelOnEscape{true};
    };

    ScriptConsole::ScriptConsole(const std::filesystem::path& pythonHome) : impl_(std::make_unique<Impl>(pythonHome)) {}

    ScriptConsole::~ScriptConsole() {
        abort();
    }

    void ScriptConsole::setMainThreadPoster(MainThreadPoster poster) {
        impl_->poster = poster;
        setScriptMainThreadPoster(std::move(poster));
    }

    bool ScriptConsole::canRunInBackground() const {
        return hasScriptMainThreadPoster();
    }

    bool ScriptConsole::running() const {
        return impl_->running.load();
    }

    std::string ScriptConsole::outputSoFar() const {
        return impl_->sink->text();
    }

    ScriptProgress ScriptConsole::progress() const {
        return scriptProgress();
    }

    bool ScriptConsole::runInBackground(const std::string& code, SketchDocument* document, SelectionModel* selection,
                                        std::function<void(const ScriptResult&)> done) {
        if (impl_->running.load() || !hasScriptMainThreadPoster()) {
            return false;
        }
        impl_->begin(document, selection);
        impl_->background = true;
        impl_->code = code;
        impl_->result = ScriptResult();
        impl_->done = std::move(done);
        // The worker needs the GIL, which this thread holds for as long as the interpreter lives, so it is given up until the run is reaped.
        impl_->mainRelease = std::make_unique<py::gil_scoped_release>();
        const std::uint64_t id = ++impl_->runId;
        impl_->worker = std::thread([this, id, poster = impl_->poster] {
            {
                py::gil_scoped_acquire gil;
                impl_->execute(impl_->code, impl_->result);
            }
            poster([this, id] {
                if (!impl_->background || impl_->runId != id) {
                    return;   // abort() reaped this run already
                }
                impl_->reapWorker();
                impl_->end(impl_->result);
                impl_->background = false;
                std::function<void(const ScriptResult&)> done = std::move(impl_->done);
                impl_->done = nullptr;
                if (done) {
                    done(impl_->result);
                }
            });
        });
        return true;
    }

    void ScriptConsole::abort() {
        if (!impl_->background) {
            return;
        }
        impl_->cancelRequested = true;
        setScriptDispatchAborting(true);
        impl_->reapWorker();
        setScriptDispatchAborting(false);
        impl_->end(impl_->result);
        impl_->background = false;
        impl_->done = nullptr;
    }

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

    std::optional<SyntaxProblem> ScriptConsole::checkSyntax(const std::string& code) {
        if (impl_->running.load()) {
            return std::nullopt;
        }
        try {
            py::module_::import("builtins").attr("compile")(code, "<script>", "exec");
            return std::nullopt;
        } catch (const py::error_already_set& e) {
            if (!e.matches(PyExc_SyntaxError)) {
                return std::nullopt;   // something else (a null character): not a syntax problem to point at
            }
            const py::object error = e.value();
            const auto number = [&error](const char* name, int fallback) {
                py::object value = py::getattr(error, name, py::none());
                return value.is_none() ? fallback : value.cast<int>();
            };
            SyntaxProblem problem;
            problem.line = (std::max)(number("lineno", 1), 1);
            problem.column = (std::max)(number("offset", 1), 1);
            problem.endLine = (std::max)(number("end_lineno", problem.line), problem.line);
            problem.endColumn = (std::max)(number("end_offset", problem.column), 1);
            py::object message = py::getattr(error, "msg", py::none());
            problem.message = message.is_none() ? std::string("invalid syntax") : py::str(message).cast<std::string>();
            return problem;
        }
    }

    ScriptResult ScriptConsole::run(const std::string& code, SketchDocument* document, SelectionModel* selection) {
        ScriptResult result;
        if (impl_->running.load()) {
            result.ok = false;
            result.error = "A script is already running\n";
            return result;
        }
        impl_->begin(document, selection);
        impl_->execute(code, result);
        impl_->end(result);
        return result;
    }

}
