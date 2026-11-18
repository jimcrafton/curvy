#include "curvy/pythonnodetrust.h"

#include <cstdint>
#include <fstream>
#include <vector>

namespace curvy {

    void PythonNodeTrust::setTrustFile(const std::filesystem::path& file) {
        std::lock_guard<std::mutex> lock(mutex_);
        trustFile_ = file;
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) {
                trusted_.insert(line);
            }
        }
    }

    std::string PythonNodeTrust::codeHash(const std::string& code) {
        std::uint64_t hash = 1469598103934665603ull;
        for (const char c : code) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
        static const char digits[] = "0123456789abcdef";
        std::string text(16, '0');
        for (int i = 15; i >= 0; --i) {
            text[static_cast<std::size_t>(i)] = digits[hash & 0xf];
            hash >>= 4;
        }
        return text;
    }

    bool PythonNodeTrust::allows(const std::string& code) {
        if (code.find_first_not_of(" \t\r\n") == std::string::npos) {
            return true;   // nothing to run: a node that was just made has no code, and must not ask
        }
        const std::string hash = codeHash(code);
        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (trusted_.count(hash) != 0) {
                return true;
            }
            if (declined_.count(hash) != 0) {
                return false;
            }
            pending_.insert(hash);
            if (!noticed_ && notice_) {
                noticed_ = true;
                notify = true;
            }
        }
        if (notify) {
            notice_();
        }
        return false;
    }

    bool PythonNodeTrust::askPending() {
        std::set<std::string> hashes;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hashes.swap(pending_);
            noticed_ = false;
        }
        if (hashes.empty() || !prompt_) {
            return false;
        }
        const bool allowed = prompt_(hashes.size());
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::string& hash : hashes) {
            if (allowed) {
                remember(hash);
            } else {
                declined_.insert(hash);
            }
        }
        return allowed;
    }

    void PythonNodeTrust::trust(const std::string& code) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string hash = codeHash(code);
        pending_.erase(hash);
        declined_.erase(hash);
        remember(hash);
    }

    bool PythonNodeTrust::isTrusted(const std::string& code) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return trusted_.count(codeHash(code)) != 0;
    }

    std::size_t PythonNodeTrust::pendingCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_.size();
    }

    void PythonNodeTrust::remember(const std::string& hash) {
        if (!trusted_.insert(hash).second) {
            return;
        }
        if (!trustFile_.empty()) {
            std::error_code ignored;
            std::filesystem::create_directories(trustFile_.parent_path(), ignored);
            std::ofstream out(trustFile_, std::ios::app);
            out << hash << "\n";
        }
    }

}
