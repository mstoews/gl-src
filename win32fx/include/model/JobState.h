//
// JobState.h - a background job's state, without a thread or a window
// (spec #8 9.2; plan S8-M4 task 3). Portable: fx::Job keeps one, and
// gl_tests checks its transitions.
//
//   Queued --Start--> Running --Finish--> Done | Failed | Cancelled
//      |                 |
//      |              Cancel --> Cancelling --Finish--> Cancelled (or Done, if it ended anyway)
//      +--Cancel--> Cancelled
//
// JobGate lets one writing job run at a time in the process: the second is
// refused (the page says which job holds the books), never queued behind.
//
#pragma once

#include <mutex>
#include <string>

namespace win32fx::model {

enum class JobPhase { Queued, Running, Cancelling, Done, Failed, Cancelled };

const char* PhaseName(JobPhase p);

class JobState {
public:
    JobPhase phase() const noexcept { return phase_; }
    bool active() const noexcept { return phase_ == JobPhase::Queued || phase_ == JobPhase::Running || phase_ == JobPhase::Cancelling; }
    bool cancelRequested() const noexcept { return phase_ == JobPhase::Cancelling || phase_ == JobPhase::Cancelled; }
    int done() const noexcept { return done_; }
    int of() const noexcept { return of_; }        // 0: not known
    const std::string& message() const noexcept { return message_; }

    bool Start();                      // Queued -> Running
    bool Cancel();                     // Queued -> Cancelled, Running -> Cancelling
    bool Progress(int done, int of, const std::string& message = {});   // while Running or Cancelling
    // The work returned: ok, or failed with `message`. A job asked to cancel that
    // returns ok before its end is Cancelled; one that had nothing left is Done.
    bool Finish(bool ok, bool stoppedEarly, const std::string& message);

private:
    JobPhase phase_ = JobPhase::Queued;
    int done_ = 0, of_ = 0;
    std::string message_;
};

class JobGate {
public:
    // False, with the holder's name, if another writing job holds the books.
    static bool TryAcquire(const std::string& job, std::string* holder = nullptr);
    static void Release(const std::string& job);
    static bool Busy();
    static std::string Holder();   // "" when free

private:
    static std::mutex& lock();
    static std::string& holder();
};

} // namespace win32fx::model

namespace fx = win32fx;
