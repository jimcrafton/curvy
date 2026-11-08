#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace curvy {

    // Thrown by TaskProgress::check() once the task was asked to stop; a worker lets it unwind to the task's top.
    struct TaskCancelled {};

    // What a worker reports and what the UI asks of it. Both directions are atomics, so neither side blocks the other.
    class TaskProgress {
    public:
        // 0..1, or negative while the worker cannot tell how far along it is (an indeterminate spinner).
        void set(float fraction) { fraction_.store(fraction, std::memory_order_relaxed); }
        float fraction() const { return fraction_.load(std::memory_order_relaxed); }

        void cancel() { cancelled_.store(true, std::memory_order_relaxed); }
        bool cancelled() const { return cancelled_.load(std::memory_order_relaxed); }
        void check() const {
            if (cancelled()) {
                throw TaskCancelled();
            }
        }
        void reset() {
            fraction_.store(-1.0f, std::memory_order_relaxed);
            cancelled_.store(false, std::memory_order_relaxed);
        }

    private:
        std::atomic<float> fraction_{-1.0f};
        std::atomic<bool> cancelled_{false};
    };

    // One job on one worker thread at a time. The job reports through the TaskProgress it is given and returns when done or cancelled.
    // Whoever needs the result (the UI) is told by the job itself, e.g. with RunLoop::post; waitFor() is for waiting on the thread's end.
    class BackgroundTask {
    public:
        typedef std::function<void(TaskProgress&)> Job;

        BackgroundTask() = default;
        BackgroundTask(const BackgroundTask&) = delete;
        BackgroundTask& operator=(const BackgroundTask&) = delete;
        ~BackgroundTask();

        // False when a job is still running. An exception escaping the job is swallowed: the job reports its own outcome.
        bool start(Job job);
        bool running() const;
        void cancel() { progress_.cancel(); }
        // True once no job is running (immediately when none was); false after `limit` with the job still going.
        bool waitFor(std::chrono::milliseconds limit);

        TaskProgress& progress() { return progress_; }
        const TaskProgress& progress() const { return progress_; }

    private:
        mutable std::mutex mutex_;
        std::condition_variable idle_;
        std::thread thread_;
        bool running_ = false;
        TaskProgress progress_;
    };

}
