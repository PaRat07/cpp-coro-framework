#include <variant>
#include <vector>
#include <iostream>
#include <algorithm>
#include <print>
#include <new>
#include <chrono>
#include <bit>
#include <memory>
#include <exception>
#include <cassert>
#include <coroutine>
#include <queue>


template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

#define CORO_ATTRIBUTES nodiscard, clang::coro_await_elidable, clang::coro_return_type, clang::coro_lifetimebound, clang::coro_only_destroy_when_complete




template <typename Result = void>
class [[CORO_ATTRIBUTES]] Task {
public:
    struct Promise {
        std::variant<Result, std::exception_ptr> result;
        std::coroutine_handle<> to_resume;

        Task get_return_object() noexcept {
            return Task { std::coroutine_handle<Promise>::from_promise(*this) };
        }

        void unhandled_exception() noexcept {
            result.template emplace<std::exception_ptr>(std::current_exception());
        }

        void* operator new(std::size_t n) {
            std::println(std::cerr, "Allocated {} bytes at {}", n, __PRETTY_FUNCTION__);
            return ::operator new(n);
        }

        void operator delete(void *data) {
            std::println(std::cerr, "Deallocated");
            ::operator delete(data);
        }

        void return_value(auto&& res) noexcept { result.template emplace<Result>(std::forward<decltype(res)>(res)); }

        std::suspend_never initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept {
            struct FinalAwaiter {
                bool await_ready() const noexcept {
                    return false;
                }

                void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                    if (handle.promise().to_resume) {
                        handle.promise().to_resume.resume();
                    }
                }

                void await_resume() noexcept {}
            };
            return FinalAwaiter{};
        }

        Result GetResult() {
            return std::move(result).visit(overloaded {
                [] (Result &&res)               -> Result&& { return res; },
                [] (std::exception_ptr exc_ptr) -> Result&& { std::rethrow_exception(exc_ptr); }
            });
        }
    };

    struct Awaiter {
        Promise &promise;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> calling) noexcept {
            promise.to_resume = calling;
        }

        void await_resume() noexcept requires(std::is_same_v<Result, void>) {
            if (promise.exc_ptr) {
                std::rethrow_exception(promise.exc_ptr);
            }
        }

        Result await_resume() noexcept requires(!std::is_same_v<Result, void>) {
            return std::move(promise.result).visit(overloaded {
                [] (Result &&res)               -> Result&&{ return res; },
                [] (std::exception_ptr exc_ptr) -> Result&& { std::rethrow_exception(exc_ptr); }
            });
        }
    };

    using promise_type = Promise;

    Task() = default;

    ~Task() {
        handle_.destroy();
    }

    Task(Task&& other) = delete;
    Task(const Task& other) = delete;

    Awaiter operator co_await() noexcept { return Awaiter { handle_.promise() }; }

    std::coroutine_handle<Promise> GetHandle() {
        return handle_;
    }
private:
    explicit Task(std::coroutine_handle<Promise> handle) noexcept
        : handle_(handle)
    {
    }

    std::coroutine_handle<Promise> handle_;
};

template <>
struct Task<void>::Promise {
    std::exception_ptr exc_ptr;
    std::coroutine_handle<> continuation;

    Task get_return_object() noexcept {
        return Task { std::coroutine_handle<Promise>::from_promise(*this) };
    }

    void unhandled_exception() noexcept {
        exc_ptr = std::current_exception();
    }

    void* operator new(std::size_t n) {
        std::println(std::cerr, "Allocated {} bytes at {}", n, __PRETTY_FUNCTION__);
        return ::operator new(n);
    }

    void operator delete(void *data) {
        std::println(std::cerr, "Deallocated");
        ::operator delete(data);
    }

    void GetResult() {
        if (exc_ptr) {
            std::rethrow_exception(exc_ptr);
        }
    }

    void return_void() noexcept {}

    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
};

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



TimedEventLoop::AwaitableByTime SleepFor(std::chrono::steady_clock::duration dur) noexcept {
    return TimedEventLoop::AwaitableByTime{ std::chrono::steady_clock::now() + dur };
}

using namespace std::chrono_literals;



Task<int> Get(int x) noexcept {
    std::println("Before: {}", x);
    co_await SleepFor(1s);
    std::println("After: {}", x);
    co_return x;
}


Task<int> Print() noexcept {
    std::println("Got: {}", co_await Get(13));
    co_return 1;
}







template<typename ResT>
auto sync_wait([[clang::coro_await_elidable_argument]] Task<ResT> awaitable) {
    std::cerr << "Can do" << std::endl;
    while (!awaitable.GetHandle().done()) {
        assert(!TimedEventLoop::IsEmpty());
        TimedEventLoop::Resume();
    }
    return awaitable.GetHandle().promise().GetResult();
}

int main() {
    std::cout << sync_wait(Print()) << std::endl;

}
