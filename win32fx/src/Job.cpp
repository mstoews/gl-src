//
// Job.cpp - see Job.h.
//
#include "Job.h"

#include <exception>

namespace win32fx {

void Job::Context::Progress(int done, int of, const std::string& message)
{
    {
        std::lock_guard<std::mutex> g(job_.lock_);
        job_.state_.Progress(done, of, message);
    }
    if (job_.notify_) PostMessageW(job_.notify_, kProgress, static_cast<WPARAM>(done), static_cast<LPARAM>(of));
}

Job::~Job()
{
    Cancel();
    Join();
}

bool Job::Start(HWND notify, const std::string& name, bool writes, Work work, std::string* refusal)
{
    if (thread_.joinable()) {
        if (active()) {
            if (refusal) *refusal = name_ + " is still running.";
            return false;
        }
        Join();
    }
    std::string holder;
    if (writes && !model::JobGate::TryAcquire(name, &holder)) {
        if (refusal) *refusal = holder + " is writing to the books: wait for it to finish.";
        return false;
    }
    {
        std::lock_guard<std::mutex> g(lock_);
        state_ = model::JobState{};
        state_.Start();
        started_ = true;
    }
    cancel_ = false;
    notify_ = notify;
    name_ = name;
    writes_ = writes;
    thread_ = std::thread(&Job::Run, this, std::move(work));
    return true;
}

void Job::Run(Work work)
{
    Context ctx(*this);
    Outcome o;
    try {
        o = work(ctx);
    } catch (const std::exception& e) {
        o = {false, false, e.what()};
    } catch (...) {
        o = {false, false, "the job stopped with an unknown error"};
    }
    {
        std::lock_guard<std::mutex> g(lock_);
        state_.Finish(o.ok, o.stoppedEarly, o.message);
    }
    if (writes_) model::JobGate::Release(name_);
    if (notify_) PostMessageW(notify_, kFinished, 0, 0);
}

void Job::Cancel()
{
    std::lock_guard<std::mutex> g(lock_);
    if (started_ && state_.active()) {
        state_.Cancel();
        cancel_ = true;
    }
}

bool Job::active() const
{
    std::lock_guard<std::mutex> g(lock_);
    return started_ && state_.active();
}

model::JobState Job::state() const
{
    std::lock_guard<std::mutex> g(lock_);
    return state_;
}

void Job::Join()
{
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

} // namespace win32fx
