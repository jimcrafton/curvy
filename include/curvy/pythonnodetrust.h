#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>

namespace curvy {

    // Decides whether the code of a jitnodes `python` node may run (docs/py-curvy-module.md, "Python nodes"). The code of a node that comes from
    // a file or a paste is somebody else's code, so a node whose exact text is not on the allowed list does not run: its cook fails, and the
    // first refusal asks the user once, on the main thread, through the prompt. The allowance is remembered by a hash of the code text in a
    // file, so an edited text asks again; a declined text is not asked about again in the same session.
    // allows() is the policy jitnodes asks on every cook, from any thread; everything else is for the main thread.
    // @reflect ignore=true
    class PythonNodeTrust {
    public:
        // Asks whether `count` python nodes with code that was not allowed before may run. Returns true to allow them.
        using Prompt = std::function<bool(std::size_t count)>;

        void setPrompt(Prompt prompt) { prompt_ = std::move(prompt); }
        // Called (on the thread of a cook) when a refused text is first seen; the application posts askPending() to its main thread.
        void setRefusedNotice(std::function<void()> notice) { notice_ = std::move(notice); }
        // The allowed hashes live in a text file, one per line; read now and appended to whenever the user allows more.
        void setTrustFile(const std::filesystem::path& file);

        static std::string codeHash(const std::string& code);

        // True when this exact code is allowed. Otherwise the text is remembered for askPending() (unless it was declined) and false is returned.
        bool allows(const std::string& code);
        // Allows or declines everything refused so far. Returns true when the user allowed it (the caller then recooks).
        bool askPending();
        // Allows this text without asking (code the user typed or a script of the user's own wrote).
        void trust(const std::string& code);
        bool isTrusted(const std::string& code) const;
        std::size_t pendingCount() const;

    private:
        void remember(const std::string& hash);

        Prompt prompt_;
        std::function<void()> notice_;
        std::filesystem::path trustFile_;
        mutable std::mutex mutex_;
        std::set<std::string> trusted_;
        std::set<std::string> declined_;
        std::set<std::string> pending_;
        bool noticed_ = false;
    };

}
