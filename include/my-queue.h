#pragma once

#include <vector>
#include <algorithm>
#include <ranges>

template<typename T>
class Queue {
public:
  void Push(auto &&val) requires (std::is_same_v<std::remove_cvref_t<decltype(val)>, T>) {
    if (cnt == cont.size()) [[unlikely]] {
      std::vector<T> new_cont(cont.size());
      if constexpr (std::is_nothrow_move_assignable_v<T>) {
        std::copy(std::make_move_iterator(cont.begin() + beg), std::make_move_iterator(std::min(cont.begin() + beg + cnt, cont.end())), new_cont.begin());
        std::copy(std::make_move_iterator(cont.begin()), std::make_move_iterator(cont.begin() + std::max(0, cnt + beg - cont.size())), new_cont.begin() + beg + cnt - cont.size());
      } else {
        std::copy(cont.begin() + beg, std::min(cont.begin() + beg + cnt, cont.end()), new_cont.begin());
        std::copy(cont.begin(), cont.begin() + std::max(0, cnt + beg - cont.size()), new_cont.begin() + beg + cnt - cont.size());
      }
      std::swap(cont, new_cont);
      beg = 0;
    }
    cont[(beg + cnt) % cont.size()] = std::forward<decltype(val)>(val);
  }

  T Pop() {
    --cnt;
    ptrdiff_t old_beg = beg++;
    return std::move(cont[old_beg]);
  }

private:
  std::vector<T> cont;
  ptrdiff_t cnt = 0;
  ptrdiff_t beg = 0;
};


