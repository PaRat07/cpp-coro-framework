#pragma once

#include "util/my-queue.h"
#include "runtime/task.h"

#include <memory>

struct NoSuspendTask;

class spawn_task {
public:
  using promise_type = NoSuspendTask;
  std::coroutine_handle<promise_type> coro_;
};
struct NoSuspendTask {
  std::coroutine_handle<NoSuspendTask> self;

  auto get_return_object() noexcept {
    return spawn_task{ std::coroutine_handle<NoSuspendTask>::from_promise(*this) };
  }

  std::suspend_never initial_suspend() noexcept { return {}; }

  std::suspend_never final_suspend() noexcept { return {}; }

  void return_void() noexcept {}
  void unhandled_exception() noexcept { std::terminate(); }
};

void _log_spawn_exception(const std::exception* ep);

template <typename Awaitable>
void spawn(Awaitable awaitable) {
  [] (Awaitable awaitable) static -> spawn_task {
    try {
      co_await awaitable;
    } catch (const std::exception &exc) {
      _log_spawn_exception(&exc);
    } catch (...) {
      _log_spawn_exception(nullptr);
    }
  } (std::move(awaitable));
}

// usage std::coroutine_handle<> my_handle = co_await Self();
inline auto Self() {
    struct SelfAwaitable {
        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            handle_ = handle;
            handle.resume();
        }

        std::coroutine_handle<> await_resume() const noexcept {
            return handle_;
        }
        std::coroutine_handle<> handle_;
    };

    return SelfAwaitable{};
}

template<typename T>
using DevoidifyedT = std::conditional_t<std::is_same_v<T, void>, std::type_identity<void>, T>;

template<typename T>
Task<DevoidifyedT<T>> Devoidify([[clang::coro_await_elidable_argument]] Task<T>&& task) {
    if constexpr (std::is_same_v<T, void>) {
        co_await task;
        co_return {};
    } else {
        co_return co_await task;
    }
}

template<typename T>
Task<> SaveTo([[clang::coro_await_elidable_argument]] Task<T> &&task, T &place) {
    place = co_await task;
    co_return;
}

template<std::same_as<Task<>>... Ts>
Task<> WhenAllImpl([[clang::coro_await_elidable_argument]] Ts&&... tasks) {
    std::coroutine_handle<> self = co_await Self();
    ((tasks.GetHandle().promise().caller_handle = self), ...);
    (tasks.GetHandle().resume(), ...);
    for (size_t i = 0; i < sizeof...(Ts); ++i) {
        co_await std::suspend_always{};
    }
    co_return;
}


template<typename... Ts>
Task<std::tuple<DevoidifyedT<Ts>...>> WhenAll([[clang::coro_await_elidable_argument]] Task<Ts>&&... tasks) {
    std::tuple<DevoidifyedT<Ts>...> ans;
    co_await [] <size_t... Inds> (auto &ans, std::index_sequence<Inds...>, [[clang::coro_await_elidable_argument]] Task<Ts>&&... tasks) -> Task<> {
        co_await WhenAllImpl(SaveTo(Devoidify(std::move(tasks...[Inds])), std::get<Inds>(ans))...);
    } (ans, std::make_index_sequence<sizeof...(Ts)>{}, std::move(tasks)...);

    co_return ans;
}



Task<> WhenAll(std::span<Task<>> tasks);

struct WriteHandle {
  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> handle) noexcept {
    to_write = handle;
  }

  void await_resume() const noexcept {}

  std::coroutine_handle<> &to_write;
};

template<typename T>
struct InvokeWithHandle {
  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> handle) noexcept {
    func(handle);
  }

  void await_resume() const noexcept {}

  T func;
};

struct Mutex {
public:
  auto lock() -> Task<> {
    struct LockAwaitable {
      bool await_ready() const noexcept { return !locked; }

      void await_suspend(std::coroutine_handle<> handle) noexcept {
        tasks.Push(handle);
      }

      void await_resume() const noexcept {}

      Queue<std::coroutine_handle<>>& tasks;
      bool locked;
    };
    co_return co_await LockAwaitable {
      .tasks = tasks_,
      .locked = locked_
    };
  }

  auto unlock() {
    locked_ = false;
    if (!tasks_.Empty()) {
      tasks_.Pop().resume();
    }
  }

private:
  Queue<std::coroutine_handle<>> tasks_;
  bool locked_ = false;
};

template<typename T>
struct RsMutex {
private:
  Queue<std::coroutine_handle<>> tasks_;
  T val;
  bool owned = false;

public:
  RsMutex(T obj) : val(std::move(obj)) {}

  struct CoroLockGuard {
  public:
    using CoroLockGuardImpl = std::unique_ptr<RsMutex, decltype([] (RsMutex *leaser) {})>;
    explicit CoroLockGuard(RsMutex &leaser) {
      leaser_ = CoroLockGuardImpl{ &leaser };
    }

    CoroLockGuard(CoroLockGuard&&) = default;
    CoroLockGuard &operator=(CoroLockGuard&&) = default;


    T &Get() {
      return leaser_->val;
    }

    bool valueless_after_move() {
      return !leaser_;
    }

    ~CoroLockGuard() {
      if (!leaser_) return;
      if (!leaser_->tasks_.Empty()) {
        leaser_->tasks_.Pop().resume();
      } else {
        leaser_->owned = false;
      }
    }

    friend struct RsMutex;
  private:
    CoroLockGuardImpl leaser_;
  };

  Task<CoroLockGuard> Lease() {
    if (owned) {
      co_await InvokeWithHandle {
        [this] (std::coroutine_handle<> hand) { tasks_.Push(hand); }
      };
    } else {
      owned = true;
    }
    co_return CoroLockGuard(*this);
  }
};


