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
    if (to_resume_on_non_empty) {
      std::exchange(to_resume_on_non_empty, {}).resume();
    }
  }

  Task<T> Pop() {
    if (queue.Empty()) {
      if (to_resume_on_non_empty) [[unlikely]] {
        [] noexcept {
          throw std::runtime_error("cant suspend multiple coros on same queue");
        } ();
      }
      co_await WriteHandle{ to_resume_on_non_empty };
      co_return queue.Pop();
    } else {
      co_return queue.Pop();
    }
  }
private:
  Queue<T> queue;
  // BinarySemaphore sem;
  std::coroutine_handle<> to_resume_on_non_empty = {};
};

