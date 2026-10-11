#pragma once
#include <atomic>
#include <exception>
#include <utility>

namespace Wheel {
    // Always relinquish the queue slot, including early returns and C++ errors.
    // A failed task reports once; the caller decides whether to close its menu.
    template<class Work,class Error>
    void RunQueuedTask(std::atomic<bool>& queued,Work&& work,Error&& error) {
        struct Release {std::atomic<bool>& flag;~Release(){flag=false;}} release{queued};
        try {std::forward<Work>(work)();}
        catch(const std::exception& e){std::forward<Error>(error)(e.what());}
        catch(...){std::forward<Error>(error)("Unknown C++ exception");}
    }

    // The queue flag has already been acquired. A scheduling failure must also
    // release it, while successful scheduling leaves ownership to the callback.
    template<class Schedule,class Error>
    void ScheduleQueuedTask(std::atomic<bool>& queued,Schedule&& schedule,Error&& error) {
        try {std::forward<Schedule>(schedule)();}
        catch(const std::exception& e){queued=false;std::forward<Error>(error)(e.what());}
        catch(...){queued=false;std::forward<Error>(error)("Unknown C++ exception");}
    }
}
