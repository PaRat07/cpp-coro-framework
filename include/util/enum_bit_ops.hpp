#pragma once

#include <type_traits>
#include <utility>

template<typename T>
constexpr bool kEnableEnumBitOps = false;


#define ENUM_OP(OP)                                                             \
  template<typename T> requires kEnableEnumBitOps<T>                               \
  T operator OP(T lhs, T rhs) {                                                 \
    return static_cast<T>(std::to_underlying(lhs) | std::to_underlying(rhs));   \
  }

ENUM_OP(|)
ENUM_OP(&)
ENUM_OP(^)

#undef ENUM_OP

template<typename T> requires kEnableEnumBitOps<T>
T operator~(T val) {
  return static_cast<T>(~std::to_underlying(val));
}

