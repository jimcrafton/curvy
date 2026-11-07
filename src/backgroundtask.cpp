#include "curvy/backgroundtask.h"

namespace curvy {

    BackgroundTask::~BackgroundTask() {
        progress_.cancel();
        if (thread_.joinable()) {
            thread_.join();   // the job holds references to its owner: it must be over before that goes
        }
    }

    bool BackgroundTask::start(Job job) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (running_) {
                return false;
            }
            running_ = true;
        }
        if (thread_.joinable()) {
            thread_.join();   // the previous job is over (running_ was false); this only reaps the thread
        }
        progress_.reset();
        thread_ = std::thread([this, job = std::move(job)]() {
            try {
                job(progress_);
            } catch (...) {
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                running_ = false;
            }
            idle_.notify_all();
        });
        return true;
    }

    bool BackgroundTask::running() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return running_;
    }

    bool BackgroundTask::waitFor(std::chrono::milliseconds limit) {
        std::unique_lock<std::mutex> lock(mutex_);
        return idle_.wait_for(lock, limit, [this]() { return !running_; });
    }

}
