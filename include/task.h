#pragma once

#include <variant>
#include <exception>
#include <coroutine>
#include <print>
#include <iostream>
#include <boost/stacktrace/stacktrace.hpp>

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

// clang attributes for all the tasks
#define CORO_ATTRIBUTES nodiscard, clang::coro_await_elidable, clang::coro_return_type, clang::coro_lifetimebound, clang::coro_only_destroy_when_complete


// async task for passing async further by stack
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
            std::cout << boost::stacktrace::stacktrace() << std::endl;
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
        if (handle_) {
            handle_.destroy();
        }
    }

    Task(Task&& other) : handle_(std::exchange(other.handle_, {})) {
    }

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
    std::coroutine_handle<> to_resume;

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