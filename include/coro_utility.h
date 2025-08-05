#pragma once

#include <coroutine>
#include <stack>
#include <vector>

#include "task.h"
#include "my-queue.h"

struct NoSuspendTask;

class spawn_task {
public:
  using promise_type = NoSuspendTask;
  std::coroutine_handle<promise_type> coro_;
};
struct NoSuspendTask {
  std::coroutine_handle<NoSuspendTask> self;
  // Called by the compiler to get the coroutine's return object:
  auto get_return_object() noexcept {
    return spawn_task{ std::coroutine_handle<NoSuspendTask>::from_promise(*this) };
  }

  std::suspend_never initial_suspend() noexcept { return {}; }

  std::suspend_never final_suspend() noexcept { return {}; }

  void return_void() noexcept {}
  void unhandled_exception() noexcept { std::terminate(); }
};
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


// waits for all the coros and returns tuple of their results
// if task returns void it returns std::type_identity<void>
template<typename... Ts>
Task<std::tuple<DevoidifyedT<Ts>...>> WhenAll([[clang::coro_await_elidable_argument]] Task<Ts>&&... tasks) {
    std::tuple<DevoidifyedT<Ts>...> ans;
    co_await [] <size_t... Inds> (auto &ans, std::index_sequence<Inds...>, [[clang::coro_await_elidable_argument]] Task<Ts>&&... tasks) -> Task<> {
        co_await WhenAllImpl(SaveTo(Devoidify(std::move(tasks...[Inds])), std::get<Inds>(ans))...);
    } (ans, std::make_index_sequence<sizeof...(Ts)>{}, std::move(tasks)...);

    co_return ans;
}



Task<> WhenAll([[clang::coro_await_elidable_argument]] std::span<Task<>> tasks) {
    std::coroutine_handle<> self = co_await Self();
    for (auto &i : tasks) {
        i.GetHandle().promise().caller_handle = self;
    }
    for (auto &i : tasks) {
        i.GetHandle().resume();
    }
    for (size_t i = 0; i < tasks.size(); ++i) {
        co_await std::suspend_always{};
    }
    co_return;
}




template <typename Awaitable>
auto spawn(Awaitable awaitable) -> void {
    [] (Awaitable awaitable) static -> spawn_task {
        try {
          co_await awaitable;
        } catch (const std::exception &exc) {
          std::cerr << "Thrown out of spawned coro: " << exc.what() << std::endl;
        } catch (...) {
          std::cerr << "Thrown out of spawned coro: <unknow exception type>" << std::endl;
        }
    } (std::move(awaitable));
}

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

struct BinarySemaphore {
  void Release() {
    locked = false;
    if (handle) {
      std::exchange(handle, {}).resume();
    }
  }
  Task<> Acquire() {
    if (!locked) {
      locked = true;
      co_return;
    } else if (handle) {
      [] noexcept {
        throw std::runtime_error("cant Acquire multiple coros on same BinarySemaphore");
      } ();
    }
    co_await WriteHandle{handle};
    locked = true;
    co_return;
  }


  std::coroutine_handle<> handle;
  bool locked = true;
};

template<typename T>
struct RsCoroMutex {
private:
  Queue<std::coroutine_handle<>> tasks_;
  T val;
  bool owned = false;

public:
  RsCoroMutex(T obj) : val(std::move(obj)) {}

  struct CoroLockGuard {
  public:
    using CoroLockGuardImpl = std::unique_ptr<RsCoroMutex, decltype([] (RsCoroMutex *leaser) {})>;
    explicit CoroLockGuard(RsCoroMutex &leaser) {
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

    friend struct RsCoroMutex;
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
