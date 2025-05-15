#pragma once

#include <chrono>
#include <coroutine>
#include <queue>


// event loop, that allows to sleep for a specific time
class TimedEventLoop {
public:
    using time_t = std::chrono::steady_clock::time_point;

    struct TaskHolder {
        time_t wait_for_time;
        std::coroutine_handle<> to_resume;

        constexpr auto operator<=>(const TaskHolder &rhs) const noexcept {
            return wait_for_time <=> rhs.wait_for_time;
        }
    };

    static void Resume() noexcept {
        while (!tasks_.empty() && tasks_.top().wait_for_time < std::chrono::steady_clock::now()) {
            auto [_, task] = tasks_.top();
            tasks_.pop();
            task.resume();
        }
    }

    static bool IsEmpty() noexcept {
        return tasks_.empty();
    }

    static void Init() noexcept {
        tasks_ = {};
    }

    struct AwaitableByTime {
        bool await_ready() const noexcept {
            return resume_time <= std::chrono::steady_clock::now();
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            tasks_.emplace(resume_time, handle);
        }
        void await_resume() const noexcept {}

        std::chrono::steady_clock::time_point resume_time;
    };

private:
    static inline std::priority_queue<TaskHolder, std::vector<TaskHolder>, std::greater<>> tasks_;
};


// suspend this coroutine for dur time
inline TimedEventLoop::AwaitableByTime SleepForImpl(std::chrono::steady_clock::duration dur) noexcept {
    return TimedEventLoop::AwaitableByTime{ std::chrono::steady_clock::now() + dur };
}

// suspend this coroutine for dur time
inline Task<> SleepFor(std::chrono::steady_clock::duration dur) noexcept {
    co_await SleepForImpl(dur);
    co_return;
}
