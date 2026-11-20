//
// JobState.cpp - see JobState.h.
//
#include "model/JobState.h"

namespace win32fx::model {

const char* PhaseName(JobPhase p)
{
    switch (p) {
    case JobPhase::Queued: return "queued";
    case JobPhase::Running: return "running";
    case JobPhase::Cancelling: return "cancelling";
    case JobPhase::Done: return "done";
    case JobPhase::Failed: return "failed";
    case JobPhase::Cancelled: return "cancelled";
    }
    return "";
}

bool JobState::Start()
{
    if (phase_ != JobPhase::Queued) return false;
    phase_ = JobPhase::Running;
    return true;
}

bool JobState::Cancel()
{
    switch (phase_) {
    case JobPhase::Queued: phase_ = JobPhase::Cancelled; return true;
    case JobPhase::Running: phase_ = JobPhase::Cancelling; return true;
    default: return false;
    }
}

bool JobState::Progress(int done, int of, const std::string& message)
{
    if (phase_ != JobPhase::Running && phase_ != JobPhase::Cancelling) return false;
    done_ = done;
    of_ = of;
    if (!message.empty()) message_ = message;
    return true;
}

bool JobState::Finish(bool ok, bool stoppedEarly, const std::string& message)
{
    if (phase_ != JobPhase::Running && phase_ != JobPhase::Cancelling) return false;
    message_ = message;
    if (!ok) phase_ = JobPhase::Failed;
    else if (phase_ == JobPhase::Cancelling && stoppedEarly) phase_ = JobPhase::Cancelled;
    else phase_ = JobPhase::Done;
    return true;
}

std::mutex& JobGate::lock()
{
    static std::mutex m;
    return m;
}

std::string& JobGate::holder()
{
    static std::string h;
    return h;
}

bool JobGate::TryAcquire(const std::string& job, std::string* who)
{
    std::lock_guard<std::mutex> g(lock());
    if (!holder().empty()) {
        if (who) *who = holder();
        return false;
    }
    holder() = job.empty() ? std::string("a job") : job;
    return true;
}

void JobGate::Release(const std::string& job)
{
    std::lock_guard<std::mutex> g(lock());
    if (holder() == (job.empty() ? std::string("a job") : job)) holder().clear();
}

bool JobGate::Busy()
{
    std::lock_guard<std::mutex> g(lock());
    return !holder().empty();
}

std::string JobGate::Holder()
{
    std::lock_guard<std::mutex> g(lock());
    return holder();
}

} // namespace win32fx::model
