#include "curvy/scriptconsole.h"

#include "curvy/scriptmodule.h"
#include "curvy/sketchdocument.h"

#include <jitnodes/graph/network.h>
#include <jitnodes/graph/serialize.h>
#include <pybind11/eval.h>
#include <pyHandles.h>
#include <pyRuntime.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
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
            timedOut = false;
            running = true;
            resetScriptProgress();
            py::list argumentList;
            for (const std::string& argument : arguments) {
                argumentList.append(argument);
            }
            scope["args"] = argumentList;
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
                    if (timedOut.load() && !cancelRequested.load()) {
                        result.timedOut = true;
                        result.error = "Script stopped: it ran longer than " + std::to_string(timeoutSeconds.load()) +
                                       " seconds (the limit is in Settings, Script editor)\n";
                    } else {
                        result.error = "Script cancelled\n";
                    }
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
            const auto started = std::chrono::steady_clock::now();
            const std::chrono::seconds limit(timeoutSeconds.load());
            while (!stop.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                const bool late = limit.count() > 0 && std::chrono::steady_clock::now() - started >= limit;
                const bool wanted = late || cancelRequested.load() || (cancelOnEscape.load() && escapePressedInThisProcess());
                if (!wanted || stop.load()) {
                    continue;
                }
                if (late) {
                    timedOut = true;
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
        py::object eventRunner;
        py::object migrationRunner;
        std::thread::id mainThread = std::this_thread::get_id();
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
        std::vector<std::string> arguments;
        std::atomic<long long> timeoutSeconds{0};
        std::atomic<bool> timedOut{false};
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

    void ScriptConsole::setArguments(std::vector<std::string> arguments) {
        impl_->arguments = std::move(arguments);
    }

    void ScriptConsole::setDocumentFileHook(ScriptDocumentFileHook hook) {
        setScriptDocumentFileHook(std::move(hook));
    }

    void ScriptConsole::setTimeout(std::chrono::seconds limit) {
        impl_->timeoutSeconds = limit.count() > 0 ? limit.count() : 0;
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

    namespace {

        const char* const kAssetEventRunner = R"PY(
import types
import jitnodes
_modules = {}
_kinds = {"index": int, "frame": int, "version": int, "value": float, "playing": lambda text: text == "1"}

def module_of(asset, definition):
    key = (asset, definition.version)
    module = _modules.get(key)
    if module is None:
        module = types.ModuleType("curvy.assets." + asset)
        text = definition.scripts.get("module")
        if text:
            exec(compile(text, "<%s module>" % asset, "exec"), module.__dict__)
        _modules[key] = module
    return module

def run_migration(asset, old, fresh, inputs, from_version, to_version):
    definition = jitnodes.assets.get(asset)
    source = definition.scripts.get("onVersionUpdated")
    if not source:
        return inputs
    kwargs = {"old": old, "fresh": fresh, "inputs": inputs, "from_version": from_version, "to_version": to_version}
    names = dict(kwargs)
    names["kwargs"] = kwargs
    names["jitnodes"] = jitnodes
    code = compile(source, "<%s.onVersionUpdated>" % asset, "exec")
    with jitnodes.assets.restricted(fresh, "onVersionUpdated"):
        names["module"] = module_of(asset, definition)
        exec(code, names)
    return names["inputs"]

def run_event(node, asset, event, values, document, curvy):
    definition = jitnodes.assets.get(asset)
    source = definition.scripts.get(event)
    if not source:
        return
    module = module_of(asset, definition)
    kwargs = {}
    for key, text in values.items():
        kind = _kinds.get(key)
        kwargs[key] = kind(text) if kind else text
    kwargs["node"] = node
    names = {"kwargs": kwargs, "node": node, "network": node.network if node is not None else None, "doc": document, "module": module,
             "curvy": curvy, "jitnodes": jitnodes}
    code = compile(source, "<%s.%s>" % (asset, event), "exec")
    if node is None:
        exec(code, names)
    else:
        with jitnodes.assets.restricted(node, event):
            exec(code, names)
)PY";

    }

    ScriptResult ScriptConsole::runAssetEvent(SketchDocument& document, SelectionModel* selection, std::uint32_t scope, std::uint32_t node,
                                              const std::string& asset, const std::string& event, const std::map<std::string, std::string>& values) {
        ScriptResult result;
        if (impl_->running.load()) {
            result.ok = false;
            result.error = "A script is already running\n";
            return result;
        }
        impl_->begin(&document, selection);
        try {
            if (!impl_->eventRunner) {
                py::dict runnerScope;
                py::exec(kAssetEventRunner, runnerScope);
                impl_->eventRunner = runnerScope["run_event"];
                impl_->migrationRunner = runnerScope["run_migration"];
            }
            py::object handle = py::none();
            if (node != jitnodes::invalidNodeId) {
                handle = jitnodes::python::wrapNetwork(document.networkOf(scope)).attr("node")(node);
            }
            py::dict extra;
            for (const auto& [name, text] : values) {
                extra[py::str(name)] = text;
            }
            impl_->eventRunner(handle, asset, event, extra, impl_->scope["doc"], impl_->scope["curvy"]);
        } catch (const py::error_already_set& e) {
            result.ok = false;
            result.error = e.what();
        } catch (const std::exception& e) {
            result.ok = false;
            result.error = e.what();
        }
        impl_->end(result);
        return result;
    }

    namespace {
        constexpr std::chrono::seconds kMigrationLimit(5);
    }

    std::string ScriptConsole::runMigration(const std::string& asset, jitnodes::AssetUpgrade& upgrade) {
        // Main thread, the GIL held (the interpreter is idle between runs). The script gets two copies in a network of their own, so nothing
        // of the document is in reach, and only the copy of the new instance may be edited (EventRestriction::Mode::onlyThisNode).
        const auto onMainThread = [this, &asset, &upgrade]() -> std::string {
            if (impl_->running.load()) {
                return "a script is running";
            }
            std::string error;
            try {
                if (!impl_->migrationRunner) {
                    py::dict runnerScope;
                    py::exec(kAssetEventRunner, runnerScope);
                    impl_->eventRunner = runnerScope["run_event"];
                    impl_->migrationRunner = runnerScope["run_migration"];
                }
                auto scratch = std::make_shared<jitnodes::Network>();
                const jitnodes::NodeId oldId = scratch->addNode(upgrade.fresh.typeName(), "old");
                jitnodes::copyNodeParameters(upgrade.old, scratch->node(oldId), true);
                const jitnodes::NodeId freshId = scratch->addNode(upgrade.fresh.typeName(), "fresh");
                jitnodes::copyNodeParameters(upgrade.fresh, scratch->node(freshId), false);
                const jitnodes::python::NetworkRef network = jitnodes::python::NetworkRef::owning(scratch);
                py::list inputs;
                for (const jitnodes::NodeId id : upgrade.inputs) {
                    inputs.append(py::int_(id));
                }

                const unsigned long scriptThread = PyThread_get_thread_ident();
                std::atomic<bool> stop{false};
                std::atomic<bool> late{false};
                const std::chrono::seconds limit = kMigrationLimit;
                std::thread watcher([this, scriptThread, &stop, &late, limit]() {
                    const auto started = std::chrono::steady_clock::now();
                    while (!stop.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(25));
                        if (stop.load() || std::chrono::steady_clock::now() - started < limit) {
                            continue;
                        }
                        late = true;
                        py::gil_scoped_acquire gil;
                        if (!stop.load()) {
                            PyThreadState_SetAsyncExc(scriptThread, impl_->cancelledType.ptr());
                        }
                    }
                });
                py::list result;
                try {
                    result = impl_->migrationRunner(asset, py::cast(jitnodes::python::PyNode(network, oldId)),
                                                    py::cast(jitnodes::python::PyNode(network, freshId)), inputs, upgrade.fromVersion, upgrade.toVersion);
                } catch (const py::error_already_set& e) {
                    error = late.load() && e.matches(impl_->cancelledType)
                                ? "it ran longer than " + std::to_string(kMigrationLimit.count()) + " seconds"
                                : std::string(e.what());
                }
                stop = true;
                {
                    py::gil_scoped_release release;
                    watcher.join();
                }
                PyThreadState_SetAsyncExc(scriptThread, nullptr);
                if (!error.empty()) {
                    return error;
                }
                std::vector<jitnodes::NodeId> reordered;
                for (const py::handle item : result) {
                    reordered.push_back(item.cast<jitnodes::NodeId>());
                }
                if (reordered.size() != upgrade.inputs.size()) {
                    return "inputs must keep its length";
                }
                for (const jitnodes::NodeId id : reordered) {
                    if (id != jitnodes::invalidNodeId && std::find(upgrade.inputs.begin(), upgrade.inputs.end(), id) == upgrade.inputs.end()) {
                        return "inputs may only be reordered, but it holds " + std::to_string(id);
                    }
                }
                jitnodes::copyNodeParameters(scratch->node(freshId), upgrade.fresh, false);
                upgrade.inputs = std::move(reordered);
            } catch (const std::exception& e) {
                return e.what();
            }
            return {};
        };

        if (std::this_thread::get_id() == impl_->mainThread) {
            return onMainThread();
        }
        // A document load runs on a worker. The interpreter belongs to the main thread, so the run is handed over and waited for.
        if (!impl_->poster) {
            return "there is no main thread to run it on";
        }
        struct Handover {
            std::mutex mutex;
            std::condition_variable changed;
            bool started = false;
            bool done = false;
            bool abandoned = false;
            std::string error;
        };
        auto handover = std::make_shared<Handover>();
        impl_->poster([handover, &onMainThread]() {
            {
                std::lock_guard<std::mutex> lock(handover->mutex);
                if (handover->abandoned) {
                    return;   // the caller is gone: what onMainThread points at no longer exists
                }
                handover->started = true;
                handover->changed.notify_all();
            }
            std::string error = onMainThread();
            std::lock_guard<std::mutex> lock(handover->mutex);
            handover->error = std::move(error);
            handover->done = true;
            handover->changed.notify_all();
        });
        std::unique_lock<std::mutex> lock(handover->mutex);
        if (!handover->changed.wait_for(lock, std::chrono::seconds(10), [&handover] { return handover->started; })) {
            handover->abandoned = true;
            return "the main thread was busy for 10 seconds";
        }
        handover->changed.wait(lock, [&handover] { return handover->done; });   // the script has a time limit of its own
        return handover->error;
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
