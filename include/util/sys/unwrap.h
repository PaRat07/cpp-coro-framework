#pragma once

#include <system_error>

inline void ThrowFromErrno() {
  throw std::system_error(errno, std::system_category());
}


template<std::integral Int>
Int Unwrap(Int res) {
  if (res < 0) [[unlikely]] {
    ThrowFromErrno();
  }
  return res;
}
