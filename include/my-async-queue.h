#pragma once

#include <vector>
#include <algorithm>
#include <ranges>
#include "coro_utility.h"

#include "my-queue.h"

template<typename T>
class AsyncQueue {
public:

  bool Empty() const noexcept {
    return queue.Empty();
  }

  void Push(auto &&val) requires (std::is_same_v<std::remove_cvref_t<decltype(val)>, T>) {
    queue.Push(std::forward<decltype(val)>(val));
    if (!to_resume_on_non_empty.Empty()) {
      to_resume_on_non_empty.Pop().resume();
    }
  }

  Task<T> Pop() {
    if (queue.Empty()) {
      co_await InvokeWithHandle{ [this] (std::coroutine_handle<> h) { to_resume_on_non_empty.Push(h); } };
      co_return queue.Pop();
    } else {
      co_return queue.Pop();
    }
  }
private:
  Queue<T> queue;
  Queue<std::coroutine_handle<>> to_resume_on_non_empty;
};

