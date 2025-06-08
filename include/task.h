#pragma once

#include <variant>
#include <exception>
#include <coroutine>
#include <print>
#include <iostream>
// #include <boost/stacktrace/stacktrace.hpp>
#include <cassert>

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

// clang attributes for all the tasks
#define CORO_ATTRIBUTES nodiscard, clang::coro_await_elidable, clang::coro_return_type, clang::coro_lifetimebound, clang::coro_only_destroy_when_complete

constexpr bool kIsDebug = false;

// async task for passing async further by stack
template <typename Result = void>
class [[CORO_ATTRIBUTES]] Task {
public:
    struct Promise {
        std::variant<std::monostate, Result, std::exception_ptr> result;
        std::coroutine_handle<> caller_handle;

        Task get_return_object() noexcept {
            return Task { std::coroutine_handle<Promise>::from_promise(*this) };
        }

        void unhandled_exception() noexcept {
            result.template emplace<std::exception_ptr>(std::current_exception());
        }

        void* operator new(std::size_t n) {
            if constexpr (kIsDebug) {
                std::println(std::cerr, "Allocated {} bytes at {}", n, __PRETTY_FUNCTION__);
                // std::cout << boost::stacktrace::stacktrace() << std::endl;
            }
            return ::operator new(n);
        }

        void operator delete(void *data) {
            if constexpr (kIsDebug) {
                std::println(std::cerr, "Deallocated");
            }
            ::operator delete(data);
        }

        void return_value(Result&& res) noexcept { result.template emplace<Result>(std::move(res)); }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() const noexcept {
                return false;
            }

            auto await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                return handle.promise().caller_handle;
            }

            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept {
            return {};
        }

        Result GetResult() {
            return std::move(result).visit(overloaded {
                [] (Result &&res)               -> Result&& { return res; },
                [] (std::exception_ptr exc_ptr) -> Result&& { std::rethrow_exception(exc_ptr); },
                [] (std::monostate)             -> Result&& { std::terminate(); }
            });
        }
    };

    struct Awaiter {
         std::coroutine_handle<Promise> handle;

        bool await_ready() const noexcept { return false; }

        auto await_suspend(std::coroutine_handle<> calling) noexcept {
            handle.promise().caller_handle = calling;
            return handle;
        }

        void await_resume() requires(std::is_same_v<Result, void>) {
            if (handle.promise().exc_ptr) {
                std::rethrow_exception(handle.promise().exc_ptr);
            }
        }

        Result await_resume() requires(!std::is_same_v<Result, void>) {
            return std::move(handle.promise().result).visit(overloaded {
                [] (Result &&res)               -> Result&& { return res; },
                [] (std::exception_ptr exc_ptr) -> Result&& { std::rethrow_exception(exc_ptr); },
                [] (std::monostate)             -> Result&& { std::terminate(); }
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

    void Release() {
        handle_ = {};
    }

    Task(Task&& other) : handle_(std::exchange(other.handle_, {})) {}

    Task(const Task& other) = delete;

    Task &operator=(Task &&other) {
        assert(!handle_);
        handle_ = std::exchange(other.handle_, {});
        return *this;
    }

    Awaiter operator co_await() noexcept { return Awaiter { handle_ }; }

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
    std::coroutine_handle<> caller_handle;

    Task get_return_object() noexcept {
        return Task { std::coroutine_handle<Promise>::from_promise(*this) };
    }

    void unhandled_exception() noexcept {
        exc_ptr = std::current_exception();
    }

    void* operator new(std::size_t n) {
        if constexpr (kIsDebug) {
            std::println(std::cerr, "Allocated {} bytes at {}", n, __PRETTY_FUNCTION__);
        }
        return ::operator new(n);
    }

    void operator delete(void *data) {
        if constexpr (kIsDebug) {
            std::println(std::cerr, "Deallocated");
        }
        ::operator delete(data);
    }

    void GetResult() {
        if (exc_ptr) {
            std::rethrow_exception(exc_ptr);
        }
    }

    void return_void() noexcept {}

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() const noexcept {
            return false;
        }

        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept {
           return handle.promise().caller_handle;
        }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept {
        return {};
    }
};