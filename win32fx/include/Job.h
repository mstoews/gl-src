//
// Job.h - batch work off the UI thread (spec #8 9.2; plan S8-M4 task 3).
//
// A job runs a function on a worker thread. The function opens its own
// connection to the company file (WAL lets the pages read meanwhile) and
// reports progress through its Context; the job posts kProgress to the window
// that started it, and kFinished at the end. Cancel sets a flag the function
// checks between steps (a voucher, an item), so it stops at a boundary with
// the books consistent.
//
// A job that writes holds model::JobGate: one writing job at a time, the
// second refused with the first's name. The state (model::JobState) is read
// under a lock from the UI thread.
//
#pragma once

#include "model/JobState.h"

#include <Windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace win32fx {

class Job {
public:
    static constexpr UINT kProgress = WM_APP + 60;   // wParam done, lParam of
    static constexpr UINT kFinished = WM_APP + 61;

    class Context {
    public:
        bool cancelled() const noexcept { return job_.cancel_.load(); }
        void Progress(int done, int of, const std::string& message = {});

    private:
        friend class Job;
        explicit Context(Job& job) : job_(job) {}
        Job& job_;
    };

    struct Outcome {
        bool ok = true;
        bool stoppedEarly = false;   // ended at a boundary because it was asked to cancel
        std::string message;
    };
    using Work = std::function<Outcome(Context&)>;

    Job() = default;
    ~Job();   // cancels, and waits for the worker
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    // Starts `work`. False (with why in `refusal`) if this job is already running,
    // or `writes` and another writing job holds the books.
    bool Start(HWND notify, const std::string& name, bool writes, Work work, std::string* refusal = nullptr);
    void Cancel();
    bool active() const;
    model::JobState state() const;
    // After kFinished: the worker is joined (the window's handler calls it).
    void Join();

private:
    void Run(Work work);

    mutable std::mutex lock_;
    model::JobState state_;
    std::atomic<bool> cancel_{false};
    std::thread thread_;
    HWND notify_ = nullptr;
    std::string name_;
    bool writes_ = false;
    bool started_ = false;   // a fresh job is not active
};

} // namespace win32fx
