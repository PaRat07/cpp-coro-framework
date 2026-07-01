#pragma once

#include <variant>
#include <exception>
#include <coroutine>
#include <utility>

#include <cassert>

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

// clang attributes for all the tasks
#define CORO_ATTRIBUTES nodiscard, clang::coro_await_elidable, clang::coro_return_type, clang::coro_lifetimebound, clang::coro_only_destroy_when_complete

constexpr bool kIsDebug = false;

template<typename Result>
struct RetBlock {
  std::variant<std::monostate, Result, std::exception_ptr> result;

  void return_value(Result res) {
    result.template emplace<Result>(std::move(res));
  }

  void unhandled_exception() {
    result.template emplace<std::exception_ptr>(std::current_exception());
  }

  Result Get() {
    if (std::holds_alternative<Result>(result)) [[likely]] {
      return std::get<Result>(std::move(result));
    } else if (std::holds_alternative<std::exception_ptr>(result)) {
      std::rethrow_exception(std::get<std::exception_ptr>(std::move(result)));
    } else {
      std::terminate();
    }
  }
};

template<>
struct RetBlock<void> {
  std::exception_ptr exc;

  void return_void() noexcept {}

  void unhandled_exception() {
    exc = std::current_exception();
  }

  void Get() {
    if (exc) [[unlikely]] {
      std::rethrow_exception(exc);
    }
  }
};

template<typename>
struct Task;

template<typename Result>
struct Promise : RetBlock<Result> {
  std::coroutine_handle<> caller_handle;

  Task<Result> get_return_object() noexcept {
    return Task<Result>{ std::coroutine_handle<Promise>::from_promise(*this) };
  }

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

  Result Get() {
    return RetBlock<Result>::Get();
  }
};

// async task for passing async further by stack
template <typename Result = void>
struct [[CORO_ATTRIBUTES]] Task {
public:
  template<typename>
  friend struct Promise;

    struct Awaiter {
      std::coroutine_handle<Promise<Result>> handle;

      bool await_ready() const noexcept { return false; }

      auto await_suspend(std::coroutine_handle<> calling) noexcept {
          handle.promise().caller_handle = calling;
          return handle;
      }

      Result await_resume() {
          return handle.promise().Get();
      }
    };

    using promise_type = Promise<Result>;

    Task() = default;

    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    void Release() {
        handle_ = {};
    }

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    Task(const Task& other) = delete;

    // Task& operator=(Task &&other) noexcept {
    //     assert(!handle_);
    //     handle_ = std::exchange(other.handle_, {});
    //     return *this;
    // }

    Awaiter operator co_await() noexcept { return Awaiter { handle_ }; }

    std::coroutine_handle<promise_type> GetHandle() {
        return handle_;
    }
private:
    explicit Task(std::coroutine_handle<promise_type> handle) noexcept
        : handle_(handle)
    {
    }

    std::coroutine_handle<promise_type> handle_;
};
