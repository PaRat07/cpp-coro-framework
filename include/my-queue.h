#pragma once

#include <vector>
#include <algorithm>
#include <ranges>
#include "coro_utility.h"

template<typename T>
class Queue {
public:
  static_assert(std::is_trivially_copyable_v<T>);

  bool Empty() const noexcept {
    return cnt == 0;
  }

  void Push(auto &&val) requires (std::is_same_v<std::remove_cvref_t<decltype(val)>, T>) {
    if (cnt == cont.size()) [[unlikely]] {
      std::vector<T> new_cont(cont.size() * 2 + 1);
      std::copy(cont.begin() + beg, cont.end(), new_cont.begin());
      std::copy(cont.begin(), cont.begin() + beg, new_cont.begin() + beg);
      std::swap(cont, new_cont);
      beg = 0;
    }
    cont[(beg + cnt) % cont.size()] = val;
    ++cnt;
  }

  T Pop() {
    assert(!Empty());
    --cnt;
    return std::move(cont[std::exchange(beg, (beg + 1) % cont.size())]);
  }

private:
  std::vector<T> cont;
  ptrdiff_t cnt = 0;
  ptrdiff_t beg = 0;
};


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

