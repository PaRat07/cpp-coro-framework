#pragma once

#include <vector>
#include <algorithm>
#include <ranges>

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


